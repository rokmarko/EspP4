/**
 * @file CanPortEsp.cpp
 */

#include "CanPortEsp.h"

#include "esp_log.h"

namespace app {

namespace {

constexpr const char *TAG = "can";

/** Receive timeout, so the loop can notice a stop request. */
constexpr TickType_t RX_WAIT = pdMS_TO_TICKS(200);

twai_mode_t ToTwaiMode(CanPortEsp::Mode eMode)
{
    switch (eMode) {
        case CanPortEsp::Mode::Listen:   return TWAI_MODE_LISTEN_ONLY;
        case CanPortEsp::Mode::SelfTest: return TWAI_MODE_NO_ACK;
        case CanPortEsp::Mode::Normal:   break;
    }
    return TWAI_MODE_NORMAL;
}

bool MakeTiming(uint32_t uKbps, twai_timing_config_t &t)
{
    switch (uKbps) {
        case 1000: t = TWAI_TIMING_CONFIG_1MBITS();   return true;
        case  800: t = TWAI_TIMING_CONFIG_800KBITS(); return true;
        case  500: t = TWAI_TIMING_CONFIG_500KBITS(); return true;
        case  250: t = TWAI_TIMING_CONFIG_250KBITS(); return true;
        case  125: t = TWAI_TIMING_CONFIG_125KBITS(); return true;
        case  100: t = TWAI_TIMING_CONFIG_100KBITS(); return true;
        case   50: t = TWAI_TIMING_CONFIG_50KBITS();  return true;
        default: break;
    }
    return false;
}

} // namespace

// --------------------------------------------------------------------------

CanPortEsp::CanPortEsp(FuncProcessMsg &&fProcMsg, const Config &cfg)
    : can::AbstractCanPort(std::move(fProcMsg))
    , m_cfg(cfg)
{
}

// --------------------------------------------------------------------------

CanPortEsp::~CanPortEsp()
{
    Stop();
}

// --------------------------------------------------------------------------

const char *CanPortEsp::ModeName(Mode eMode)
{
    switch (eMode) {
        case Mode::Normal:   return "normal";
        case Mode::Listen:   return "listen";
        case Mode::SelfTest: return "self-test";
    }
    return "?";
}

// --------------------------------------------------------------------------

bool CanPortEsp::Start()
{
    if (m_bRunning) return true;

    twai_timing_config_t timing{};
    if (MakeTiming(m_cfg.uBitrateKbps, timing) == false) {
        ESP_LOGE(TAG, "%u kbit/s is not one of the rates the driver offers",
                 static_cast<unsigned>(m_cfg.uBitrateKbps));
        return false;
    }

    /* Self-test needs the transmission to come back. Two things make that
     * happen without any wiring: NO_ACK mode (nobody is there to acknowledge)
     * and mapping RX to the same pin as TX, so the GPIO matrix feeds the
     * outgoing signal straight back into the receiver. The frames themselves
     * additionally have to be sent as self-reception requests -- see Send(). */
    if (m_cfg.eMode == Mode::SelfTest && m_cfg.eRx != m_cfg.eTx) {
        ESP_LOGI(TAG, "self-test: looping RX back onto GPIO%d", static_cast<int>(m_cfg.eTx));
        m_cfg.eRx = m_cfg.eTx;
    }

    twai_general_config_t general =
        TWAI_GENERAL_CONFIG_DEFAULT(m_cfg.eTx, m_cfg.eRx, ToTwaiMode(m_cfg.eMode));
    /* CANaerospace traffic is bursty; a deeper RX queue costs little. */
    general.rx_queue_len = 64;
    general.tx_queue_len = 16;

    /* Accept everything: filtering is the processor's job, by id range. */
    const twai_filter_config_t filter = TWAI_FILTER_CONFIG_ACCEPT_ALL();

    esp_err_t err = twai_driver_install(&general, &timing, &filter);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_driver_install: %s", esp_err_to_name(err));
        return false;
    }

    err = twai_start();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "twai_start: %s", esp_err_to_name(err));
        twai_driver_uninstall();
        return false;
    }

    m_bRunning = true;
    StartLoopProcess();

    ESP_LOGI(TAG, "TWAI up: tx=GPIO%d rx=GPIO%d %u kbit/s mode=%s",
             static_cast<int>(m_cfg.eTx), static_cast<int>(m_cfg.eRx),
             static_cast<unsigned>(m_cfg.uBitrateKbps), ModeName(m_cfg.eMode));
    return true;
}

// --------------------------------------------------------------------------

void CanPortEsp::Stop()
{
    if (m_bRunning == false) return;
    m_bRunning = false;

    /* jthread's destructor requests the stop and joins; the loop wakes up
     * within RX_WAIT and returns. */
    if (m_thread.joinable()) {
        m_thread.request_stop();
        m_thread.join();
    }

    twai_stop();
    twai_driver_uninstall();
    ESP_LOGI(TAG, "TWAI down");
}

// --------------------------------------------------------------------------

can::Message CanPortEsp::FromTwai(const twai_message_t &tm)
{
    /* Same packing as SocketCan: register A is data[0..3], B is data[4..7]. */
    can::Register rA;
    can::Register rB;
    for (int i = 0; i < 4; i++) {
        rA.by[i] = (i < tm.data_length_code)     ? tm.data[i]     : 0;
        rB.by[i] = (i + 4 < tm.data_length_code) ? tm.data[i + 4] : 0;
    }

    can::Message msg(can::Identifier(tm.identifier), rA, rB);
    msg.SetFrameInfo(tm.data_length_code, tm.rtr != 0, tm.extd != 0);
    return msg;
}

// --------------------------------------------------------------------------

twai_message_t CanPortEsp::ToTwai(const can::Message &msg)
{
    twai_message_t tm{};
    tm.identifier       = msg.GetIdentifier().ui32;
    tm.extd             = 1;   /* Kanardia always uses the 29-bit identifier */
    tm.rtr              = msg.IsRemote() ? 1 : 0;
    tm.data_length_code = msg.GetDataLength();

    for (int i = 0; i < tm.data_length_code && i < 8; i++)
        tm.data[i] = msg.GetByte(static_cast<unsigned>(i));

    return tm;
}

// --------------------------------------------------------------------------

bool CanPortEsp::Send(const can::Message &msg)
{
    if (m_bRunning == false) return false;

    twai_message_t tm = ToTwai(msg);
    /* Ask the controller to hand the frame back to us as well. */
    tm.self = (m_cfg.eMode == Mode::SelfTest) ? 1 : 0;

    const esp_err_t err = twai_transmit(&tm, pdMS_TO_TICKS(10));
    if (err != ESP_OK) {
        m_uErr++;
        return false;
    }
    m_uTx++;
    return true;
}

// --------------------------------------------------------------------------

bool CanPortEsp::IsSpaceFor(uint32_t uMessages) const
{
    twai_status_info_t st{};
    if (twai_get_status_info(&st) != ESP_OK) return false;
    return (st.msgs_to_tx + uMessages) <= 16;   /* matches tx_queue_len */
}

// --------------------------------------------------------------------------

uint32_t CanPortEsp::GetBusState() const
{
    twai_status_info_t st{};
    if (twai_get_status_info(&st) != ESP_OK) return 0;
    return static_cast<uint32_t>(st.state);
}

// --------------------------------------------------------------------------

void CanPortEsp::Loop(std::stop_token st)
{
    ESP_LOGI(TAG, "receive loop running");

    while (st.stop_requested() == false) {
        twai_message_t tm{};
        const esp_err_t err = twai_receive(&tm, RX_WAIT);

        if (err == ESP_ERR_TIMEOUT) continue;

        if (err != ESP_OK) {
            m_uErr++;
            continue;
        }

        m_uRx++;
        OnReceive(FromTwai(tm));
    }

    ESP_LOGI(TAG, "receive loop stopped");
}

} // namespace app
