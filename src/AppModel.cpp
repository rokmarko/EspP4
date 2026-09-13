/***************************************************************************
 *                                                                         *
 *   Copyright (C) 2026 by Kanardia d.o.o. [see www.kanardia.eu]           *
 *                                                                         *
 *   License:                                                              *
 *      Proprietary - All rights reserved                                  *
 *                                                                         *
 ***************************************************************************/

// Concrete avio::ModelBase plus the task that drives it.

#include "AppModel.h"

#include "CanPort.h"
#include "CanProcessor.h"
#include "Platform.h"
#include "StorageOptions.h"

#include "CanAerospace/DownloadService.h"
#include "CanAerospace/MessageAerospace.h"
#include "CanAerospace/ModuleConfigIds.h"
#include "CRC/CRC-16.h"
#include "Parameter/ParamStorage.h"

#include <cstdlib>
#include <cstring>

#include <algorithm>
#include <cmath>
#include <vector>

namespace app {

namespace {

	constexpr const char* TAG = "model";

// ModelBase asks for a 50 ms beat and a 1 s beat.
	constexpr uint32_t TICK_MS			= 50;
	constexpr uint32_t TICKS_PER_SEC = 1000 / TICK_MS;

// ThorVG already wants a big stack; the model is far more modest, but Navigation
// and SunriseSunset do real floating-point work, so do not go below 8 kB.
	constexpr uint32_t TASK_STACK = 8 * 1024;
	constexpr int		 TASK_PRIO	= 4;

	Model*					g_pModel = nullptr;
	platform::TaskHandle g_hTask	= nullptr;

// How many option blobs came back out of NVS at boot. Reported by the console
// so a stored-then-rebooted round trip can be checked from the host.
	uint32_t g_uOptionsLoaded = 0;

// The CAN half. Both are function-local statics created in
// StartModelLoop() and only pointed at from here.
	CanPort*		  g_pPort = nullptr;
	CanProcessor* g_pProc = nullptr;

// One CANaerospace normal-operation-data frame.
//
// Register A carries the CANaerospace header -- data index, data type,
// service code, message code -- and register B the value. ProcessNOD()
// reads the NOD index out of the service-code byte, exactly as Horis does.
	can::Message MakeNodMessage(can::Id eId, uint8_t uNodIndex, float fValue)
	{
		namespace os = can::oldservice;

		can::Register rA;
		rA.by[os::DATA_INDEX_INDEX]	= 0;
		rA.by[os::DATA_TYPE_INDEX]		= os::dtFloat;
		rA.by[os::SERVICE_CODE_INDEX] = uNodIndex;
		rA.by[os::MESSAGE_CODE_INDEX] = 0;

		const can::Identifier id(can::lPrimary, eId, CAN_NODE_ID, can::EVERYBODY);
		can::Message			 msg(id, rA, can::Register(fValue));
		msg.SetFrameInfo(8, false, true);
		return msg;
	}

} // namespace

// --------------------------------------------------------------------------

Model::Model() :
	NodOwner(),
	avio::ModelBase(0 /* own serial: no CAN identity on this board */, m_nodOwned)
{}

// --------------------------------------------------------------------------

void Model::RestoreLastKnownCoordinate()
{
	m_lastKnownCoordinate.Initialize(m_options.m_lastKnown.GetCoordinate());
}

// --------------------------------------------------------------------------

void Model::SaveLastKnownCoordinate()
{
	// ModelBase calls this at most once a minute, and only after the aircraft
	// has moved a kilometre with a valid fix -- so this is not a hot path, and
	// one NVS write per call is the right shape. SetCoordinate() marks the
	// option dirty, which is what makes Save() pick it up.

	m_options.m_lastKnown.SetCoordinate(m_lastKnownCoordinate.Get());

	const uint32_t uWritten = GetSettings().Save(m_options);
	APP_LOGI(
		TAG,
		"last known coordinate saved (%u option blob%s written)",
		static_cast<unsigned>(uWritten),
		uWritten == 1 ? "" : "s"
	);
}

// --------------------------------------------------------------------------

void Model::HandleNavigationChange(const avio::navigation::Action eAction, const uint32_t uActivateAxisCombo)
{
	APP_LOGI(
		TAG,
		"navigation change: action=%d axes=0x%02x",
		static_cast<int>(eAction),
		static_cast<unsigned>(uActivateAxisCombo)
	);
}

// --------------------------------------------------------------------------

void Model::ActivateAutopilot(const uint32_t uAxisCombo, const avio::autopilot::Operation eOperation)
{
	// No autopilot on the bus; log so the call is visible while bringing up.
	APP_LOGI(TAG, "autopilot: axes=0x%02x op=%d", static_cast<unsigned>(uAxisCombo), static_cast<int>(eOperation));
}

// --------------------------------------------------------------------------

void Model::Simulate(float fSeconds)
{
	// Only in self-test, where the frames come back to us and reach no further.
	// On a real bus these ids belong to somebody else.
	if(g_pPort == nullptr || g_pProc == nullptr || g_pPort->GetMode() != CanPort::Mode::SelfTest)
		return;

	// Stand in for the engine ECU: idle, then a slow sweep up through the green
	// arc and back.
	const float fRel = 0.5f + 0.5f * std::sin(fSeconds * 0.35f);
	g_pPort->Send(MakeNodMessage(can::Id::EngineRPM_1, 0, 400.0f + 2500.0f * fRel));

	// Rotor rpm, tracking the engine through a fixed reduction the way it does
	// with the clutch engaged. Gives the dual tachometer a second live needle.
	g_pPort->Send(MakeNodMessage(can::Id::RotorRPM_1, 0, 380.0f + 200.0f * fRel));

	// A little vertical acceleration so GMinMax has something to collect.
	g_pPort->Send(
		MakeNodMessage(can::Id::AccelerationZ, 0, common::STANDARD_GRAVITY * (1.0f + 0.15f * std::sin(fSeconds * 1.7f)))
	);

	// Air data, in the SI units CANaerospace carries: m/s and metres. The
	// scales convert to km/h and feet, because bands are in user units.
	const float fKmh = 150.0f + 100.0f * std::sin(fSeconds * 0.23f);
	g_pPort->Send(MakeNodMessage(can::Id::IndicatedAirspeed, 0, fKmh / 3.6f));

	const float fFeet = 7500.0f + 7500.0f * std::sin(fSeconds * 0.05f);
	g_pPort->Send(MakeNodMessage(can::Id::BaroCorrectedAltitude, 0, fFeet / 3.28084f));
}

// --------------------------------------------------------------------------

bool Model::SimulateParameterPush()
{
	if(g_pPort == nullptr || g_pPort->GetMode() != CanPort::Mode::SelfTest)
		return false;
	if(g_pProc == nullptr)
		return false;

	// Build what a configuration tool would send: one parameter, with bands an
	// operator might have edited -- green ends earlier, yellow starts earlier.
	// A standalone Parameter is enough; only its description is packed.
	parameter::Parameter param(
		can::Id::EngineRPM_1,
		parameter::Function::EngineRPM,
		unit::Key::RPM,
		1,
		[](can::Id, uint8_t) { return common::StampedFloat(); }
	);
	param.SetUnitKeyUser(unit::Key::RPM);
	param.GetNames().Set("Engine RPM", "RPM", "RPM");

	parameter::Bands bands;
	bands.SetLow(0.0f);
	bands.Append(1200.0f, parameter::Color::NoColor, 0.0f);
	bands.Append(2200.0f, parameter::Color::Green, 0.0f);
	bands.Append(2600.0f, parameter::Color::Yellow, 0.0f);
	bands.Append(3000.0f, parameter::Color::Red, 0.0f);
	param.GetBands() = bands;	  // EngineRPM's system unit is rpm already.

	// From here on this is Nesis's DialogParameters::Transfer(), verbatim in
	// shape: pack the parameter, hand it to the unit as a Parameter buffer
	// download. There it reads
	//
	//     auto vFB = ParamStorage::GetParameterFB(pc.Find(pP->GetId()));
	//     if(vFB.empty()==false)
	//         pU->Download(BufferDownloadCommand::Parameter, vFB);
	//
	// and PushBuffer() is UnitInfoBase::Download() against the same services.
	const std::vector<uint8_t> vFB = parameter::ParamStorage::GetParameterFB(&param);
	if(vFB.empty()) {
		APP_LOGE(TAG, "could not pack the parameter to push");
		return false;
	}

	// At ourselves: in self-test the controller hands every frame back, so the
	// whole handshake runs -- offer, accept, data, checksum, commit.
	return g_pProc->PushBuffer(g_pProc->GetNodeId(), can::oldservice::BufferDownloadCommand::Parameter, vFB);
}

// --------------------------------------------------------------------------

void Model::SendSignOfLife()
{
	if(g_pProc == nullptr)
		return;

	// Every mode, unlike Simulate() and SimulateParameterPush(). Those put
	// frames on the bus under ids that belong to real sensors and to whoever
	// is being configured, so they stay inside self-test; a sign of life is
	// ours to send, and announcing ourselves is what a module on a real bus is
	// supposed to do. SOLAutoId is what makes that safe -- the id it settles
	// on is negotiated against the units already talking.
	//
	// The frame and the id behind it are CanProcessor's, through SOLAutoId --
	// see CanProcessor::PostSignOfLife(). Building the message here meant a
	// second copy of a layout Common already owns, and it did not match: the
	// data-type byte was dtULong where ServiceHandler sends a zero.
	g_pProc->PostSignOfLife();
	APP_LOGI(TAG, "sign-of-life sent, id=%u", static_cast<unsigned>(g_pProc->GetNodeId()));
}

// --------------------------------------------------------------------------

float Model::GetEngineRPM() const
{
	return GetNOD().GetEngineRPM().GetValue();
}

// --------------------------------------------------------------------------

float Model::GetIAS() const
{
	return GetNOD().GetIAS().GetValue();
}

// --------------------------------------------------------------------------

float Model::GetAltitude() const
{
	return GetNOD().GetBaroCorrectedAltitude().GetValue();
}

// --------------------------------------------------------------------------
//  Processing loop
// --------------------------------------------------------------------------

namespace {

	void ModelTask(void*)
	{
		const int64_t tStart = platform::Micros();
	// The beat is kept against the clock rather than by sleeping TICK_MS at a
	// time, so a slow tick is absorbed instead of accumulating. Falling more
	// than one period behind resets the phase: catching up by running several
	// Update50ms() back to back would be worse than losing one.
		int64_t tNext = tStart;

	// Primed so the one-second work runs on the very first tick: a unit that
	// has just come up announces itself straight away, it does not wait.
		uint32_t uTick = TICKS_PER_SEC - 1;

		for(;;) {
			tNext += TICK_MS * 1000;
			const int64_t tNow = platform::Micros();
			if(tNext > tNow)
				platform::SleepMs(static_cast<uint32_t>((tNext - tNow) / 1000));
			else
				tNext = tNow;

		// Stand in for the traffic a real bus would carry. Does nothing unless
		// the port is in self-test, which is the only mode where these frames
		// reach no further than ourselves -- so this is a no-op on the board's
		// default configuration and what makes the simulator move.
			g_pModel->Simulate(static_cast<float>(tNow - tStart) / 1e6f);

			g_pModel->Update50ms();

		// Drive the outgoing CANaerospace services. A buffer push moves one
		// message per OldServices::Update(), so it needs a real beat rather
		// than the once-a-second poke the rest of the service work gets.
			if(g_pProc)
				g_pProc->Pump();

			if(++uTick >= TICKS_PER_SEC) {
				uTick = 0;
				g_pModel->Update1s();
				g_pModel->SendSignOfLife();
				if(g_pProc)
					g_pProc->Update1s();
			}
		}
	}

} // namespace

bool StartModelLoop()
{
	if(g_pModel != nullptr)
		return true;

	static Model model;
	g_pModel = &model;

	// CAN first, so the port exists before the first Simulate() call.
	//
	// Which port that is, and what a mode means on it, is the build's business
	// -- the TWAI controller on the board, a CANU adapter in the simulator.
	// Both answer Mode::SelfTest the same way: frames sent come straight back
	// through port -> processor -> NOD -> model, which is what makes a unit
	// with no bus in front of it worth running at all.
	//
	// The default is Normal, which is what the board has wanted since it grew
	// a transceiver. The simulator drops to self-test on its own when there is
	// no adapter to open -- the usual state on a desk -- and it is in that
	// mode that Simulate() has something to talk to.
	CanPort::Config cfg;
	// Ignored on the board, where the pins are fixed; the simulator reads it
	// as the adapter's serial device and falls back to self-test without one.
	cfg.pszDevice = std::getenv("ESPP4_CAN_DEVICE");

	// The port hands every frame to the processor, which files it in the NOD
	// the model is already reading.
	g_pPort = CreateCanPort(
		[](const can::Message& msg) {
			if(g_pProc)
				g_pProc->Process(msg);
		},
		cfg
	);
	if(g_pPort == nullptr) {
		APP_LOGE(TAG, "no CAN port; the NOD will stay empty");
		return false;
	}

	static CanProcessor proc(model.GetNODStore(), *g_pPort, Model::DEMO_SERIAL);
	g_pProc = &proc;

	if(g_pPort->Start() == false) {
		APP_LOGE(TAG, "CAN port failed to start; the NOD will stay empty");
	}

	// Settings after the CAN port and before the model task, on purpose.
	//
	// Mounting an NVS partition costs internal RAM in proportion to its size
	// -- measured at ~2.2 kB per 4 kB sector -- and never gives it back, while
	// the CAN port's thread needs a *contiguous* 32 kB of it. Taking the big
	// stack first keeps that allocation off the fragmented end of the heap.
	// The options still land before anything reads them: Update1s() asks the
	// azimuth options for a magnetic declination, and the task below has not
	// started yet.
	//
	// An empty store is the normal state on a first boot -- write the defaults
	// out so the partition holds a known-good image from then on.
	Settings& settings = GetSettings();
	if(settings.Open()) {
		g_uOptionsLoaded = settings.Load(model.GetOptions());
		if(g_uOptionsLoaded == 0) {
			const uint32_t uWritten = settings.Save(model.GetOptions(), false);
			APP_LOGI(TAG, "no stored options; wrote %u defaults", static_cast<unsigned>(uWritten));
		}
		else {
			APP_LOGI(TAG, "%u option blobs loaded from NVS", static_cast<unsigned>(g_uOptionsLoaded));
		}
		model.RestoreLastKnownCoordinate();

		// The parameter set is one blob rather than one entry per key -- see
		// Settings::LoadParameters(). Nothing stored means the defaults built
		// in Parameters' constructor stand; write them out so the blob exists
		// and can be edited from a host later.
		if(settings.LoadParameters(model.GetParameters()) == false)
			settings.SaveParameters(model.GetParameters());
	}
	else {
		APP_LOGW(
			TAG,
			"settings unavailable; options and parameters "
			"stay at their defaults"
		);
	}

	const platform::TaskConfig taskCfg{"model", TASK_STACK, TASK_PRIO};
	g_hTask = platform::StartTask(taskCfg, ModelTask, nullptr);
	if(g_hTask == nullptr) {
		g_pModel = nullptr;
		return false;
	}

	APP_LOGI(
		TAG,
		"model loop running: Update50ms every %u ms, Update1s every %u ms",
		static_cast<unsigned>(TICK_MS),
		static_cast<unsigned>(TICK_MS * TICKS_PER_SEC)
	);
	return true;
}

// --------------------------------------------------------------------------

Model* GetModel()
{
	return g_pModel;
}

// --------------------------------------------------------------------------

CanPort* GetCanPort()
{
	return g_pPort;
}

// --------------------------------------------------------------------------

CanProcessor* GetCanProcessor()
{
	return g_pProc;
}

// --------------------------------------------------------------------------

can::Id ApplyPushedParameter()
{
	if(g_pModel == nullptr || g_pProc == nullptr)
		return can::Id::Invalid;

	std::vector<uint8_t> vBlob;
	if(g_pProc->TakePendingParameter(vBlob) == false)
		return can::Id::Invalid;

	const can::Id eId = g_pModel->GetParameters().ApplyPushedParameter(vBlob);
	if(eId == can::Id::Invalid)
		return eId;

	// Persist the whole container, not just the parameter that changed: the
	// blob is one packed image, and a push that survives only until the next
	// power cycle would be worse than useless.
	if(GetSettings().SaveParameters(g_pModel->GetParameters()) == false)
		APP_LOGE(TAG, "pushed parameter applied but could not be saved");

	return eId;
}

// --------------------------------------------------------------------------

uint32_t ParameterPushCount()
{
	return g_pProc != nullptr ? g_pProc->GetParameterPushCount() : 0;
}

// --------------------------------------------------------------------------

bool IsPushActive()
{
	return g_pProc != nullptr && g_pProc->IsPushActive();
}

// --------------------------------------------------------------------------

uint32_t OptionsLoaded()
{
	return g_uOptionsLoaded;
}

// --------------------------------------------------------------------------

uint32_t ModelStackHeadroom()
{
	return platform::StackHeadroom(g_hTask);
}

} // namespace app
