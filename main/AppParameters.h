#pragma once

/**
 * @file AppParameters.h
 * @brief The instrument parameters, held in Common's own container.
 *
 * `parameter::ParameterContainer` is how every Kanardia product manages the
 * values it displays: a hash of `parameter::Parameter` keyed by `can::Id`,
 * each one carrying its function, its system unit, the unit the pilot reads,
 * its colour bands, its names and a low-pass filter. A parameter pulls its own
 * value through a callback -- here, straight out of the `can::DirectNOD` the
 * CAN processor fills -- so the scenes no longer reach into the model for
 * numbers or do their own unit arithmetic.
 *
 * Bands are stored in the *system* unit (`function_util::GetSystemUnit()`:
 * m/s for airspeed, metres for altitude, rpm for engine speed) and converted
 * on the way out by `Parameter::GetUserBands()`. That is what makes a
 * parameter blob portable between products that show different units.
 */

#include "KanardiaCommon.h"

#include "CanAerospace/CanNOD.h"
#include "Parameter/ParamContainer.h"

namespace app {

class Parameters : public parameter::ParameterContainer
{
public:
    /**
     * Build the default set: engine rpm, indicated airspeed and altitude.
     *
     * @param nod  the network object data every parameter reads from. It has
     *             to outlive this container -- each Parameter keeps a callback
     *             that captures it by reference.
     */
    explicit Parameters(const can::DirectNOD &nod);

    /* Both answer sensibly for an id the container does not hold, because a
     * loaded blob may name parameters this product never registered. */

    /** The filtered value, converted to the unit the pilot reads. */
    float GetUserValue(can::Id eId) const;

    /** The colour bands, converted to the unit the pilot reads. */
    parameter::Bands GetUserBands(can::Id eId) const;

    /**
     * Apply one parameter pushed over CAN.
     *
     * @p sBlob is a single `parameter::fbs::ParamItem` flatbuffer -- what
     * `ParamStorage::GetParameterFB()` produces and what the Kanardia tools
     * send in a DDS_BUFFER push. Its `can_id` picks the parameter to replace;
     * bands, names, time constant, enabled flag and attributes all come from
     * the blob, and everything else this product decided (the user unit) is
     * left alone.
     *
     * Call this on the thread that reads the parameters -- it resizes the
     * value vector, so it cannot run while another thread is sampling.
     *
     * @return the id that changed, or can::Id::Invalid if the blob was not a
     *         parameter we hold.
     */
    can::Id ApplyPushedParameter(std::span<const uint8_t> sBlob);
};

} // namespace app
