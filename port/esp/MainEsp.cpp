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

// The board's entry point.
//
// The panel is 4", 720x720 round IPS on 2-lane MIPI-DSI (JD9365) with a GT911
// capacitive touch controller. All of that is handled by the Waveshare BSP
// component; this file only starts it and hands over to app::Startup(), which
// is the half of the boot that the simulator shares.

#include "KanardiaCommon.h"

#include "App.h"
#include "Platform.h"

#include "bsp/display.h"
#include "bsp/esp-bsp.h"

namespace { constexpr const char* TAG = "main"; } // namespace

extern "C" void app_main(void)
{
	APP_LOGI(TAG, "panel %dx%d, %d-lane MIPI-DSI", BSP_LCD_H_RES, BSP_LCD_V_RES, BSP_LCD_MIPI_DSI_LANE_NUM);

	// Zero-initialised, then filled in explicitly: ESP_LV_ADAPTER_DEFAULT_CONFIG()
	// leaves the nested auto_sleep.callbacks member out and trips
	// -Wmissing-field-initializers in C++. Everything left at zero (auto sleep
	// off, stack in internal RAM, no touch axis flips) matches its defaults.
	bsp_display_cfg_t cfg = {};
	// ThorVG rasterises from the LVGL task; the adapter's 8 kB default is tight.
	cfg.lv_adapter_cfg.task_stack_size	 = 32 * 1024;
	cfg.lv_adapter_cfg.task_priority		 = ESP_LV_ADAPTER_DEFAULT_TASK_PRIORITY;
	cfg.lv_adapter_cfg.task_core_id		 = ESP_LV_ADAPTER_DEFAULT_TASK_CORE_ID;
	cfg.lv_adapter_cfg.tick_period_ms	 = ESP_LV_ADAPTER_DEFAULT_TICK_PERIOD_MS;
	cfg.lv_adapter_cfg.task_min_delay_ms = ESP_LV_ADAPTER_DEFAULT_TASK_MIN_DELAY_MS;
	cfg.lv_adapter_cfg.task_max_delay_ms = ESP_LV_ADAPTER_DEFAULT_TASK_MAX_DELAY_MS;
	cfg.rotation								 = ESP_LV_ADAPTER_ROTATE_0;
	cfg.tear_avoid_mode						 = ESP_LV_ADAPTER_TEAR_AVOID_MODE_TRIPLE_PARTIAL;

	if(bsp_display_start_with_config(&cfg) == nullptr) {
		APP_LOGE(TAG, "display init failed");
		return;
	}

	if(app::Startup() == false)
		return;

	bsp_display_backlight_on();

	// Largest block, not just the total: the 32 kB stacks above need it
	// contiguous, and that is what runs out first.
	const platform::HeapStats heap = platform::GetHeapStats();
	APP_LOGI(
		TAG,
		"free heap: %u B internal (largest block %u B), %u B PSRAM",
		static_cast<unsigned>(heap.uFreeInternal),
		static_cast<unsigned>(heap.uLargestBlock),
		static_cast<unsigned>(heap.uFreePsram)
	);
}
