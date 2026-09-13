/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#pragma once

// app::CanPort on the Kanardia CANU adapter.
//
// `can::CanuCan` is Common's own desktop port -- the framed serial protocol
// the CANU v2 adapter speaks, the same one Nesis and every other Kanardia
// desktop program reaches a bus through. It is `final`, so this is composition
// rather than inheritance: CanuCan does the wire work on its own thread, and
// this class is the façade that gives the application what CanPort promises --
// start, stop, mode and the counters the console reports.
//
// With no adapter plugged in the port falls back to Mode::SelfTest, which is
// what makes the simulator useful on a desk: frames sent come back through the
// full decode path, exactly as the board's controller hands its own
// transmissions back in NO_ACK mode.
//
// The loopback is a queue and a thread, not a direct call, and that is
// load-bearing. `CanProcessor::Pump()` holds the service mutex while
// `OldServices::Update()` posts a message, and delivering that message inline
// would re-enter `Process()` on the same thread and deadlock on the same
// mutex. Going through the receive thread is also what the board does, so the
// two builds have the same thread shape.

#include "KanardiaCommon.h"

#include "CanPort.h"

#include "CanPort/CanuCan.h"

#include <condition_variable>
#include <deque>
#include <memory>
#include <mutex>
#include <string>

namespace app {

class CanPortCanu : public CanPort
{
public:
	// Serial device the adapter shows up as, when the configuration names none.
	static constexpr const char* DEFAULT_DEVICE = "/dev/ttyUSB0";

	CanPortCanu(FuncProcessMsg&& fProcMsg, const Config& cfg);
	~CanPortCanu() override;

	// --- app::CanPort ----------------------------------------------------

	// Open the adapter and start receiving. Falls back to Mode::SelfTest when
	// the device is not there, which is not a failure: the simulator runs
	// perfectly well without a bus.
	bool Start() override;
	void Stop() override;

	bool Send(const can::Message& msg) override;
	bool IsSpaceFor(uint32_t uMessages) const override;

	Mode GetMode() const override { return m_eMode; }

	// The device the adapter was opened on, or "loopback" in self-test.
	const char* GetDeviceName() const;

protected:
	// The loopback delivery thread. Started only in self-test; on a real
	// adapter CanuCan runs its own receive loop and this never opens.
	void Loop(std::stop_token st) override;

private:
	// True when the device exists and can be opened for reading and writing.
	static bool DeviceUsable(const std::string& ssDevice);

	Config		m_cfg;
	Mode			m_eMode = Mode::SelfTest;
	std::string m_ssDevice;
	bool			m_bRunning = false;

	// Null in self-test: there is nothing to talk to.
	std::unique_ptr<can::CanuCan> m_pCanu;

	// Frames waiting to be handed back to ourselves in self-test.
	mutable std::mutex		 m_mxLoop;
	std::condition_variable	 m_cvLoop;
	std::deque<can::Message> m_qLoop;
};

} // namespace app
