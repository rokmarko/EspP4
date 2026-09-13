/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// The platform surface on ESP-IDF and the Waveshare BSP.
//
// Every one of these is a line or two around an IDF call, which is the point:
// nothing above this file names FreeRTOS, and nothing in it decides anything.

#include "Platform.h"

#include "bsp/esp-bsp.h"

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cstdio>

namespace platform {

namespace {

	constexpr const char* TAG = "platform";

// Host link buffers. The transmit side carries base64 screenshots, so it is
// the larger of the two by a good margin.
	constexpr int USB_RX_BUF = 1024;
	constexpr int USB_TX_BUF = 4096;

// How long a blocked write waits before giving up on the host.
	constexpr TickType_t WRITE_WAIT = pdMS_TO_TICKS(2000);

	esp_log_level_t ToIdf(LogLevel eLevel)
	{
		switch(eLevel) {
		case LogLevel::None:	 return ESP_LOG_NONE;
		case LogLevel::Error: return ESP_LOG_ERROR;
		case LogLevel::Warn:	 return ESP_LOG_WARN;
		case LogLevel::Info:	 return ESP_LOG_INFO;
		case LogLevel::Debug: return ESP_LOG_DEBUG;
		}
		return ESP_LOG_INFO;
	}

} // namespace

// --------------------------------------------------------------------------

const char* Name()
{
	return "esp32p4";
}

// --------------------------------------------------------------------------

// APP_LOG* is ESP-IDF's own macro on this build and never reaches here; this
// is for anything that calls platform::Log() by name.
void Log(LogLevel eLevel, const char* pszTag, const char* pszFmt, ...)
{
	char	  szLine[256];
	va_list args;
	va_start(args, pszFmt);
	std::vsnprintf(szLine, sizeof(szLine), pszFmt, args);
	va_end(args);

	ESP_LOG_LEVEL(ToIdf(eLevel), pszTag, "%s", szLine);
}

// --------------------------------------------------------------------------

void SetLogLevel(LogLevel eLevel)
{
	esp_log_level_set("*", ToIdf(eLevel));
}

// --------------------------------------------------------------------------

int64_t Micros()
{
	return esp_timer_get_time();
}

// --------------------------------------------------------------------------

void SleepMs(uint32_t uMs)
{
	vTaskDelay(pdMS_TO_TICKS(uMs));
}

// --------------------------------------------------------------------------

void LockDisplay()
{
	bsp_display_lock(UINT32_MAX);
}

// --------------------------------------------------------------------------

void UnlockDisplay()
{
	bsp_display_unlock();
}

// --------------------------------------------------------------------------

HeapStats GetHeapStats()
{
	HeapStats stats;
	stats.uFreeInternal	  = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_INTERNAL));
	stats.uFreePsram		  = static_cast<uint32_t>(heap_caps_get_free_size(MALLOC_CAP_SPIRAM));
	stats.uMinFreeInternal = static_cast<uint32_t>(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL));
	stats.uLargestBlock	  = static_cast<uint32_t>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
	return stats;
}

// --------------------------------------------------------------------------

TaskHandle StartTask(const TaskConfig& cfg, void (*pfnEntry)(void*), void* pCtx)
{
	TaskHandle_t hTask = nullptr;
	if(xTaskCreate(pfnEntry, cfg.pszName, cfg.uStack, pCtx, static_cast<UBaseType_t>(cfg.iPriority), &hTask) != pdPASS) {
		// A stack this size has to be *contiguous* internal RAM, and after boot
		// the largest free block is around 31 kB -- so say what was free as
		// well as what was asked for.
		APP_LOGE(
			TAG,
			"could not create task '%s' (%u B stack); largest free internal block is %u B",
			cfg.pszName,
			static_cast<unsigned>(cfg.uStack),
			static_cast<unsigned>(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL))
		);
		return nullptr;
	}
	return static_cast<TaskHandle>(hTask);
}

// --------------------------------------------------------------------------

uint32_t StackHeadroom(TaskHandle hTask)
{
	if(hTask == nullptr)
		return 0;
	// FreeRTOS reports the high water mark in words on this port.
	return uxTaskGetStackHighWaterMark(static_cast<TaskHandle_t>(hTask)) * sizeof(StackType_t);
}

// --------------------------------------------------------------------------

bool ConsoleOpen()
{
	usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
	cfg.rx_buffer_size						= USB_RX_BUF;
	cfg.tx_buffer_size						= USB_TX_BUF;

	const esp_err_t err = usb_serial_jtag_driver_install(&cfg);
	if(err != ESP_OK) {
		APP_LOGE(TAG, "usb_serial_jtag_driver_install failed: 0x%x", err);
		return false;
	}

	// Route printf()/ESP_LOG through the driver so log output and console
	// output cannot interleave mid-byte.
	usb_serial_jtag_vfs_use_driver();
	return true;
}

// --------------------------------------------------------------------------

size_t ConsoleRead(uint8_t* pData, size_t uSize)
{
	const int n = usb_serial_jtag_read_bytes(pData, uSize, portMAX_DELAY);
	return n > 0 ? static_cast<size_t>(n) : 0;
}

// --------------------------------------------------------------------------

void ConsoleWrite(const uint8_t* pData, size_t uSize)
{
	while(uSize > 0) {
		const int n = usb_serial_jtag_write_bytes(pData, uSize, WRITE_WAIT);
		if(n <= 0)
			return;  // the host is not reading; dropping is better than hanging
		pData += n;
		uSize -= static_cast<size_t>(n);
	}
}

} // namespace platform
