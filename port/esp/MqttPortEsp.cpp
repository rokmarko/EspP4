/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// app::MqttPort on ESP-IDF's esp-mqtt component.
//
// esp-mqtt is what the rest of the ESP world talks to a broker with: it owns a
// task, a socket and a reconnect timer, and hands whole application messages
// back through an event. All this file does is turn its events into the two
// callbacks app::MqttClient expects and copy the configuration in.
//
// Two things about it are worth knowing:
//
//   - a payload larger than the receive buffer arrives in pieces, one
//     MQTT_EVENT_DATA per piece, and only the first carries the topic. A
//     layout pushed from the server is exactly the case that does this, so the
//     pieces are joined here and the client sees one message;
//   - the task is real internal RAM, and internal RAM is the tightest
//     resource on this board. MQTT_TASK_STACK is deliberately modest: nothing
//     in the event handler does more than copy bytes into a queue, because the
//     client does its parsing on the model task.
//
// The network itself is somebody else's job. This board reaches the outside
// world through the ESP32-C6 companion over SDIO, and nothing in this project
// brings that link up yet -- with no route to the broker esp-mqtt simply
// retries, which is the same thing it does when the link drops later.

#include "MqttPort.h"
#include "Platform.h"

#include "mqtt_client.h"

#include <cstring>
#include <string>

namespace app {

namespace {

	constexpr const char* TAG = "mqtt";

// Enough for one remote call with a small layout in it; bigger payloads arrive
// in pieces and are joined below.
	constexpr int MQTT_BUFFER_SIZE = 2048;

// The event handler copies bytes and nothing else -- see the file header.
	constexpr int MQTT_TASK_STACK = 5120;

	class MqttPortEsp final : public MqttPort
	{
	public:
		MqttPortEsp(FuncMessage&& fMessage, FuncState&& fState) :
			MqttPort(std::move(fMessage), std::move(fState))
		{}

		~MqttPortEsp() override { Stop(); }

		bool Start(const Config& cfg) override
		{
			Stop();

			if(cfg.pszHost == nullptr || *cfg.pszHost == '\0')
				return false;

			// Copied, every one of them: esp-mqtt keeps the pointers it is
			// given, and the client builds this from strings it is free to
			// drop as soon as the call returns.
			m_ssHost		 = cfg.pszHost;
			m_ssClientId = cfg.pszClientId != nullptr ? cfg.pszClientId : "";
			m_ssUser		 = cfg.pszUser != nullptr ? cfg.pszUser : "";
			m_ssPassword = cfg.pszPassword != nullptr ? cfg.pszPassword : "";

			esp_mqtt_client_config_t mcfg = {};
			mcfg.broker.address.hostname	= m_ssHost.c_str();
			mcfg.broker.address.port		= cfg.uPort;
			mcfg.broker.address.transport = MQTT_TRANSPORT_OVER_TCP;

			// A device that has not been claimed yet has no identity to give,
			// and esp-mqtt would invent one ("ESP32_xxxxxx") where the broker
			// expects none.
			if(m_ssClientId.empty())
				mcfg.credentials.set_null_client_id = true;
			else
				mcfg.credentials.client_id = m_ssClientId.c_str();

			if(m_ssUser.empty() == false)
				mcfg.credentials.username = m_ssUser.c_str();
			if(m_ssPassword.empty() == false)
				mcfg.credentials.authentication.password = m_ssPassword.c_str();

			mcfg.session.keepalive = cfg.uKeepAliveSec;
			mcfg.buffer.size		  = MQTT_BUFFER_SIZE;
			mcfg.task.stack_size	  = MQTT_TASK_STACK;

			m_hClient = esp_mqtt_client_init(&mcfg);
			if(m_hClient == nullptr) {
				APP_LOGE(TAG, "esp_mqtt_client_init failed");
				return false;
			}

			esp_mqtt_client_register_event(
				m_hClient, static_cast<esp_mqtt_event_id_t>(ESP_EVENT_ANY_ID), &MqttPortEsp::OnEvent, this
			);

			const esp_err_t err = esp_mqtt_client_start(m_hClient);
			if(err != ESP_OK) {
				APP_LOGE(TAG, "esp_mqtt_client_start: %s", esp_err_to_name(err));
				esp_mqtt_client_destroy(m_hClient);
				m_hClient = nullptr;
				return false;
			}

			return true;
		}

		void Stop() override
		{
			if(m_hClient == nullptr)
				return;

			esp_mqtt_client_stop(m_hClient);
			esp_mqtt_client_destroy(m_hClient);
			m_hClient	 = nullptr;
			m_bConnected = false;
			m_ssPartial.clear();
			m_ssTopic.clear();
		}

		bool IsConnected() const override { return m_bConnected; }

		bool Subscribe(const char* pszFilter) override
		{
			if(m_hClient == nullptr || pszFilter == nullptr)
				return false;

			return esp_mqtt_client_subscribe(m_hClient, pszFilter, 0) >= 0;
		}

		bool Publish(const char* pszTopic, std::string_view svData) override
		{
			if(m_hClient == nullptr || pszTopic == nullptr)
				return false;

			// QoS 0: nothing here is worth an outbox entry and a retry on a
			// board with this little internal RAM.
			const int iMsg =
				esp_mqtt_client_publish(m_hClient, pszTopic, svData.data(), static_cast<int>(svData.size()), 0, 0);
			if(iMsg < 0)
				return false;

			m_uTx++;
			return true;
		}

	private:
		static void OnEvent(void* pCtx, esp_event_base_t /*base*/, int32_t iId, void* pData)
		{
			static_cast<MqttPortEsp*>(pCtx)->Handle(iId, static_cast<esp_mqtt_event_handle_t>(pData));
		}

		void Handle(int32_t iId, esp_mqtt_event_handle_t pEvent)
		{
			switch(static_cast<esp_mqtt_event_id_t>(iId)) {
			case MQTT_EVENT_CONNECTED:
				m_bConnected = true;
				if(m_fState)
					m_fState(true);
				break;

			case MQTT_EVENT_DISCONNECTED:
				m_ssPartial.clear();
				if(m_bConnected.exchange(false) && m_fState)
					m_fState(false);
				break;

			case MQTT_EVENT_DATA: HandleData(pEvent); break;

			case MQTT_EVENT_ERROR: APP_LOGW(TAG, "transport error"); break;

			default: break;
			}
		}

		// One MQTT_EVENT_DATA is a piece of a message, not a message: only the
		// first piece carries the topic, and the payload is whole only once
		// current_data_offset + data_len reaches total_data_len.
		void HandleData(esp_mqtt_event_handle_t pEvent)
		{
			if(pEvent->current_data_offset == 0) {
				m_ssTopic.assign(pEvent->topic, static_cast<size_t>(pEvent->topic_len));
				m_ssPartial.clear();
				m_ssPartial.reserve(static_cast<size_t>(pEvent->total_data_len));
			}

			m_ssPartial.append(pEvent->data, static_cast<size_t>(pEvent->data_len));

			if(m_ssPartial.size() < static_cast<size_t>(pEvent->total_data_len))
				return;

			m_uRx++;
			if(m_fMessage)
				m_fMessage(m_ssTopic, m_ssPartial);

			m_ssPartial.clear();
			m_ssPartial.shrink_to_fit();
		}

	private:
		esp_mqtt_client_handle_t m_hClient = nullptr;
		std::atomic<bool>			 m_bConnected{false};

		std::string m_ssHost;
		std::string m_ssClientId;
		std::string m_ssUser;
		std::string m_ssPassword;

		// What a fragmented message is being assembled into.
		std::string m_ssTopic;
		std::string m_ssPartial;
	};

} // namespace

// --------------------------------------------------------------------------

MqttPort* CreateMqttPort(MqttPort::FuncMessage&& fMessage, MqttPort::FuncState&& fState)
{
	static MqttPortEsp port(std::move(fMessage), std::move(fState));
	return &port;
}

} // namespace app
