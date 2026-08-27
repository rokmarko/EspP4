/**
 * @file AppModel.cpp
 * @brief Concrete avio::ModelBase plus the task that drives it.
 */

#include "AppModel.h"

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
    /* Stand in for the engine ECU on the CAN bus: idle, then a slow sweep up
     * through the green arc and back. Same shape the scene used to invent for
     * itself, only now it arrives the way real data would. */
    const float fRel = 0.5f + 0.5f * std::sin(fSeconds * 0.35f);
    m_nodOwned.SetFloat(can::Id::EngineRPM_1, 0, 400.0f + 2500.0f * fRel);

    /* A little vertical acceleration so GMinMax has something to collect. */
    m_nodOwned.SetFloat(can::Id::AccelerationZ, 0,
                        common::STANDARD_GRAVITY * (1.0f + 0.15f * std::sin(fSeconds * 1.7f)));
}

// --------------------------------------------------------------------------

float Model::GetEngineRPM() const
{
    return GetNOD().GetEngineRPM().GetValue();
}

// --------------------------------------------------------------------------
//  Processing loop
// --------------------------------------------------------------------------

namespace {

void ModelTask(void *)
{
    const int64_t tStart = esp_timer_get_time();
    TickType_t    tWake  = xTaskGetTickCount();
    uint32_t      uTick  = 0;

    for (;;) {
        vTaskDelayUntil(&tWake, pdMS_TO_TICKS(TICK_MS));

        const float fSeconds = static_cast<float>(esp_timer_get_time() - tStart) / 1e6f;
        g_pModel->Simulate(fSeconds);

        g_pModel->Update50ms();

        if (++uTick >= TICKS_PER_SEC) {
            uTick = 0;
            g_pModel->Update1s();
        }
    }
}

} // namespace

bool StartModelLoop()
{
    if (g_pModel != nullptr) return true;

    static Model model;
    g_pModel = &model;

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

uint32_t ModelStackHeadroom()
{
    if (g_hTask == nullptr) return 0;
    /* FreeRTOS reports the high water mark in words on this port. */
    return uxTaskGetStackHighWaterMark(g_hTask) * sizeof(StackType_t);
}

} // namespace app
