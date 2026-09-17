/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#include "MqttClient.h"

#include "AppModel.h"
#include "MqttPort.h"
#include "Platform.h"
#include "StorageOptions.h"

#include "MathEx.h"
#include "Map/MapPrimitives.h"

#include "ThirdParty/rapidjson/document.h"
#include "ThirdParty/rapidjson/stringbuffer.h"
#include "ThirdParty/rapidjson/writer.h"

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

//     target_compile_definitions(${COMPONENT_LIB} PRIVATE MQTT_PROVISION_KEY="...")
#ifndef MQTT_PROVISION_KEY
#define MQTT_PROVISION_KEY "948a3r4nbh9jsawohr3r"
#endif

#ifndef MQTT_PROVISION_SECRET
#define MQTT_PROVISION_SECRET "n9a9crpt7aqa5c5t2h7q"
#endif

// Where the broker is, when nothing in the environment says otherwise.
#ifndef MQTT_HOST
#define MQTT_HOST "thing.kanardia.eu"
#endif

namespace app {

namespace {

	constexpr const char* TAG = "mqtt";

// ThingsBoard's own topics, the ones Nesis's CloudClient uses.
	constexpr const char* TOPIC_TELEMETRY		= "v1/devices/me/telemetry";
	constexpr const char* TOPIC_RPC_FILTER		= "v1/devices/me/rpc/request/+";
	constexpr const char* TOPIC_RPC_PREFIX		= "v1/devices/me/rpc/request/";
	constexpr const char* TOPIC_RPC_RESPONSE	= "v1/devices/me/rpc/response/";
	constexpr const char* TOPIC_PROVISION_REQ = "/provision/request";
	constexpr const char* TOPIC_PROVISION_RES = "/provision/response";

// The user name a device with no credentials yet connects under.
	constexpr const char* PROVISION_USER = "provision";

// One entry in the settings store, next to the option blobs. NVS allows 15
// characters and this is four.
	constexpr const char* CREDENTIALS_KEY = "mqtt";

// What the port may leave waiting for the model task. A broker that floods us
// must not grow the heap without limit; the oldest message is the one worth
// losing, since a remote call nobody answered has long since timed out.
	constexpr size_t QUEUE_MAX = 8;

// Refuse a payload larger than this outright. A layout is a few kB of XML; a
// megabyte is somebody else's idea of what this unit is.
	constexpr size_t PAYLOAD_MAX = 32 * 1024;

	constexpr uint32_t TELEMETRY_EVERY_S = 10;
// After a provisioning round trip, and after one that failed.
	constexpr uint32_t REOPEN_AFTER_S			= 2;
	constexpr uint32_t REOPEN_AFTER_FAILURE_S = 30;

	// --- Environment ----------------------------------------------------

// ESPP4_MQTT_HOST names the broker, and its presence is also what turns the
// client on at boot -- see MqttClient::IsConfigured(). "host" or "host:port".
	const char* EnvHost()
	{
		const char* pszHost = std::getenv("ESPP4_MQTT_HOST");
		return (pszHost != nullptr && *pszHost != '\0') ? pszHost : nullptr;
	}

	// --- JSON -----------------------------------------------------------

	std::string GetString(const rapidjson::Value& v, const char* pszName, const char* pszDef = "")
	{
		if(v.IsObject() == false)
			return pszDef;
		const auto it = v.FindMember(pszName);
		if(it == v.MemberEnd() || it->value.IsString() == false)
			return pszDef;
		return std::string(it->value.GetString(), it->value.GetStringLength());
	}

	int GetInt(const rapidjson::Value& v, const char* pszName, int iDef)
	{
		if(v.IsObject() == false)
			return iDef;
		const auto it = v.FindMember(pszName);
		if(it == v.MemberEnd() || it->value.IsInt() == false)
			return iDef;
		return it->value.GetInt();
	}

// The server spells the shape of a message the way Nesis's showMessage does.
	MqttClient::MessageType ParseMessageType(const std::string& ssType)
	{
		if(ssType == "warning")
			return MqttClient::MessageType::Warning;
		if(ssType == "caution")
			return MqttClient::MessageType::Caution;
		return MqttClient::MessageType::Info;
	}

} // namespace

// --------------------------------------------------------------------------

MqttClient::MqttClient(bool bDebug) :
	m_bDebug(bDebug)
{}

// --------------------------------------------------------------------------

MqttClient::~MqttClient()
{
	Disconnect();
}

// --------------------------------------------------------------------------

bool MqttClient::IsConfigured()
{
	return EnvHost() != nullptr;
}

// --------------------------------------------------------------------------

bool MqttClient::Connect(const char* pszHost)
{
	if(m_pPort == nullptr) {
		m_pPort = CreateMqttPort(
			[this](std::string_view svTopic, std::string_view svData) { OnMessage(svTopic, svData); },
			[this](bool bConnected) { OnState(bConnected); }
		);
		if(m_pPort == nullptr) {
			APP_LOGE(TAG, "no MQTT port on this build");
			return false;
		}
	}

	// "host" or "host:port", from wherever the host came from.
	std::string ssHost = pszHost != nullptr ? pszHost : (EnvHost() != nullptr ? EnvHost() : MQTT_HOST);
	m_uPort				 = 1883;
	if(const size_t uColon = ssHost.rfind(':'); uColon != std::string::npos) {
		const int iPort = std::atoi(ssHost.c_str() + uColon + 1);
		if(iPort > 0 && iPort < 65536) {
			m_uPort = static_cast<uint16_t>(iPort);
			ssHost.resize(uColon);
		}
	}
	m_ssHost = std::move(ssHost);

	return Open();
}

// --------------------------------------------------------------------------

bool MqttClient::Open()
{
	if(m_pPort == nullptr || m_ssHost.empty())
		return false;

	MqttPort::Config cfg;
	cfg.pszHost = m_ssHost.c_str();
	cfg.uPort	= m_uPort;

	// A stored token is only ours if it was issued to the name we still answer
	// to. A device renamed under a token that names something else would talk
	// to somebody else's device on the server, which is worse than provisioning
	// again from scratch.
	const std::string							  ssName = GetDeviceName();
	const std::optional<CloudCredentials> creds	= ReadCredentials();

	m_bProvision = true;
	if(creds.has_value()) {
		if(creds->ssId == ssName) {
			cfg.pszClientId = creds->ssId.c_str();
			cfg.pszUser		 = creds->ssToken.c_str();
			m_bProvision	 = false;
		}
		else {
			APP_LOGW(TAG, "stored credentials name '%s', we are '%s'", creds->ssId.c_str(), ssName.c_str());
		}
	}

	if(m_bProvision)
		cfg.pszUser = PROVISION_USER;

	m_eState = State::Connecting;
	if(m_pPort->Start(cfg) == false) {
		m_eState = State::Off;
		APP_LOGE(TAG, "cannot reach %s:%u", m_ssHost.c_str(), static_cast<unsigned>(m_uPort));
		return false;
	}

	APP_LOGI(
		TAG,
		"connecting to %s:%u as '%s'%s",
		m_ssHost.c_str(),
		static_cast<unsigned>(m_uPort),
		ssName.c_str(),
		m_bProvision ? " (provisioning)" : ""
	);
	return true;
}

// --------------------------------------------------------------------------

void MqttClient::Disconnect()
{
	if(m_pPort != nullptr)
		m_pPort->Stop();

	m_eState			 = State::Off;
	m_bConnected	 = false;
	m_bStateChanged = false;

	const std::lock_guard<std::mutex> lock(m_mutex);
	m_queue.clear();
}

// --------------------------------------------------------------------------

bool MqttClient::IsConnected() const
{
	return m_eState == State::Online;
}

// --------------------------------------------------------------------------

const char* MqttClient::GetStateName() const
{
	switch(m_eState) {
	case State::Off:			  return "off";
	case State::Connecting:	  return "connecting";
	case State::Provisioning: return "provisioning";
	case State::Online:		  return "online";
	}
	return "?";
}

// --------------------------------------------------------------------------
//  On the port's thread
// --------------------------------------------------------------------------

void MqttClient::OnMessage(std::string_view svTopic, std::string_view svData)
{
	if(svData.size() > PAYLOAD_MAX) {
		APP_LOGW(TAG, "%s: %u bytes is too much, dropped", std::string(svTopic).c_str(), unsigned(svData.size()));
		return;
	}

	if(m_bDebug)
		APP_LOGD(TAG, "rx '%s', %u bytes", std::string(svTopic).c_str(), unsigned(svData.size()));

	const std::lock_guard<std::mutex> lock(m_mutex);
	if(m_queue.size() >= QUEUE_MAX) {
		m_queue.pop_front();
		m_uDropped++;
	}
	m_queue.push_back(Incoming{std::string(svTopic), std::string(svData)});
}

// --------------------------------------------------------------------------

void MqttClient::OnState(bool bConnected)
{
	m_bConnected	 = bConnected;
	m_bStateChanged = true;
}

// --------------------------------------------------------------------------
//  On the model task
// --------------------------------------------------------------------------

void MqttClient::Pump()
{
	if(m_pPort == nullptr)
		return;

	if(m_bStateChanged.exchange(false)) {
		if(m_bConnected)
			OnConnected();
		else
			OnDisconnected();
	}

	// One at a time, and out of the lock: handling a message publishes, and a
	// publish must not happen with the queue held.
	for(;;) {
		Incoming in;
		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			if(m_queue.empty())
				break;
			in = std::move(m_queue.front());
			m_queue.pop_front();
		}

		if(in.ssTopic == TOPIC_PROVISION_RES)
			HandleProvision(in.ssData);
		else if(in.ssTopic.compare(0, std::strlen(TOPIC_RPC_PREFIX), TOPIC_RPC_PREFIX) == 0)
			HandleRpcRequest(in.ssTopic, in.ssData);
		else if(m_bDebug)
			APP_LOGD(TAG, "unhandled topic '%s'", in.ssTopic.c_str());
	}
}

// --------------------------------------------------------------------------

void MqttClient::Update1s()
{
	if(m_pPort == nullptr)
		return;

	// The hand-over after provisioning, and the retry after one that failed.
	// Both go through a stopped port rather than a second connection: the
	// broker will not take new credentials on a session it has already
	// accepted under the old ones.
	if(m_uReopenIn > 0) {
		if(--m_uReopenIn == 0)
			Open();
		return;
	}

	if(m_eState != State::Online)
		return;

	if(++m_uTelemetryTick >= TELEMETRY_EVERY_S) {
		m_uTelemetryTick = 0;
		SendTelemetry();
	}
}

// --------------------------------------------------------------------------

void MqttClient::OnConnected()
{
	if(m_bProvision) {
		m_eState = State::Provisioning;
		m_pPort->Subscribe(TOPIC_PROVISION_RES);

		rapidjson::StringBuffer							 buf;
		rapidjson::Writer<rapidjson::StringBuffer> w(buf);
		w.StartObject();
		w.Key("deviceName");
		const std::string ssName = GetDeviceName();
		w.String(ssName.c_str());
		w.Key("provisionDeviceKey");
		w.String(MQTT_PROVISION_KEY);
		w.Key("provisionDeviceSecret");
		w.String(MQTT_PROVISION_SECRET);
		w.EndObject();

		m_pPort->Publish(TOPIC_PROVISION_REQ, std::string_view(buf.GetString(), buf.GetSize()));
		APP_LOGI(TAG, "claiming '%s'", ssName.c_str());
		return;
	}

	// Subscribed on every connect, not once: a port that lost the broker and
	// found it again starts a clean session, and the subscription went with
	// the old one.
	m_eState = State::Online;
	m_pPort->Subscribe(TOPIC_RPC_FILTER);
	m_uTelemetryTick = TELEMETRY_EVERY_S; // say hello straight away
	APP_LOGI(TAG, "online");
}

// --------------------------------------------------------------------------

void MqttClient::OnDisconnected()
{
	if(m_eState != State::Off)
		m_eState = State::Connecting;

	if(m_bDebug)
		APP_LOGD(TAG, "broker gone; the port will try again");
}

// --------------------------------------------------------------------------
//  Provisioning
// --------------------------------------------------------------------------

void MqttClient::HandleProvision(std::string_view svData)
{
	rapidjson::Document doc;
	doc.Parse(svData.data(), svData.size());

	const std::string ssStatus = doc.HasParseError() ? "" : GetString(doc, "status");
	const std::string ssType	= GetString(doc, "credentialsType");
	const std::string ssToken	= GetString(doc, "credentialsValue");

	// The port is stopped either way: this connection has no identity and
	// nothing else to say on it.
	m_pPort->Stop();
	m_bConnected	 = false;
	m_bStateChanged = false;

	if(ssStatus == "SUCCESS" && ssType == "ACCESS_TOKEN" && ssToken.empty() == false) {
		WriteCredentials(ssToken);
		m_uReopenIn = REOPEN_AFTER_S;
		APP_LOGI(TAG, "claimed; reconnecting with our own token");
	}
	else {
		m_eState		= State::Connecting;
		m_uReopenIn = REOPEN_AFTER_FAILURE_S;
		APP_LOGW(
			TAG,
			"provisioning refused (status '%s'); retrying in %u s",
			ssStatus.empty() ? "?" : ssStatus.c_str(),
			static_cast<unsigned>(REOPEN_AFTER_FAILURE_S)
		);
	}
}

// --------------------------------------------------------------------------
//  Remote calls
// --------------------------------------------------------------------------

void MqttClient::HandleRpcRequest(std::string_view svTopic, std::string_view svData)
{
	// v1/devices/me/rpc/request/<id> -- the answer goes to the same id.
	const std::string ssId(svTopic.substr(std::strlen(TOPIC_RPC_PREFIX)));
	const std::string ssResponse = std::string(TOPIC_RPC_RESPONSE) + ssId;

	rapidjson::Document doc;
	doc.Parse(svData.data(), svData.size());
	if(doc.HasParseError() || doc.IsObject() == false) {
		APP_LOGW(TAG, "rpc %s: not an object", ssId.c_str());
		return;
	}

	const std::string ssMethod = GetString(doc, "method");

	// params is where every call this product answers keeps its arguments.
	static const rapidjson::Value					  kEmpty(rapidjson::kObjectType);
	const rapidjson::Value::ConstMemberIterator itParams = doc.FindMember("params");
	const rapidjson::Value&							  params =
		  (itParams != doc.MemberEnd() && itParams->value.IsObject()) ? itParams->value : kEmpty;

	if(ssMethod == "showMessage") {
		Message msg;
		msg.ssText		= GetString(params, "message", "N/A");
		msg.eType		= ParseMessageType(GetString(params, "type", "info"));
		msg.iTimeoutMs = GetInt(params, "timeout", 5000);

		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			m_message = std::move(msg);
		}
		m_uRpc++;
		m_pPort->Publish(ssResponse.c_str(), R"({"status":"show"})");
		APP_LOGI(TAG, "message: %s", GetString(params, "message", "N/A").c_str());
	}
	else if(ssMethod == "sendLayout") {
		const std::string ssXml = GetString(params, "xml_content");
		if(ssXml.empty()) {
			APP_LOGW(TAG, "sendLayout: empty xml_content");
			m_pPort->Publish(ssResponse.c_str(), R"({"processed":false})");
			return;
		}

		{
			const std::lock_guard<std::mutex> lock(m_mutex);
			m_layout.ssTitle = GetString(params, "title", "N/A");
			m_layout.ssXml	  = ssXml;
		}
		m_uRpc++;
		m_pPort->Publish(ssResponse.c_str(), R"({"processed":true})");
		APP_LOGI(TAG, "layout '%s', %u bytes", GetString(params, "title", "N/A").c_str(), unsigned(ssXml.size()));
	}
	else {
		// Everything else the server asks of a Nesis -- the terminal, the
		// logbook, the autopilot -- means nothing here. Nesis answers nothing
		// to a method it does not know either, and the caller times out.
		m_uRpcIgnored++;
		if(m_bDebug)
			APP_LOGD(TAG, "rpc %s: '%s' not answered here", ssId.c_str(), ssMethod.c_str());
	}
}

// --------------------------------------------------------------------------

std::optional<MqttClient::Message> MqttClient::TakeMessage()
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	if(m_message.has_value() == false)
		return std::nullopt;

	Message msg = std::move(*m_message);
	m_message.reset();
	return msg;
}

// --------------------------------------------------------------------------

MqttClient::Layout MqttClient::GetLayout() const
{
	const std::lock_guard<std::mutex> lock(m_mutex);
	return m_layout;
}

// --------------------------------------------------------------------------
//  Telemetry
// --------------------------------------------------------------------------

void MqttClient::SendTelemetry()
{
	const Model* pModel = GetModel();
	if(pModel == nullptr)
		return;

	// The same fields Nesis's slow telemetry carries, minus what this board
	// has no source for. The server reads them by name, so the names matter
	// and the rest does not.
	const auto	coor	  = pModel->GetCoordinate();
	const char* pszWhat = "Parked";
	if(pModel->IsEngineRunning())
		pszWhat = "Running";
	if(pModel->IsFlying())
		pszWhat = "Flying";

	rapidjson::StringBuffer							 buf;
	rapidjson::Writer<rapidjson::StringBuffer> w(buf);
	w.StartObject();
	w.Key("status");
	w.String(pszWhat);
	if(coor.second) {
		w.Key("latitude");
		w.Double(common::Deg(static_cast<double>(coor.first.GetLatitude())));
		w.Key("longitude");
		w.Double(common::Deg(static_cast<double>(coor.first.GetLongitude())));
	}
	w.Key("alt");
	w.Double(std::round(pModel->GetAltitude()));
	w.Key("ias");
	w.Double(std::round(pModel->GetIAS()));
	w.Key("rpm");
	w.Double(std::round(pModel->GetEngineRPM()));
	w.EndObject();

	m_pPort->Publish(TOPIC_TELEMETRY, std::string_view(buf.GetString(), buf.GetSize()));
}

// --------------------------------------------------------------------------
//  Credentials
// --------------------------------------------------------------------------

std::optional<CloudCredentials> MqttClient::ReadCredentials() const
{
	common::BLOB blob;
	if(GetSettings().ReadBlob(CREDENTIALS_KEY, blob) == false || blob.empty())
		return std::nullopt;

	// The same shape Nesis keeps in mqtt.json, in the store this product has
	// instead of a filesystem.
	rapidjson::Document doc;
	doc.Parse(reinterpret_cast<const char*>(blob.data()), blob.size());
	if(doc.HasParseError() || doc.IsObject() == false) {
		APP_LOGW(TAG, "stored credentials are unreadable");
		return std::nullopt;
	}

	CloudCredentials creds;
	creds.ssId	  = GetString(doc, "id");
	creds.ssToken = GetString(doc, "token");
	if(creds.ssToken.empty())
		return std::nullopt;

	return creds;
}

// --------------------------------------------------------------------------

void MqttClient::WriteCredentials(const std::string& ssToken)
{
	const std::string ssName = GetDeviceName();

	rapidjson::StringBuffer							 buf;
	rapidjson::Writer<rapidjson::StringBuffer> w(buf);
	w.StartObject();
	w.Key("id");
	w.String(ssName.c_str());
	w.Key("token");
	w.String(ssToken.c_str());
	w.EndObject();

	const common::SpanBLOB span(reinterpret_cast<const uint8_t*>(buf.GetString()), buf.GetSize());
	if(GetSettings().WriteBlob(CREDENTIALS_KEY, span) == false)
		APP_LOGE(TAG, "credentials could not be stored; this unit will claim itself again on the next boot");
}

// --------------------------------------------------------------------------

void MqttClient::ResetCredentials()
{
	// An empty object rather than an empty entry: NVS has no zero-length blob,
	// and a stored `{}` reads back as "no token", which is what this means.
	static constexpr char  EMPTY[] = "{}";
	const common::SpanBLOB blob(reinterpret_cast<const uint8_t*>(EMPTY), sizeof(EMPTY) - 1);
	if(GetSettings().WriteBlob(CREDENTIALS_KEY, blob))
		APP_LOGI(TAG, "credentials forgotten");
}

// --------------------------------------------------------------------------

std::string MqttClient::GetDeviceName() const
{
	// What the server files this unit under. It carries the build it came from
	// -- "EspP4-esp32p4 940804" off the panel, "EspP4-sim 940804" off a
	// desktop -- so a simulator does not claim the board's own device and then
	// answer its remote calls.
	char sz[64];
	std::snprintf(sz, sizeof(sz), "EspP4-%s %u", platform::Name(), static_cast<unsigned>(Model::DEMO_SERIAL));
	return sz;
}

// --------------------------------------------------------------------------

uint32_t MqttClient::GetRxCount() const
{
	return m_pPort != nullptr ? m_pPort->GetRxCount() : 0;
}

uint32_t MqttClient::GetTxCount() const
{
	return m_pPort != nullptr ? m_pPort->GetTxCount() : 0;
}

// --------------------------------------------------------------------------

MqttClient& GetMqttClient()
{
	const bool bDebug = std::getenv("ESPP4_MQTT_DEBUG") != nullptr;

	// Asking for debug output has to turn debug output on. The logger sits at
	// Info on both builds, so every APP_LOGD below -- the topics that arrive,
	// the methods we do not answer, a publish that found no link -- would
	// otherwise be written and dropped, and the switch would look broken.
	if(bDebug && platform::GetLogLevel() < platform::LogLevel::Debug)
		platform::SetLogLevel(platform::LogLevel::Debug);

	static MqttClient client(bDebug);
	return client;
}

} // namespace app
