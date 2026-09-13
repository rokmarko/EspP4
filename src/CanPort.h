/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2019 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *   Writen by:                                                            *
 *      Rok Markovic [rok.markovic@kanardia.eu]                            *
 *                                                                         *
 *   Status: Open Source                                                   *
 *                                                                         *
 *   License: GPL - GNU General Public License                             *
 *                                                                         *
 ***************************************************************************/

#pragma once

// The CAN port as the application sees it.
//
// `can::AbstractCanPort` is Common's own interface and is all `CanProcessor`
// needs: send a frame, say whether there is room for more. What the rest of
// this product wants on top of that is the same in both builds and in neither
// of Common's ports -- start and stop the hardware, say which mode it is in,
// and count what went past for the console's `i` line. That is this class.
//
// Two implementations:
//
//   port/esp/CanPortEsp -- the ESP32-P4's own TWAI controller;
//   port/pc/CanPortCanu -- `can::CanuCan`, the Kanardia CANU adapter on a
//                          serial port, which is what every desktop Kanardia
//                          program talks to a bus through.
//
// Mode::SelfTest means the same thing on both and is what makes a build
// without a bus useful: frames sent come straight back through the whole
// decode path. The board does it in the controller (NO_ACK plus RX mapped onto
// the TX pin); the simulator does it in software, because there is nothing to
// loop back through when no adapter is plugged in.

#include "KanardiaCommon.h"

#include "CanPort/AbstractCanPort.h"

#include <atomic>
#include <cstdint>

namespace app {

class CanPort : public can::AbstractCanPort
{
public:
	enum class Mode
	{
		Normal,	  // a real bus with at least one other node on it
		Listen,	  // receive only, never acknowledge, never transmit
		SelfTest,	 // no bus: own frames come back through the decode path
	};

	struct Config
	{
		Mode eMode = Mode::Normal;
		// Where the bus is. The board ignores it -- the TWAI pins are fixed in
		// the port -- and the simulator reads it as the CANU adapter's serial
		// device, "/dev/ttyUSB0" and the like. Null means the port's default.
		const char* pszDevice = nullptr;
		// Ignored by the simulator: a CANU adapter is configured on its own
		// side and the link to it runs at a fixed rate.
		uint32_t uBitrateKbps = 500;
	};

	// Bring the hardware up, then the receive thread.
	virtual bool Start() = 0;
	// Stop the receive thread and release the hardware.
	virtual void Stop() = 0;

	virtual Mode GetMode() const = 0;

	// Bus-off, error-passive and friends, as the controller reports them.
	// Zero where there is no controller to ask.
	virtual uint32_t GetBusState() const { return 0; }

	uint32_t GetRxCount() const { return m_uRx; }
	uint32_t GetTxCount() const { return m_uTx; }
	uint32_t GetErrCount() const { return m_uErr; }

	static const char* ModeName(Mode eMode)
	{
		switch(eMode) {
		case Mode::Normal:	return "normal";
		case Mode::Listen:	return "listen";
		case Mode::SelfTest: return "self-test";
		}
		return "?";
	}

protected:
	explicit CanPort(FuncProcessMsg&& fProcMsg) :
		can::AbstractCanPort(std::move(fProcMsg))
	{}

	// Counted by the implementations, read from the console task.
	std::atomic<uint32_t> m_uRx{0};
	std::atomic<uint32_t> m_uTx{0};
	std::atomic<uint32_t> m_uErr{0};
};

// --------------------------------------------------------------------------

// The one CAN port this build talks through, made by its port.
//
// Owned by the port and alive for the life of the process, the way every other
// singleton here is. Not started: `StartModelLoop()` does that, because the
// order in which the big allocations happen matters on the board.
//
// Returns nullptr only if the port could not be constructed at all.
CanPort* CreateCanPort(can::AbstractCanPort::FuncProcessMsg&& fProcMsg, const CanPort::Config& cfg);

} // namespace app
