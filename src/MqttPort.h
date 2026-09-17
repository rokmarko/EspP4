/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// The MQTT link as the application sees it.
//
// `app::MqttClient` is written entirely against this: connect, subscribe,
// publish, and two callbacks that say what arrived. Everything a broker
// connection needs beyond that -- sockets, TLS one day, the reconnect timer --
// is the port's business, exactly as it is for `app::CanPort`.
//
// Two implementations:
//
//   port/esp/MqttPortEsp   -- ESP-IDF's own esp-mqtt component, which brings
//                             its own task and its own reconnect;
//   port/pc/MqttPortPosix  -- MQTT 3.1.1 over a plain socket, QoS 0 only,
//                             with a receive thread of its own. Small enough
//                             to read in one sitting, which is the point: the
//                             simulator talks to a real broker, it does not
//                             pretend to.
//
// Both callbacks are delivered on the port's own thread, so neither may touch
// LVGL or the model. `MqttClient` queues what arrives and does the work on the
// model task -- the same rule the CAN side learned with pushed parameters.

#include <cstddef>
#include <cstdint>
#include <atomic>
#include <functional>
#include <string_view>

namespace app {

class MqttPort
{
public:
	struct Config
	{
		const char* pszHost = "";
		uint16_t		uPort	  = 1883;
		// What the broker knows this unit as. Empty is allowed and is what the
		// provisioning connection uses -- it has no identity yet.
		const char* pszClientId = "";
		// ThingsBoard puts the access token here and wants no password; the
		// provisioning connection sends the literal "provision".
		const char* pszUser		  = "";
		const char* pszPassword	  = "";
		uint16_t		uKeepAliveSec = 60;
	};

	// One whole application message. svTopic and svData are valid for the
	// duration of the call and not a byte longer.
	using FuncMessage = std::function<void(std::string_view svTopic, std::string_view svData)>;
	// True when the broker has accepted a connection, false when it is gone.
	using FuncState = std::function<void(bool bConnected)>;

	virtual ~MqttPort() = default;

	// Open the link and keep it open: a port that loses the broker reconnects
	// on its own and says so through the state callback, which is why the
	// client re-subscribes every time it is told it is connected.
	//
	// Returns false if the configuration could not be used at all.
	virtual bool Start(const Config& cfg) = 0;

	// Close the link and release the thread behind it. Safe to call on a port
	// that never started; never called from the port's own thread.
	virtual void Stop() = 0;

	virtual bool IsConnected() const = 0;

	// QoS 0 for both, which is all this product needs: telemetry that is
	// stale a second later, and RPC replies the caller retries anyway.
	virtual bool Subscribe(const char* pszFilter)							  = 0;
	virtual bool Publish(const char* pszTopic, std::string_view svData) = 0;

	// What the console's `i` line reports.
	uint32_t GetRxCount() const { return m_uRx; }
	uint32_t GetTxCount() const { return m_uTx; }

protected:
	MqttPort(FuncMessage&& fMessage, FuncState&& fState) :
		m_fMessage(std::move(fMessage)),
		m_fState(std::move(fState))
	{}

	FuncMessage m_fMessage;
	FuncState	m_fState;

	// Counted by the implementations, read from the console task.
	std::atomic<uint32_t> m_uRx{0};
	std::atomic<uint32_t> m_uTx{0};
};

// --------------------------------------------------------------------------

// The one MQTT port this build talks through, made by its port.
//
// Owned by the port and alive for the life of the process, like every other
// singleton here. Not started: `MqttClient::Connect()` does that, because only
// the client knows whether it has credentials yet.
//
// Returns nullptr only if the port could not be constructed at all.
MqttPort* CreateMqttPort(MqttPort::FuncMessage&& fMessage, MqttPort::FuncState&& fState);

} // namespace app
