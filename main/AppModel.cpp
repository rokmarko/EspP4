/**
 * @file AppModel.cpp
 * @brief Concrete avio::ModelBase plus the task that drives it.
 */

#include "AppModel.h"

#include "CanPortEsp.h"
#include "CanProcessor.h"

#include "CanAerospace/MessageAerospace.h"

#include "esp_log.h"
#include "esp_timer.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#include <cmath>

namespace app {

namespace {

constexpr const char *TAG = "model";

/* ModelBase asks for a 50 ms beat and a 1 s beat. */
constexpr uint32_t TICK_MS       = 50;
constexpr uint32_t TICKS_PER_SEC = 1000 / TICK_MS;

/* ThorVG already wants a big stack; the model is far more modest, but Navigation
 * and SunriseSunset do real floating-point work, so do not go below 8 kB. */
constexpr uint32_t TASK_STACK  = 8 * 1024;
constexpr UBaseType_t TASK_PRIO = 4;

Model         *g_pModel = nullptr;
TaskHandle_t   g_hTask  = nullptr;

/* The CAN half. Both are function-local statics created in
 * StartModelLoop() and only pointed at from here. */
CanPortEsp   *g_pPort = nullptr;
CanProcessor *g_pProc = nullptr;

/**
 * One CANaerospace normal-operation-data frame.
 *
 * Register A carries the CANaerospace header -- data index, data type,
 * service code, message code -- and register B the value. ProcessNOD()
 * reads the NOD index out of the service-code byte, exactly as Horis does.
 */
can::Message MakeNodMessage(can::Id eId, uint8_t uNodIndex, float fValue)
{
    namespace os = can::oldservice;

    can::Register rA;
    rA.by[os::DATA_INDEX_INDEX]   = 0;
    rA.by[os::DATA_TYPE_INDEX]    = os::dtFloat;
    rA.by[os::SERVICE_CODE_INDEX] = uNodIndex;
    rA.by[os::MESSAGE_CODE_INDEX] = 0;

    const can::Identifier id(can::lPrimary, eId, CAN_NODE_ID, can::EVERYBODY);
    can::Message msg(id, rA, can::Register(fValue));
    msg.SetFrameInfo(8, false, true);
    return msg;
}

} // namespace

// --------------------------------------------------------------------------

Model::Model()
    : NodOwner()
    , avio::ModelBase(0 /* own serial: no CAN identity on this board */, m_nodOwned)
{
}

// --------------------------------------------------------------------------

void Model::SaveLastKnownCoordinate()
{
    /* Nothing to save to: no EEPROM, no options storage on this board. */
    ESP_LOGD(TAG, "last known coordinate would be saved here");
}

// --------------------------------------------------------------------------

void Model::HandleNavigationChange(const avio::navigation::Action eAction,
                                   const uint32_t uActivateAxisCombo)
{
    ESP_LOGI(TAG, "navigation change: action=%d axes=0x%02x",
             static_cast<int>(eAction), static_cast<unsigned>(uActivateAxisCombo));
}

// --------------------------------------------------------------------------

void Model::ActivateAutopilot(const uint32_t uAxisCombo,
                              const avio::autopilot::Operation eOperation)
{
    /* No autopilot on the bus; log so the call is visible while bringing up. */
    ESP_LOGI(TAG, "autopilot: axes=0x%02x op=%d",
             static_cast<unsigned>(uAxisCombo), static_cast<int>(eOperation));
}

// --------------------------------------------------------------------------

void Model::Simulate(float fSeconds)
{
    /* Only in self-test, where the frames come back to us and reach no further.
     * On a real bus these ids belong to somebody else. */
    if (g_pPort == nullptr || g_pPort->GetMode() != CanPortEsp::Mode::SelfTest)
        return;

    /* Stand in for the engine ECU: idle, then a slow sweep up through the green
     * arc and back. */
    const float fRel = 0.5f + 0.5f * std::sin(fSeconds * 0.35f);
    g_pPort->Send(MakeNodMessage(can::Id::EngineRPM_1, 0, 400.0f + 2500.0f * fRel));

    /* A little vertical acceleration so GMinMax has something to collect. */
    g_pPort->Send(MakeNodMessage(can::Id::AccelerationZ, 0,
                  common::STANDARD_GRAVITY * (1.0f + 0.15f * std::sin(fSeconds * 1.7f))));

    /* Air data, in the SI units CANaerospace carries: m/s and metres. The
     * scales convert to km/h and feet, because bands are in user units. */
    const float fKmh = 150.0f + 100.0f * std::sin(fSeconds * 0.23f);
    g_pPort->Send(MakeNodMessage(can::Id::IndicatedAirspeed, 0, fKmh / 3.6f));

    const float fFeet = 7500.0f + 7500.0f * std::sin(fSeconds * 0.05f);
    g_pPort->Send(MakeNodMessage(can::Id::BaroCorrectedAltitude, 0, fFeet / 3.28084f));
}

// --------------------------------------------------------------------------

void Model::SendSignOfLife()
{
    if (g_pPort == nullptr || g_pPort->GetMode() != CanPortEsp::Mode::SelfTest)
        return;

    namespace os = can::oldservice;

    /* Same shape SOLAutoId::MakeSOL() builds: a service request whose service
     * code is sSignOfLife, hardware type in the message-code byte, serial in
     * register B. */
    const can::Identifier id(can::lPrimary, can::Id::ServiceRequest,
                             CAN_NODE_ID, can::EVERYBODY);
    const can::Message msg(
        id,
        can::Register(uint8_t(0), uint8_t(os::dtULong),
                      uint8_t(os::sSignOfLife), uint8_t(CAN_NODE_ID)),
        can::Register(uint32_t(DEMO_SERIAL)));

    g_pPort->Send(msg);
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

void ModelTask(void *)
{
    const int64_t tStart = esp_timer_get_time();
    TickType_t    tWake  = xTaskGetTickCount();
    /* Primed so the one-second work runs on the very first tick: a unit that
     * has just come up announces itself straight away, it does not wait. */
    uint32_t      uTick  = TICKS_PER_SEC - 1;

    for (;;) {
        vTaskDelayUntil(&tWake, pdMS_TO_TICKS(TICK_MS));

        const float fSeconds = static_cast<float>(esp_timer_get_time() - tStart) / 1e6f;
        g_pModel->Simulate(fSeconds);

        g_pModel->Update50ms();

        if (++uTick >= TICKS_PER_SEC) {
            uTick = 0;
            g_pModel->Update1s();
            g_pModel->SendSignOfLife();
            if (g_pProc) g_pProc->Update1s();
        }
    }
}

} // namespace

bool StartModelLoop()
{
    if (g_pModel != nullptr) return true;

    static Model model;
    g_pModel = &model;

    /* CAN first, so the port exists before the first Simulate() call.
     *
     * Self-test by default: this board carries no transceiver, and in that mode
     * the controller takes its own frames back, which exercises port ->
     * processor -> NOD -> model for real. Wire a transceiver to the pins and
     * switch to Mode::Normal to sit on an actual bus. */
    CanPortEsp::Config cfg;
    cfg.eMode = CanPortEsp::Mode::SelfTest;

    /* The port hands every frame to the processor, which files it in the NOD
     * the model is already reading. */
    static CanPortEsp port(
        [](const can::Message &msg) { if (g_pProc) g_pProc->Process(msg); }, cfg);
    g_pPort = &port;

    static CanProcessor proc(model.GetNODStore(), port);
    g_pProc = &proc;

    if (port.Start() == false) {
        ESP_LOGE(TAG, "CAN port failed to start; the NOD will stay empty");
    }

    if (xTaskCreate(ModelTask, "model", TASK_STACK, nullptr, TASK_PRIO, &g_hTask) != pdPASS) {
        ESP_LOGE(TAG, "could not create the model task");
        g_pModel = nullptr;
        return false;
    }

    ESP_LOGI(TAG, "model loop running: Update50ms every %u ms, Update1s every %u ms",
             static_cast<unsigned>(TICK_MS), static_cast<unsigned>(TICK_MS * TICKS_PER_SEC));
    return true;
}

// --------------------------------------------------------------------------

Model *GetModel()
{
    return g_pModel;
}

// --------------------------------------------------------------------------

CanPortEsp *GetCanPort()
{
    return g_pPort;
}

// --------------------------------------------------------------------------

CanProcessor *GetCanProcessor()
{
    return g_pProc;
}

// --------------------------------------------------------------------------

uint32_t ModelStackHeadroom()
{
    if (g_hTask == nullptr) return 0;
    /* FreeRTOS reports the high water mark in words on this port. */
    return uxTaskGetStackHighWaterMark(g_hTask) * sizeof(StackType_t);
}

} // namespace app
