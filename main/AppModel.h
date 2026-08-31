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
 * The ESP32-P4 demo board has no GNSS receiver, so that hook answers "nothing
 * connected" and the NOD is fed over CAN. Options do have somewhere to live:
 * app::Settings keeps them as blobs in the `settings` NVS partition.
 * Everything above the NOD is the real Common code, running unmodified.
 */

#include "KanardiaCommon.h"

#include "AppOptions.h"
#include "AppParameters.h"

#include "Avio/Model/ModelBase.h"
#include "CanAerospace/CanNOD.h"

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

    /** The stored options, for app::Settings to read and write. */
    Options &GetOptions() { return m_options; }

    /**
     * The instrument parameters. They read from this model's own NOD, so the
     * scenes get filtered values in the pilot's units without touching either.
     */
    Parameters &GetParameters() { return m_params; }
    const Parameters &GetParameters() const { return m_params; }

    /**
     * Seed the model's last-known position from what came out of NVS.
     *
     * ModelBase starts its own tracker at whatever it was constructed with;
     * this hands it the coordinate the previous flight ended at, which is the
     * whole point of storing it. Call once, after the options are loaded.
     */
    void RestoreLastKnownCoordinate();

    /**
     * Stand in for an ECU on the bus: transmit one set of CANaerospace NOD
     * frames. In self-test mode the controller hands them straight back, so
     * they reach the NOD by exactly the route real traffic would take.
     *
     * Does nothing on a real bus -- this board listens there, it does not
     * pretend to be somebody else's engine.
     *
     * @param fSeconds  seconds since boot, drives the synthetic engine run.
     */
    void Simulate(float fSeconds);

    /**
     * Stand in for a configuration tool: push a modified engine-rpm parameter
     * at ourselves over the bus.
     *
     * Sends the real thing -- a DDS_BUFFER download carrying one
     * `parameter::fbs::ParamItem` flatbuffer, then an MCS_APPLY_BUFFER_DATA
     * message with the buffer's length and CRC-16. In self-test the controller
     * hands every frame back, so it arrives through DDS_B and MCS_B exactly as
     * a real node's push would, and the tachometer's bands visibly change.
     *
     * Self-test only, for the same reason Simulate() is.
     *
     * @return false if the port is not in self-test, or a frame could not be
     *         posted.
     */
    bool SimulateParameterPush();

    /**
     * Announce ourselves the way every Kanardia module does, once a second.
     *
     * This is what populates the unit container: a sign-of-life carries the
     * sender's node id, hardware type and serial, and the container asks any
     * unit it has not seen before to identify itself properly.
     *
     * Sent in every mode, unlike Simulate(): the id it goes out under is the
     * one SOLAutoId negotiated against whoever else is on the bus, so there is
     * nothing to collide with.
     */
    void SendSignOfLife();

    /* All three read straight out of the NOD, in the CAN units the bus uses. */

    /** Engine rpm as the model currently sees it. */
    float GetEngineRPM() const;
    /** Indicated airspeed [m/s]. */
    float GetIAS() const;
    /** Baro-corrected altitude [m]. */
    float GetAltitude() const;

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

public:
    /**
     * Serial reported in our sign-of-life. No real unit carries this one.
     *
     * Public because SOLAutoId needs it, and SOLAutoId lives in CanProcessor:
     * a collision on the auto id is settled by comparing serials, so the
     * number has to reach the state machine that does the comparing.
     */
    static constexpr uint32_t DEMO_SERIAL = 0xE5B04;

private:
    /** 180 km/h, a plausible default for the kind of aircraft this ends up in. */
    static constexpr float CRUISE_MPS = 50.0f;

    Options    m_options;
    /* Constructed from m_nodOwned, which NodOwner has already built: bases
     * before members, and NodOwner is the first base. */
    Parameters m_params{m_nodOwned};
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

/** The CAN port, or nullptr before StartModelLoop(). */
class CanPortEsp *GetCanPort();

/** The CANaerospace processor, or nullptr before StartModelLoop(). */
class CanProcessor *GetCanProcessor();

/**
 * Take a parameter another node pushed over CAN, apply it and save it.
 *
 * Call from the thread that reads the parameters -- the LVGL task, via the
 * scene's tick. Applying resizes the parameter's value vector, so it must not
 * run while another thread is sampling values; and re-saving the whole
 * container keeps the pushed change across a power cycle.
 *
 * @return the can::Id that changed, or can::Id::Invalid if nothing was
 *         waiting. A caller that caches band data should refresh it when this
 *         answers with an id.
 */
can::Id ApplyPushedParameter();

/** How many parameter pushes the bus has delivered since boot. */
uint32_t ParameterPushCount();

/** Whether an outgoing buffer push is still in flight. */
bool IsPushActive();

/**
 * How many option blobs Settings read back out of NVS at boot.
 *
 * Zero on the very first boot after a flash erase (the defaults are written
 * instead); the registered option count on every boot after that.
 */
uint32_t OptionsLoaded();

/**
 * Smallest free stack the model task has ever had, in bytes.
 *
 * Internal RAM is the tightest resource on this board, so the task's stack is
 * deliberately modest and this is how you find out whether it is modest enough.
 * Returns 0 before the loop starts.
 */
uint32_t ModelStackHeadroom();

} // namespace app
