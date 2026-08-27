#pragma once

/**
 * @file CanPortEsp.h
 * @brief can::AbstractCanPort on the ESP32-P4's TWAI controller.
 *
 * TWAI is Espressif's name for the CAN 2.0B peripheral. The controller is on
 * the chip, but a real bus needs an external transceiver wired to the TX/RX
 * pins -- the Waveshare 4C board does not carry one, so on a bare board the
 * pins go nowhere.
 *
 * That is what Mode::SelfTest is for: the controller accepts its own
 * transmissions and requires no acknowledge from another node, which exercises
 * the whole path -- frame out, frame in, CANaerospace decode, NOD store --
 * without a transceiver or a second node. It is how this is tested here.
 *
 * Frame layout follows Common/CanPort/SocketCan.cpp exactly, so both ports put
 * the same bytes on the wire: 29-bit identifier, register A in data[0..3],
 * register B in data[4..7].
 */

#include "KanardiaCommon.h"

#include "CanPort/AbstractCanPort.h"

#include "driver/gpio.h"
#include "driver/twai.h"

#include <atomic>

namespace app {

class CanPortEsp : public can::AbstractCanPort
{
public:
    enum class Mode {
        Normal,    ///< real bus: needs a transceiver and at least one other node
        Listen,    ///< receive only, never acknowledge, never transmit
        SelfTest,  ///< no acknowledge needed and own frames come back
    };

    struct Config {
        gpio_num_t eTx           = GPIO_NUM_20;
        gpio_num_t eRx           = GPIO_NUM_21;
        /** The Kanardia bus runs at 500 kbit/s -- see SocketCan.cpp. */
        uint32_t   uBitrateKbps  = 500;
        Mode       eMode         = Mode::Normal;
    };

    CanPortEsp(FuncProcessMsg &&fProcMsg, const Config &cfg);
    ~CanPortEsp() override;

    /** Install and start the driver, then the receive thread. */
    bool Start();
    /** Stop the receive thread and uninstall the driver. */
    void Stop();

    bool Send(const can::Message &msg) override;
    bool IsSpaceFor(uint32_t uMessages) const override;

    uint32_t GetRxCount() const  { return m_uRx; }
    uint32_t GetTxCount() const  { return m_uTx; }
    uint32_t GetErrCount() const { return m_uErr; }

    /** Bus-off, error-passive and friends, straight from the controller. */
    uint32_t GetBusState() const;

    Mode GetMode() const { return m_cfg.eMode; }
    const Config &GetConfig() const { return m_cfg; }

    static const char *ModeName(Mode eMode);

protected:
    void Loop(std::stop_token st) override;

private:
    static can::Message FromTwai(const twai_message_t &tm);
    static twai_message_t ToTwai(const can::Message &msg);

    Config                m_cfg;
    bool                  m_bRunning = false;
    std::atomic<uint32_t> m_uRx{0};
    std::atomic<uint32_t> m_uTx{0};
    std::atomic<uint32_t> m_uErr{0};
};

} // namespace app
