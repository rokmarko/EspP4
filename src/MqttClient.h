/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// This unit on the Kanardia cloud, over MQTT.
//
// Same shape as `core::cloud::CloudClient` in Nesis, which is the client every
// other Kanardia product talks to the server with: a device that has no
// credentials yet connects as "provision", claims itself with the product's
// own provisioning key and secret, stores the access token it gets back and
// reconnects with it. From then on it publishes telemetry and answers remote
// procedure calls on `v1/devices/me/rpc/request/+`.
//
// The provisioning key and secret are this product's own and deliberately not
// Nesis's -- see PROVISION_KEY in MqttClient.cpp. They decide which device
// profile the server files this unit under, and a board that claimed itself
// with Nesis's pair would land among the Nesis units.
//
// Only two calls are acted on, and the rest are logged and dropped:
//
//   sendMessage  -- text to put in front of the pilot. Handed to whoever
//                   called SetMessageHandler(); the scene shows it.
//   sendLayout   -- an instrument layout as XML. Kept, so it can be looked at
//                   from the console; nothing here renders one yet.
//
// Threads. The port delivers what arrives on a thread of its own, and this
// class does nothing there but copy the bytes into a queue. Everything else --
// parsing, answering, provisioning, reconnecting -- happens on the model task,
// in Pump() and Update1s(). That is the same rule pushed parameters taught the
// CAN side: the thread that receives is never the thread that acts.

#include "KanardiaCommon.h"

#include <atomic>
#include <cstdint>
#include <deque>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>

namespace app {

class MqttPort;

// What the server knows this device as, once it has been claimed.
struct CloudCredentials
{
	std::string ssId;
	std::string ssToken;
};

// --------------------------------------------------------------------------

class MqttClient
{
public:
	enum class State
	{
		Off,	  // never connected, or told to stop
		Connecting, // socket is up to the port; no broker yet
		Provisioning, // connected as "provision", waiting for a token
		Online	 // connected with our own token, subscribed
	};

	// What sendMessage asks us to show. Mirrors Nesis's showMessage: the
	// server picks the shape, the product decides what it looks like.
	enum class MessageType
	{
		Info,
		Caution,
		Warning
	};

	struct Message
	{
		std::string ssText;
		MessageType eType		  = MessageType::Info;
		int			iTimeoutMs = 5000;
	};

	// What sendLayout pushed at us, the last one wins.
	struct Layout
	{
		std::string ssTitle;
		std::string ssXml;
	};

	explicit MqttClient(bool bDebug = false);
	MqttClient(const MqttClient&)				  = delete;
	MqttClient& operator=(const MqttClient&) = delete;
	~MqttClient();

	// Whether there is a broker to talk to at all.
	//
	// Off unless ESPP4_MQTT_HOST names one: there is no network stack on the
	// board yet and no broker on a desk, so a client that dialled out on every
	// boot would do nothing but fill the log. The console's `c` command turns
	// it on against the compiled-in default host.
	static bool IsConfigured();

	// Open the link, provisioning first if this unit has no token yet.
	//
	// pszHost:  null takes the configured host -- ESPP4_MQTT_HOST, or the
	//           compiled-in default.
	// Returns false if there is no port, or no host to dial.
	bool Connect(const char* pszHost = nullptr);
	void Disconnect();

	// Connected, subscribed and past provisioning.
	bool IsConnected() const;

	State			GetState() const { return m_eState; }
	const char* GetStateName() const;

	// The beat, both on the model task. Pump() drains what the port received
	// and is called on the 50 ms tick, because an RPC answered a second late
	// looks broken from the other end; Update1s() carries the telemetry and
	// the provisioning hand-over.
	void Pump();
	void Update1s();

	// Forget the stored token. The next Connect() provisions again.
	void ResetCredentials();

	// Take the message sendMessage left, if any. Call from the thread that
	// shows it -- the LVGL task, out of the scene's tick.
	std::optional<Message> TakeMessage();

	// The last layout the server pushed. Empty until one arrives.
	Layout GetLayout() const;

	// --- What the console's `i` line reports ---------------------------

	uint32_t GetRxCount() const;
	uint32_t GetTxCount() const;
	// Remote calls acted on, and those that named a method we do not answer.
	uint32_t GetRpcCount() const { return m_uRpc; }
	uint32_t GetRpcIgnoredCount() const { return m_uRpcIgnored; }

private:
	// --- On the port's thread -------------------------------------------

	// Copy and queue; the work happens in Pump().
	void OnMessage(std::string_view svTopic, std::string_view svData);
	void OnState(bool bConnected);

	// --- On the model task ----------------------------------------------

	// Subscribe to what this connection is for: the provisioning answer, or
	// the remote calls.
	void OnConnected();
	void OnDisconnected();

	void HandleProvision(std::string_view svData);
	void HandleRpcRequest(std::string_view svTopic, std::string_view svData);

	void SendTelemetry();

	// Start the port with what we have: the token if there is one, the
	// provisioning identity if there is not.
	bool Open();

	std::optional<CloudCredentials> ReadCredentials() const;
	void									  WriteCredentials(const std::string& ssToken);

	// "EspP4-esp32p4 940804", or "EspP4-sim 940804" on a desktop -- what the
	// server files the device under, and what it has to keep answering to.
	std::string GetDeviceName() const;

private:
	struct Incoming
	{
		std::string ssTopic;
		std::string ssData;
	};

	MqttPort* m_pPort	 = nullptr;
	bool		 m_bDebug = false;

	std::string m_ssHost;
	uint16_t		m_uPort = 1883;

	State m_eState = State::Off;
	// Whether the open connection is the one that claims this device, rather
	// than the one that works.
	bool m_bProvision = true;
	// Set by the port's thread, acted on by Pump().
	std::atomic<bool> m_bConnected{false};
	std::atomic<bool> m_bStateChanged{false};

	// What the port received, waiting for the model task. Bounded: a broker
	// that floods us must not grow the heap without limit, and the oldest
	// message is the one worth losing.
	mutable std::mutex	m_mutex;
	std::deque<Incoming> m_queue;
	uint32_t					m_uDropped = 0;

	// What the two calls we answer left behind.
	std::optional<Message> m_message;
	Layout					  m_layout;

	uint32_t m_uRpc		  = 0;
	uint32_t m_uRpcIgnored = 0;

	// Telemetry goes out every tenth second-tick, as Nesis's does.
	uint32_t m_uTelemetryTick = 0;
	// Seconds until the next attempt after a provisioning round trip.
	uint32_t m_uReopenIn = 0;
};

// --------------------------------------------------------------------------

// The one cloud client.
MqttClient& GetMqttClient();

} // namespace app
