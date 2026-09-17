/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// app::MqttPort on the Eclipse Paho C++ client.
//
// The board reaches a broker through ESP-IDF's esp-mqtt; this is the desktop's
// equivalent, and it is deliberately the same shape -- a thin adapter over
// somebody else's client, turning its callbacks into the two this product
// asks for. Nothing here speaks MQTT itself. That matters more than the lines
// it saves: a protocol written twice is a product that behaves two ways, and
// the whole point of the simulator is that it does not.
//
// Paho is an apt package away (`sudo apt install libpaho-mqttpp-dev`) and
// port/pc/CMakeLists.txt says so when it is missing.
//
// Two things about it are worth knowing:
//
//   - **Its automatic reconnect is switched off**, and the retry below does
//     that job instead. Paho's own only starts after one successful
//     connection, so a simulator started before its broker would sit there
//     forever; one loop that dials whenever it is not connected covers both
//     that case and a link that drops later.
//   - **Everything it hands back arrives on its callback thread**, which is
//     why the handlers here do nothing but count and forward. app::MqttClient
//     queues the bytes and does the work on the model task.

#include "MqttPort.h"
#include "Platform.h"

#include "mqtt/async_client.h"

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace app {

namespace {

	constexpr const char* TAG = "mqtt";

// How long a CONNECT may take to be answered, and how long the loop waits
// before dialling again. The wait is a condition variable rather than a sleep
// so that Stop() does not have to sit through it.
	constexpr auto CONNECT_WAIT	 = std::chrono::seconds(4);
	constexpr auto RETRY_WAIT		 = std::chrono::seconds(5);
	constexpr auto DISCONNECT_WAIT = std::chrono::seconds(1);

	class MqttPortPaho final : public MqttPort
	{
	public:
		MqttPortPaho(FuncMessage&& fMessage, FuncState&& fState) :
			MqttPort(std::move(fMessage), std::move(fState))
		{}

		~MqttPortPaho() override { Stop(); }

		bool Start(const Config& cfg) override
		{
			Stop();

			if(cfg.pszHost == nullptr || *cfg.pszHost == '\0')
				return false;

			// Paho wants the broker as a URI. Plain TCP for now: the Kanardia
			// broker listens on 1883, and the library is linked against the
			// SSL build, so "ssl://" is the whole change when it stops.
			m_ssUri = "tcp://" + std::string(cfg.pszHost) + ":" + std::to_string(cfg.uPort);

			const std::string ssClientId = cfg.pszClientId != nullptr ? cfg.pszClientId : "";

			try {
				// Null persistence: this client publishes at QoS 0 on a clean
				// session, so there is nothing worth a file in the working
				// directory, which is what paho would otherwise make.
				m_client = std::make_unique<mqtt::async_client>(m_ssUri, ssClientId, nullptr);
			} catch(const mqtt::exception& e) {
				APP_LOGE(TAG, "cannot create a client for %s: %s", m_ssUri.c_str(), e.what());
				return false;
			}

			m_client->set_connected_handler([this](const std::string&) { OnUp(); });
			m_client->set_connection_lost_handler([this](const std::string& ssCause) {
				APP_LOGW(TAG, "connection lost%s%s", ssCause.empty() ? "" : ": ", ssCause.c_str());
				OnDown();
			});
			m_client->set_message_callback([this](mqtt::const_message_ptr pMsg) {
				if(pMsg == nullptr)
					return;
				m_uRx++;
				if(m_fMessage)
					m_fMessage(pMsg->get_topic(), pMsg->get_payload_str());
			});

			m_connOpts = mqtt::connect_options();
			m_connOpts.set_clean_session(true);
			m_connOpts.set_keep_alive_interval(static_cast<int>(cfg.uKeepAliveSec));
			m_connOpts.set_connect_timeout(CONNECT_WAIT);
			m_connOpts.set_automatic_reconnect(false); // Loop() does it -- see the file header
			if(cfg.pszUser != nullptr && *cfg.pszUser != '\0')
				m_connOpts.set_user_name(cfg.pszUser);
			if(cfg.pszPassword != nullptr && *cfg.pszPassword != '\0')
				m_connOpts.set_password(cfg.pszPassword);

			m_thread = std::jthread([this](std::stop_token st) { Loop(st); });
			return true;
		}

		void Stop() override
		{
			// The stop token first: the loop may be inside a connect that has
			// to finish before the client can be taken apart.
			if(m_thread.joinable()) {
				m_thread.request_stop();
				m_cvWait.notify_all();
				m_thread.join();
			}

			if(m_client != nullptr) {
				try {
					if(m_client->is_connected())
						m_client->disconnect()->wait_for(DISCONNECT_WAIT);
				} catch(const mqtt::exception& e) {
					APP_LOGD(TAG, "disconnect: %s", e.what());
				}

				// The handlers captured `this` and paho calls them from a
				// thread of its own; the client owns that thread and shuts it
				// down here, before anything they touch goes away.
				m_client.reset();
			}

			// Deliberately no state callback: this is the client taking the
			// port down on purpose, not the broker going away.
			m_bConnected = false;
		}

		bool IsConnected() const override { return m_bConnected; }

		bool Subscribe(const char* pszFilter) override
		{
			if(m_client == nullptr || pszFilter == nullptr)
				return false;

			// Not waited on: the answer would only tell us the granted QoS,
			// and the client re-subscribes on every connect anyway.
			try {
				m_client->subscribe(pszFilter, 0);
				return true;
			} catch(const mqtt::exception& e) {
				APP_LOGW(TAG, "subscribe '%s': %s", pszFilter, e.what());
				return false;
			}
		}

		bool Publish(const char* pszTopic, std::string_view svData) override
		{
			if(m_client == nullptr || pszTopic == nullptr)
				return false;

			// QoS 0, not retained, and not waited on. Publishing while the
			// link is down throws rather than queueing, which is the answer
			// this port wants: telemetry that did not go out is stale by the
			// time it could.
			try {
				m_client->publish(pszTopic, svData.data(), svData.size(), 0, false);
				m_uTx++;
				return true;
			} catch(const mqtt::exception& e) {
				APP_LOGD(TAG, "publish '%s': %s", pszTopic, e.what());
				return false;
			}
		}

	private:
		// Dial whenever there is no link, and wait between attempts. This is
		// the only place that connects, which is why paho's own reconnect is
		// off: two of them would race over the same client.
		void Loop(std::stop_token st)
		{
			while(st.stop_requested() == false) {
				if(m_client->is_connected() == false) {
					try {
						m_client->connect(m_connOpts)->wait_for(CONNECT_WAIT);
					} catch(const mqtt::exception& e) {
						// A broker that is not there yet is the normal state
						// on a desk, so this is a warning and not an error.
						APP_LOGW(TAG, "%s: %s", m_ssUri.c_str(), e.what());
					}
				}

				std::unique_lock<std::mutex> lock(m_mxWait);
				m_cvWait.wait_for(lock, RETRY_WAIT, [&st] { return st.stop_requested(); });
			}
		}

		// Both edges are reported once: paho calls the connected handler on
		// every successful connect, including the ones this loop makes, and
		// the client re-subscribes each time it is told.
		void OnUp()
		{
			APP_LOGI(TAG, "connected to %s", m_ssUri.c_str());
			if(m_bConnected.exchange(true) == false && m_fState)
				m_fState(true);
		}

		void OnDown()
		{
			if(m_bConnected.exchange(false) && m_fState)
				m_fState(false);
		}

	private:
		std::unique_ptr<mqtt::async_client> m_client;
		mqtt::connect_options					m_connOpts;
		std::string									m_ssUri;

		std::atomic<bool>			m_bConnected{false};
		std::jthread				m_thread;
		std::mutex					m_mxWait;
		std::condition_variable m_cvWait;
	};

} // namespace

// --------------------------------------------------------------------------

MqttPort* CreateMqttPort(MqttPort::FuncMessage&& fMessage, MqttPort::FuncState&& fState)
{
	static MqttPortPaho port(std::move(fMessage), std::move(fState));
	return &port;
}

} // namespace app
