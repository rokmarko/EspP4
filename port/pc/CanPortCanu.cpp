/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#include "CanPortCanu.h"

#include "Platform.h"

#include <chrono>
#include <utility>

#include <unistd.h>

namespace app {

namespace {

	constexpr const char* TAG = "can";

// How long the loopback thread waits before looking at the stop token again.
	constexpr auto LOOP_WAIT = std::chrono::milliseconds(200);

// What IsSpaceFor() answers with in self-test. The queue has no hardware limit
// behind it, but DDS_A asks this before every burst and an unbounded yes would
// have it post a whole download in one 50 ms tick -- which is not what the
// board does, and the point of the simulator is to behave like the board.
	constexpr uint32_t LOOPBACK_DEPTH = 16;

} // namespace

// --------------------------------------------------------------------------

CanPortCanu::CanPortCanu(FuncProcessMsg&& fProcMsg, const Config& cfg) :
	CanPort(std::move(fProcMsg)),
	m_cfg(cfg),
	m_eMode(cfg.eMode),
	m_ssDevice(cfg.pszDevice != nullptr ? cfg.pszDevice : DEFAULT_DEVICE)
{}

// --------------------------------------------------------------------------

CanPortCanu::~CanPortCanu()
{
	Stop();
}

// --------------------------------------------------------------------------

bool CanPortCanu::DeviceUsable(const std::string& ssDevice)
{
	return ::access(ssDevice.c_str(), R_OK | W_OK) == 0;
}

// --------------------------------------------------------------------------

const char* CanPortCanu::GetDeviceName() const
{
	return m_pCanu != nullptr ? m_ssDevice.c_str() : "loopback";
}

// --------------------------------------------------------------------------

bool CanPortCanu::Start()
{
	if(m_bRunning)
		return true;

	// Listen-only is a controller mode and the adapter has no equivalent, so
	// it is honoured the only way it can be: open the device, never transmit.
	// Send() checks the mode.
	const bool bWantsBus = m_eMode != Mode::SelfTest;

	if(bWantsBus && DeviceUsable(m_ssDevice) == false) {
		APP_LOGW(TAG, "no CANU adapter on '%s'; falling back to self-test", m_ssDevice.c_str());
		m_eMode = Mode::SelfTest;
	}

	if(m_eMode != Mode::SelfTest) {
		// CanuCan opens the port in its constructor and hands every decoded
		// frame to this callback, on its own receive thread.
		m_pCanu = std::make_unique<can::CanuCan>(m_ssDevice, [this](const can::Message& msg) {
			m_uRx++;
			OnReceive(msg);
		});
		m_pCanu->StartLoopProcess();
	}
	else {
		// Self-test: our own thread delivers what we send back to us.
		StartLoopProcess();
	}

	m_bRunning = true;
	APP_LOGI(TAG, "CAN up: %s mode=%s", GetDeviceName(), ModeName(m_eMode));
	return true;
}

// --------------------------------------------------------------------------

void CanPortCanu::Stop()
{
	if(m_bRunning == false)
		return;
	m_bRunning = false;

	// Whichever thread is running, the jthread's stop token is how it is asked
	// to leave; both loops look at it within their poll timeout.
	if(m_pCanu != nullptr) {
		m_pCanu.reset();
	}
	else if(m_thread.joinable()) {
		m_thread.request_stop();
		m_cvLoop.notify_all();
		m_thread.join();
	}

	APP_LOGI(TAG, "CAN down");
}

// --------------------------------------------------------------------------

bool CanPortCanu::Send(const can::Message& msg)
{
	if(m_bRunning == false || m_eMode == Mode::Listen)
		return false;

	if(m_pCanu != nullptr) {
		if(m_pCanu->Send(msg) == false) {
			m_uErr++;
			return false;
		}
		m_uTx++;
		return true;
	}

	// Self-test. Queue rather than deliver: the caller may be holding a lock
	// that the receive path takes -- see the header.
	{
		std::lock_guard g(m_mxLoop);
		if(m_qLoop.size() >= LOOPBACK_DEPTH) {
			m_uErr++;
			return false;
		}
		m_qLoop.push_back(msg);
	}
	m_cvLoop.notify_one();
	m_uTx++;
	return true;
}

// --------------------------------------------------------------------------

bool CanPortCanu::IsSpaceFor(uint32_t uMessages) const
{
	if(m_pCanu != nullptr)
		return m_pCanu->IsSpaceFor(uMessages);

	std::lock_guard g(m_mxLoop);
	return (m_qLoop.size() + uMessages) <= LOOPBACK_DEPTH;
}

// --------------------------------------------------------------------------

void CanPortCanu::Loop(std::stop_token st)
{
	APP_LOGI(TAG, "loopback receive thread running");

	while(st.stop_requested() == false) {
		can::Message msg;
		{
			std::unique_lock lock(m_mxLoop);
			m_cvLoop.wait_for(lock, LOOP_WAIT, [this, &st] { return m_qLoop.empty() == false || st.stop_requested(); });
			if(m_qLoop.empty())
				continue;

			msg = m_qLoop.front();
			m_qLoop.pop_front();
		}

		m_uRx++;
		OnReceive(msg);
	}

	APP_LOGI(TAG, "loopback receive thread stopped");
}

// --------------------------------------------------------------------------

CanPort* CreateCanPort(can::AbstractCanPort::FuncProcessMsg&& fProcMsg, const CanPort::Config& cfg)
{
	static CanPortCanu port(std::move(fProcMsg), cfg);
	return &port;
}

} // namespace app
