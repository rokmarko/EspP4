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

// can::AbstractCanPort on the ESP32-P4's TWAI controller.
//
// TWAI is Espressif's name for the CAN 2.0B peripheral. The controller is on
// the chip, but a real bus needs an external transceiver wired to the TX/RX
// pins -- the Waveshare 4C board does not carry one, so on a bare board the
// pins go nowhere.
//
// That is what Mode::SelfTest is for: the controller accepts its own
// transmissions and requires no acknowledge from another node, which exercises
// the whole path -- frame out, frame in, CANaerospace decode, NOD store --
// without a transceiver or a second node. It is how this is tested here.
//
// Frame layout follows Common/CanPort/SocketCan.cpp exactly, so both ports put
// the same bytes on the wire: 29-bit identifier, register A in data[0..3],
// register B in data[4..7].

#include "KanardiaCommon.h"

#include "CanPort.h"

#include "driver/gpio.h"
#include "driver/twai.h"

namespace app {

class CanPortEsp : public CanPort
{
public:
	// The pins the transceiver is wired to. Not in CanPort::Config, which is
	// what both builds share and a desktop has no use for.
	static constexpr gpio_num_t PIN_TX = GPIO_NUM_30;
	static constexpr gpio_num_t PIN_RX = GPIO_NUM_31;

	CanPortEsp(FuncProcessMsg&& fProcMsg, const Config& cfg);
	~CanPortEsp() override;

	// --- app::CanPort ----------------------------------------------------

	// Install and start the driver, then the receive thread.
	bool Start() override;
	// Stop the receive thread and uninstall the driver.
	void Stop() override;

	bool Send(const can::Message& msg) override;
	bool IsSpaceFor(uint32_t uMessages) const override;

	Mode GetMode() const override { return m_cfg.eMode; }

	// Bus-off, error-passive and friends, straight from the controller.
	uint32_t GetBusState() const override;

	const Config& GetConfig() const { return m_cfg; }

protected:
	void Loop(std::stop_token st) override;

private:
	static can::Message	 FromTwai(const twai_message_t& tm);
	static twai_message_t ToTwai(const can::Message& msg);

	Config	  m_cfg;
	gpio_num_t m_eTx		 = PIN_TX;
	gpio_num_t m_eRx		 = PIN_RX;
	bool		  m_bRunning = false;
};

} // namespace app
