/**
 * @file Main.cpp
 * @brief LVGL 9 + ThorVG demo for the Waveshare ESP32-P4-WIFI6-Touch-LCD-4C.
 *
 * The board is a 4", 720x720 round IPS panel on 2-lane MIPI-DSI (JD9365) with a
 * GT911 capacitive touch controller. All of that is handled by the Waveshare
 * BSP component; this file only starts it and hands over to the scene.
 */

#include "KanardiaCommon.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"

#include "esp_heap_caps.h"
#include "esp_log.h"

#include "AppModel.h"
#include "SerialConsole.h"
#include "VectorScene.h"

#include "Avio/Format/AvioFormat.h"
#include "Unit/UnitFormatterUtf8.h"

namespace {
constexpr const char *TAG = "main";

/**
 * Hand Common's formatting layer the formatter it works through.
 *
 * `avio::format` keeps one process-wide `unit::Formatter*` and asserts on it;
 * everything below -- ToString(), ToStringFromSystemUnit(), Formatter::
 * FormatAzimuth() -- reaches it from there, so no call site has to carry one.
 * The UTF-8 formatter is the one that maps a unit onto the private-use
 * codepoint the Kanardia font draws for it.
 */
void InstallUnitFormatter()
{
    static const unit::FormatterUtf8 formatter;
    avio::format::SetUnitFormatter(&formatter);
}
} // namespace

extern "C" void app_main(void)
{
    ESP_LOGI(TAG, "panel %dx%d, %d-lane MIPI-DSI",
             BSP_LCD_H_RES, BSP_LCD_V_RES, BSP_LCD_MIPI_DSI_LANE_NUM);

    /* Zero-initialised, then filled in explicitly: ESP_LV_ADAPTER_DEFAULT_CONFIG()
     * leaves the nested auto_sleep.callbacks member out and trips
     * -Wmissing-field-initializers in C++. Everything left at zero (auto sleep
     * off, stack in internal RAM, no touch axis flips) matches its defaults. */
    bsp_display_cfg_t cfg = {};
    /* ThorVG rasterises from the LVGL task; the adapter's 8 kB default is tight. */
    cfg.lv_adapter_cfg.task_stack_size   = 32 * 1024;
    cfg.lv_adapter_cfg.task_priority     = ESP_LV_ADAPTER_DEFAULT_TASK_PRIORITY;
    cfg.lv_adapter_cfg.task_core_id      = ESP_LV_ADAPTER_DEFAULT_TASK_CORE_ID;
    cfg.lv_adapter_cfg.tick_period_ms    = ESP_LV_ADAPTER_DEFAULT_TICK_PERIOD_MS;
    cfg.lv_adapter_cfg.task_min_delay_ms = ESP_LV_ADAPTER_DEFAULT_TASK_MIN_DELAY_MS;
    cfg.lv_adapter_cfg.task_max_delay_ms = ESP_LV_ADAPTER_DEFAULT_TASK_MAX_DELAY_MS;
    cfg.rotation        = ESP_LV_ADAPTER_ROTATE_0;
    cfg.tear_avoid_mode = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL;

    if (bsp_display_start_with_config(&cfg) == nullptr) {
        ESP_LOGE(TAG, "display init failed");
        return;
    }

    /* Before anything formats a value -- the scene builds its labels below. */
    InstallUnitFormatter();

    bsp_display_lock(UINT32_MAX);
    const bool ok = demo::CreateScene();
    bsp_display_unlock();

    if (!ok) {
        ESP_LOGE(TAG, "scene init failed");
        return;
    }

    bsp_display_backlight_on();

    /* The shared Kanardia flight model, ticking on its own task from here on. */
    if (!app::StartModelLoop()) {
        ESP_LOGE(TAG, "model loop failed to start");
    }

    demo::StartSerialConsole();

    ESP_LOGI(TAG, "free heap: %u B internal, %u B PSRAM",
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
             static_cast<unsigned>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
}
