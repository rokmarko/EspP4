/**
 * @file SerialConsole.cpp
 * @brief One-character debug console on USB-Serial/JTAG.
 *
 * Commands (send a single byte, no newline needed):
 *
 *   h   help
 *   i   print one <<<STATS ...>>> line
 *   t   toggle scene, as a screen tap would
 *   w   write the option blobs to NVS, then print <<<SAVE ...>>>
 *   P   push a parameter at ourselves over CAN (self-test only)
 *   s   screenshot at half resolution  (fast, ~360x360)
 *   S   screenshot at full resolution  (720x720, several seconds)
 *
 * A screenshot is framed so a host parser can find it in the log stream:
 *
 *   <<<SHOT w=360 h=360 fmt=rgb888>>>
 *   <base64, 76 chars per line>
 *   <<<ENDSHOT>>>
 */

#include "SerialConsole.h"

#ifndef DEMO_NO_SERIAL_CONSOLE

#include "AppModel.h"
#include "AppOptions.h"
#include "CanPortEsp.h"
#include "CanProcessor.h"
#include "StorageOptions.h"
#include "VectorScene.h"

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr const char *TAG = "console";

constexpr int  CONSOLE_TASK_STACK = 32768;  /* LVGL draw calls happen on this task */
constexpr int  USB_RX_BUF   = 1024;
constexpr int  USB_TX_BUF   = 4096;
constexpr int  B64_LINE_LEN = 76;   /* must stay a multiple of 4 */

void WriteAll(const uint8_t *data, size_t len)
{
    while (len > 0) {
        const int n = usb_serial_jtag_write_bytes(data, len, pdMS_TO_TICKS(2000));
        if (n <= 0) return;
        data += n;
        len  -= static_cast<size_t>(n);
    }
}

void WriteStr(const char *s)
{
    WriteAll(reinterpret_cast<const uint8_t *>(s), std::strlen(s));
}

/**
 * Streaming base64 encoder. Rows arrive in arbitrary sizes, so leftover
 * bytes carry across calls; Finish() flushes the tail with padding.
 */
class Base64Stream {
public:
    void Feed(const uint8_t *data, size_t len)
    {
        for (size_t i = 0; i < len; i++) {
            m_group[m_nGroup++] = data[i];
            if (m_nGroup == 3) {
                EmitGroup(3);
                m_nGroup = 0;
            }
        }
    }

    void Finish()
    {
        if (m_nGroup > 0) {
            const int pad = 3 - m_nGroup;
            for (int i = m_nGroup; i < 3; i++) m_group[i] = 0;
            EmitGroup(3 - pad);
            m_nGroup = 0;
        }
        if (m_nLine > 0) PutRaw("\n", 1);
        Flush();
    }

private:
    static constexpr const char *ALPHABET =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

    void EmitGroup(int valid)
    {
        char out[4];
        const uint32_t v = (static_cast<uint32_t>(m_group[0]) << 16) |
                           (static_cast<uint32_t>(m_group[1]) << 8) |
                           static_cast<uint32_t>(m_group[2]);
        out[0] = ALPHABET[(v >> 18) & 0x3F];
        out[1] = ALPHABET[(v >> 12) & 0x3F];
        out[2] = valid > 1 ? ALPHABET[(v >> 6) & 0x3F] : '=';
        out[3] = valid > 2 ? ALPHABET[v & 0x3F] : '=';
        PutRaw(out, 4);

        m_nLine += 4;
        if (m_nLine >= B64_LINE_LEN) {
            PutRaw("\n", 1);
            m_nLine = 0;
        }
    }

    void PutRaw(const char *s, size_t n)
    {
        if (m_nBuf + n > sizeof(m_buf)) Flush();
        std::memcpy(m_buf + m_nBuf, s, n);
        m_nBuf += n;
    }

    void Flush()
    {
        if (m_nBuf == 0) return;
        WriteAll(m_buf, m_nBuf);
        m_nBuf = 0;
    }

    uint8_t m_group[3]  = {};
    int     m_nGroup    = 0;
    int     m_nLine     = 0;
    uint8_t m_buf[1024] = {};
    size_t  m_nBuf      = 0;
};

void SinkToBase64(const uint8_t *data, size_t len, void *ctx)
{
    static_cast<Base64Stream *>(ctx)->Feed(data, len);
}

void PrintStats()
{
    char line[448];
    const int tenths = demo::FrameTimeTenths();

    /* Model fields prove the processing loop is actually ticking: rpm comes
     * from the NOD, eng/moving from ModelBase's above/below detectors. */
    const app::Model     *pModel = app::GetModel();
    const app::CanPortEsp  *pPort = app::GetCanPort();
    const app::CanProcessor *pProc = app::GetCanProcessor();
    const int  iRpm    = pModel ? static_cast<int>(pModel->GetEngineRPM() + 0.5f) : -1;
    const int  iEng    = pModel ? (pModel->IsEngineRunning() ? 1 : 0) : -1;
    const int  iMoving = pModel ? (pModel->IsMoving() ? 1 : 0) : -1;

    /* nvs_opt is how many option blobs came back at boot; a fresh flash reads
     * 0 and writes the defaults, every boot after that reads them all. */
    const app::Settings::Usage use = app::GetSettings().GetUsage();

    std::snprintf(line, sizeof(line),
                  "<<<STATS scene=%s frame_ms=%d.%d heap_int=%u heap_psram=%u "
                  "rpm=%d eng=%d moving=%d model_stack=%u "
                  "can=%s can_rx=%u can_tx=%u can_nod=%u can_alive=%d can_ident=%d can_err=%u can_state=%u "
                  "nvs=%s nvs_opt=%u nvs_used=%u/%u heap_int_min=%u can_push=%u>>>\n",
                  demo::SceneName(), tenths / 10, tenths % 10,
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)),
                  iRpm, iEng, iMoving,
                  static_cast<unsigned>(app::ModelStackHeadroom()),
                  pPort ? app::CanPortEsp::ModeName(pPort->GetMode()) : "off",
                  static_cast<unsigned>(pPort ? pPort->GetRxCount() : 0),
                  static_cast<unsigned>(pPort ? pPort->GetTxCount() : 0),
                  static_cast<unsigned>(pProc ? pProc->GetNodCount() : 0),
                  pProc ? pProc->GetAliveUnitCount() : -1,
                  pProc ? pProc->GetIdentifiedUnitCount() : -1,
                  static_cast<unsigned>(pPort ? pPort->GetErrCount() : 0),
                  static_cast<unsigned>(pPort ? pPort->GetBusState() : 0),
                  use.uTotal ? "open" : "off",
                  static_cast<unsigned>(app::OptionsLoaded()),
                  static_cast<unsigned>(use.uUsed),
                  static_cast<unsigned>(use.uTotal),
                  /* Low-water mark since boot. heap_int is a snapshot taken at
                   * a random point in a ThorVG frame and swings by tens of kB;
                   * this is the figure that says whether the margin is real. */
                  static_cast<unsigned>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                  static_cast<unsigned>(app::ParameterPushCount()));
    WriteStr(line);
}

void SaveSettings()
{
    app::Model    *pModel   = app::GetModel();
    app::Settings &settings = app::GetSettings();

    if (pModel == nullptr || settings.IsOpen() == false) {
        WriteStr("<<<SAVE ok=0 written=0>>>\n");
        return;
    }

    /* The whole set, not just what is dirty: nothing on this board changes an
     * option by itself, so a dirty-only save would write nothing and prove
     * nothing. Reboot afterwards and check nvs_opt in <<<STATS>>>. */
    const uint32_t uWritten = settings.Save(pModel->GetOptions(), false);
    const app::Settings::Usage use = settings.GetUsage();

    char line[128];
    std::snprintf(line, sizeof(line), "<<<SAVE ok=%d written=%u used=%u/%u>>>\n",
                  uWritten > 0 ? 1 : 0, static_cast<unsigned>(uWritten),
                  static_cast<unsigned>(use.uUsed),
                  static_cast<unsigned>(use.uTotal));
    WriteStr(line);
}

void PushParameter()
{
    app::Model *pModel = app::GetModel();
    if (pModel == nullptr) {
        WriteStr("<<<PUSH ok=0 pushes=0>>>\n");
        return;
    }

    const bool bSent = pModel->SimulateParameterPush();

    /* The transfer is asynchronous now: DDS_A posts a burst per 50 ms tick,
     * then the commit, then the apply on the LVGL task. Wait for the services
     * to go idle rather than guessing at a delay. */
    for (int i = 0; i < 60 && app::IsPushActive(); ++i)
        vTaskDelay(pdMS_TO_TICKS(50));
    vTaskDelay(pdMS_TO_TICKS(200));   /* and one more scene tick for the apply */

    char line[96];
    std::snprintf(line, sizeof(line), "<<<PUSH ok=%d pushes=%u>>>\n",
                  bSent ? 1 : 0,
                  static_cast<unsigned>(app::ParameterPushCount()));
    WriteStr(line);
}

void Screenshot(int step)
{
    Base64Stream b64;
    int32_t w = 0;
    int32_t h = 0;

    /* ESP_LOG writes go to the same USB endpoint as the base64 body. Any log
     * line emitted mid-capture -- by us, by LVGL, by the adapter -- lands in
     * the middle of a base64 line and corrupts the image beyond recovery on
     * the host. Silence logging for the duration; the frame markers below are
     * written directly, so they are unaffected. */
    esp_log_level_set("*", ESP_LOG_NONE);

    char header[96];
    std::snprintf(header, sizeof(header), "<<<SHOT step=%d fmt=rgb888>>>\n", step);
    WriteStr(header);

    const bool ok = demo::CaptureScreenshot(step, SinkToBase64, &b64, &w, &h);
    b64.Finish();

    char footer[96];
    std::snprintf(footer, sizeof(footer), "<<<ENDSHOT ok=%d w=%d h=%d>>>\n",
                  ok ? 1 : 0, static_cast<int>(w), static_cast<int>(h));
    WriteStr(footer);

    esp_log_level_set("*", ESP_LOG_INFO);
}

void Help()
{
    WriteStr("<<<HELP h=help i=stats t=toggle w=save-settings P=push-param s=shot-half S=shot-full>>>\n");
}

void ConsoleTask(void *)
{
    WriteStr("<<<CONSOLE ready>>>\n");
    for (;;) {
        uint8_t c = 0;
        const int n = usb_serial_jtag_read_bytes(&c, 1, portMAX_DELAY);
        if (n != 1) continue;
        switch (c) {
            case 'h': Help();               break;
            case 'i': PrintStats();         break;
            case 't': demo::ToggleScene();
                      PrintStats();         break;
            case 'w': SaveSettings();       break;
            case 'P': PushParameter();      break;
            case 's': Screenshot(2);        break;
            case 'S': Screenshot(1);        break;
            case '\r':
            case '\n':                      break;
            default:                        break;
        }
    }
}

} // namespace

namespace demo {

void StartSerialConsole()
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    cfg.rx_buffer_size = USB_RX_BUF;
    cfg.tx_buffer_size = USB_TX_BUF;

    const esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "usb_serial_jtag_driver_install failed: 0x%x", err);
        return;
    }
    /* Route printf()/ESP_LOG through the driver so log output and console
     * output cannot interleave mid-byte. */
    usb_serial_jtag_vfs_use_driver();

    /* Checked, because a silent failure here looks exactly like a dead board:
     * no <<<CONSOLE ready>>>, no error, and the driver on the host reports
     * only that it got nothing back. The stack is large because
     * lv_snapshot_take() draws on the calling thread, and 32 kB has to be
     * *contiguous* -- if this ever fails, something mounted or allocated ahead
     * of it fragmented internal RAM. */
    if (xTaskCreate(ConsoleTask, "console", CONSOLE_TASK_STACK,
                    nullptr, 4, nullptr) != pdPASS) {
        ESP_LOGE(TAG, "could not create the console task (%u B stack); "
                      "largest free internal block is %u B",
                 static_cast<unsigned>(CONSOLE_TASK_STACK),
                 static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)));
    }
}

} // namespace demo

#else  /* DEMO_NO_SERIAL_CONSOLE */

namespace demo {
void StartSerialConsole() {}
} // namespace demo

#endif
