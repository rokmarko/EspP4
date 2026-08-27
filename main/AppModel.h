#pragma once

/**
 * @file AppModel.h
 * @brief The smallest concrete avio::ModelBase this board can carry.
 *
 * ModelBase is the shared Kanardia flight model: it folds CAN network object
 * data (DirectNOD) into GNSS, navigation, clock, sunrise/sunset and the
 * above/below detectors that answer IsFlying() / IsEngineRunning() / IsMoving().
 * It is abstract -- the products supply storage, options and autopilot wiring.
 *
 * The ESP32-P4 demo board has no CAN transceiver, no GNSS receiver and no
 * options storage, so those hooks answer "nothing connected" and the NOD is fed
 * from Simulate(). Everything above the NOD is the real Common code, running
 * unmodified.
 */

#include "KanardiaCommon.h"

#include "Avio/Model/ModelBase.h"
#include "CanAerospace/CanNOD.h"
#include "Option/OptionsModel.h"

namespace app {

/**
 * Holds the DirectNOD that ModelBase takes by reference.
 *
 * ModelBase stores a reference, so the object has to exist before the base is
 * constructed -- which means it has to live in a base of its own, declared
 * first.
 */
class NodOwner {
protected:
    can::DirectNOD m_nodOwned;
};

// --------------------------------------------------------------------------

class Model : private NodOwner, public avio::ModelBase
{
public:
    Model();

    /** Direct access to the NOD, for whoever plays the part of the CAN bus. */
    can::DirectNOD &GetNODStore() { return m_nodOwned; }

    /**
     * Stand in for the CAN bus: push one set of values into the NOD.
     * @param fSeconds  seconds since boot, drives the synthetic engine run.
     */
    void Simulate(float fSeconds);

    /** Engine rpm as the model currently sees it. */
    float GetEngineRPM() const;

protected:
    /* --- avio::ModelBase hooks ----------------------------------------- */
    const avio::gnss::ExtendedReceiverNMEA *GetExternalNMEAReceiverAlive() const override
    { return nullptr; }

    option::ModelBase *GetOptionsBase() override { return &m_options; }

    void SaveLastKnownCoordinate() override;

    void HandleNavigationChange(const avio::navigation::Action eAction,
                                const uint32_t uActivateAxisCombo) override;

    void ActivateAutopilot(const uint32_t uAxisCombo,
                           const avio::autopilot::Operation eOperation) override;

    float GetAircraftDefaultCruisingSpeed() const override { return CRUISE_MPS; }

public:
    StampedExtNavStatus GetExternalNavigationStatus() const override { return {}; }

private:
    /** 180 km/h, a plausible default for the kind of aircraft this ends up in. */
    static constexpr float CRUISE_MPS = 50.0f;

    option::ModelBase m_options;
};

// --------------------------------------------------------------------------

/**
 * Start the model's processing loop: Update50ms() on a 50 ms tick and
 * Update1s() once per second, on their own FreeRTOS task.
 *
 * @return false if the task could not be created.
 */
bool StartModelLoop();

/** The one process-wide model. Valid only after StartModelLoop(). */
Model *GetModel();

/**
 * Smallest free stack the model task has ever had, in bytes.
 *
 * Internal RAM is the tightest resource on this board, so the task's stack is
 * deliberately modest and this is how you find out whether it is modest enough.
 * Returns 0 before the loop starts.
 */
uint32_t ModelStackHeadroom();

} // namespace app
