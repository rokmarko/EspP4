/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// The board's network: Wi-Fi station, through the ESP32-C6 next to the P4.
//
// The ESP32-P4 has no radio of its own. This board carries an ESP32-C6 beside
// it on an SDIO bus, and `espressif/esp_wifi_remote` plus `espressif/esp_hosted`
// make that arrangement invisible: the esp_wifi_* calls below are the ordinary
// ones, and the component pair carries each of them across to the C6 and brings
// the answer back. Nothing here mentions the transport, which is why the SDIO
// pins appear nowhere -- esp_hosted's defaults are what this board is wired to,
// and Waveshare's own wifistation example sets none either.
//
// Three things to know:
//
//   - **The C6 has to be carrying the hosted slave firmware.** It ships with
//     it. If Wi-Fi never gets past "connecting" and the log shows no answer
//     from the slave at all, that is the thing to check before the antenna or
//     the passphrase.
//   - **The network comes up last, after the big stacks.** esp_hosted and lwip
//     want a good deal of internal RAM and start tasks of their own, and the
//     three 32 kB stacks this product needs are contiguous internal RAM that
//     nothing else may take first. app::Startup() calls NetworkStart() after
//     the model loop for that reason.
//   - **It reconnects for as long as the board is on.** A panel that lost the
//     ap while the hangar door was open should be back on the bus by the time
//     anyone looks at it, so there is no retry limit and no giving up.
//
// The credentials are compiled in, overridable from the build the same way the
// cloud client's provisioning pair is:
//
//     target_compile_definitions(${COMPONENT_LIB} PRIVATE WIFI_SSID="...")

#include "Platform.h"

#include "esp_event.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "nvs_flash.h"

#include <atomic>
#include <cstdio>
#include <cstring>

#ifndef WIFI_SSID
#define WIFI_SSID "Pikapoka"
#endif

#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "pikapoka"
#endif

namespace platform {

namespace {

	constexpr const char* TAG = "wifi";

	enum class State : uint8_t
	{
		Off,
		Connecting,
		Up
	};

// Written by the event task, read by the console. Two atomics rather than a
// string, so a reader can never catch a half-written one.
	std::atomic<State>	 g_eState{State::Off};
	std::atomic<uint32_t> g_uAddress{0};

	bool		g_bStarted	= false;
	uint32_t g_uAttempts = 0;

// The default `nvs` partition, which is IDF's own: Wi-Fi keeps its calibration
// and its stored configuration there. app::Settings has a partition of its own
// (`settings`) and is not touched by this.
	bool MountNvs()
	{
		esp_err_t err = nvs_flash_init();
		if(err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
			ESP_ERROR_CHECK(nvs_flash_erase());
			err = nvs_flash_init();
		}
		if(err != ESP_OK) {
			APP_LOGE(TAG, "nvs_flash_init: %s", esp_err_to_name(err));
			return false;
		}
		return true;
	}

	void OnEvent(void*, esp_event_base_t base, int32_t iId, void* pData)
	{
		if(base == WIFI_EVENT && iId == WIFI_EVENT_STA_START) {
			g_eState = State::Connecting;
			esp_wifi_connect();
			return;
		}

		if(base == WIFI_EVENT && iId == WIFI_EVENT_STA_DISCONNECTED) {
			const auto* pEv = static_cast<wifi_event_sta_disconnected_t*>(pData);

			g_uAddress = 0;
			g_eState	  = State::Connecting;

			// Loud the first few times, then quiet: an ap that is simply not
			// there would otherwise fill the log for as long as the board is
			// on, and the state is in the console's `i` line anyway.
			if(++g_uAttempts <= 3)
				APP_LOGW(TAG, "'%s' disconnected (reason %d); retrying", WIFI_SSID, pEv != nullptr ? pEv->reason : 0);
			else
				APP_LOGD(TAG, "'%s' disconnected (reason %d)", WIFI_SSID, pEv != nullptr ? pEv->reason : 0);

			esp_wifi_connect();
			return;
		}

		if(base == IP_EVENT && iId == IP_EVENT_STA_GOT_IP) {
			const auto* pEv = static_cast<ip_event_got_ip_t*>(pData);

			g_uAddress	= pEv != nullptr ? pEv->ip_info.ip.addr : 0;
			g_eState		= State::Up;
			g_uAttempts = 0;
			APP_LOGI(TAG, "'%s' up, address " IPSTR, WIFI_SSID, IP2STR(&pEv->ip_info.ip));
		}
	}

} // namespace

// --------------------------------------------------------------------------

bool NetworkStart()
{
	if(g_bStarted)
		return true;

	if(MountNvs() == false)
		return false;

	// Both of these may already have been done by something else in the
	// image; saying so is not an error.
	esp_err_t err = esp_netif_init();
	if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		APP_LOGE(TAG, "esp_netif_init: %s", esp_err_to_name(err));
		return false;
	}

	err = esp_event_loop_create_default();
	if(err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
		APP_LOGE(TAG, "esp_event_loop_create_default: %s", esp_err_to_name(err));
		return false;
	}

	if(esp_netif_create_default_wifi_sta() == nullptr) {
		APP_LOGE(TAG, "no station interface");
		return false;
	}

	// The C6 is reached from here: esp_wifi_init() is where esp_wifi_remote
	// starts talking to the slave, so a board whose companion is not answering
	// fails on this call rather than later.
	wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
	err						  = esp_wifi_init(&cfg);
	if(err != ESP_OK) {
		APP_LOGE(TAG, "esp_wifi_init: %s -- is the C6 running the hosted slave firmware?", esp_err_to_name(err));
		return false;
	}

	esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID, &OnEvent, nullptr, nullptr);
	esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP, &OnEvent, nullptr, nullptr);

	wifi_config_t sta = {};
	std::snprintf(reinterpret_cast<char*>(sta.sta.ssid), sizeof(sta.sta.ssid), "%s", WIFI_SSID);
	std::snprintf(reinterpret_cast<char*>(sta.sta.password), sizeof(sta.sta.password), "%s", WIFI_PASSWORD);
	// Whatever the ap offers, as long as it is not open: the passphrase is
	// compiled in and an open network would not be using it.
	sta.sta.threshold.authmode = WIFI_AUTH_WEP;

	ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
	ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &sta));

	err = esp_wifi_start();
	if(err != ESP_OK) {
		APP_LOGE(TAG, "esp_wifi_start: %s", esp_err_to_name(err));
		return false;
	}

	// Asynchronous from here: the association and the address arrive as
	// events, and IsNetworkUp() is how anything else finds out. Nothing waits
	// -- the scene has a panel to draw whether or not there is an ap.
	g_bStarted = true;
	g_eState	  = State::Connecting;
	APP_LOGI(TAG, "joining '%s'", WIFI_SSID);
	return true;
}

// --------------------------------------------------------------------------

bool IsNetworkUp()
{
	return g_eState.load() == State::Up;
}

// --------------------------------------------------------------------------

const char* NetworkStatus()
{
	// Formatted per call into the caller's own storage, so the console task
	// and the event task cannot meet inside one buffer.
	static thread_local char szStatus[20];

	switch(g_eState.load()) {
	case State::Off:			return "off";
	case State::Connecting: return "joining";
	case State::Up:			break;
	}

	const uint32_t uIp = g_uAddress.load();
	std::snprintf(
		szStatus,
		sizeof(szStatus),
		"%u.%u.%u.%u",
		static_cast<unsigned>(uIp & 0xFF),
		static_cast<unsigned>((uIp >> 8) & 0xFF),
		static_cast<unsigned>((uIp >> 16) & 0xFF),
		static_cast<unsigned>((uIp >> 24) & 0xFF)
	);
	return szStatus;
}

} // namespace platform
