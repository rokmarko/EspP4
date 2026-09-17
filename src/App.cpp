/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

#include "App.h"

#include "AppModel.h"
#include "MqttClient.h"
#include "Platform.h"
#include "SerialConsole.h"
#include "VectorScene.h"

#include "Avio/Format/AvioFormat.h"
#include "Unit/UnitFormatterUtf8.h"

namespace app {

namespace {

	constexpr const char* TAG = "app";

// Hand Common's formatting layer the formatter it works through.
//
// `avio::format` keeps one process-wide `unit::Formatter*` and asserts on it;
// everything below -- ToString(), ToStringFromSystemUnit(), Formatter::
// FormatAzimuth() -- reaches it from there, so no call site has to carry one.
// The UTF-8 formatter is the one that maps a unit onto the private-use
// codepoint the Kanardia font draws for it.
	void InstallUnitFormatter()
	{
		static const unit::FormatterUtf8 formatter;
		avio::format::SetUnitFormatter(&formatter);
	}

} // namespace

// --------------------------------------------------------------------------

bool Startup()
{
	// Before anything formats a value -- the scene builds its labels below.
	InstallUnitFormatter();

	// Console first, then the model, then the scene.
	//
	// On the board the console and the CAN thread each want a 32 kB
	// *contiguous* stack out of internal RAM, and the model loop mounts the
	// NVS settings partition on its way past -- so the big stacks are taken
	// while the heap is still clean. The console failing is especially bad,
	// since it is the only way to see anything from the host. None of that
	// binds on a desktop, but the order costs nothing there and one order is
	// easier to reason about than two.
	//
	// The scene comes last because it reads its colour bands out of the
	// parameter container, and that is only populated -- from its defaults and
	// then from the stored blob -- once the model loop has run.
	demo::StartSerialConsole();

	if(StartModelLoop() == false)
		APP_LOGE(TAG, "model loop failed to start");

	// The network, after the model loop and never before it: bringing Wi-Fi up
	// on the board starts esp_hosted and lwip, which want internal RAM and
	// tasks of their own, and the three 32 kB contiguous stacks this product
	// needs have to be placed while the heap is still clean.
	platform::NetworkStart();

	// The cloud client, and only if there is a broker configured to talk to:
	// there is no network stack on the board yet and no broker on a desk, so
	// one that dialled out on every boot would do nothing but fill the log.
	// It is started after the model loop because it publishes what the model
	// holds and keeps its credentials in the settings store the loop opened.
	if(MqttClient::IsConfigured()) {
		GetMqttClient().Connect();
	}
	else {
		// Silence here reads as "the cloud client is not in this build", which
		// is the wrong conclusion: it is in, and waiting to be told where to
		// dial. Say so once.
		APP_LOGI(TAG, "cloud client off; set ESPP4_MQTT_HOST=host[:port], or press 'c' on the console");
	}

	platform::LockDisplay();
	const bool bOk = demo::CreateScene();
	platform::UnlockDisplay();

	if(bOk == false) {
		APP_LOGE(TAG, "scene init failed");
		return false;
	}

	return true;
}

} // namespace app
