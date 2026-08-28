/**
 * @file AppParameters.cpp
 */

#include "AppParameters.h"

#include "Parameter/ParamFunction.h"
#include "Parameter/ParamStorage.h"
#include "FBS/ParamStorageItem_generated.h"

#include "esp_log.h"

namespace app {

namespace {

using parameter::Bands;
using parameter::Color;
using parameter::Function;
using parameter::Parameter;

/**
 * One parameter, wired to the NOD.
 *
 * The bands come in whatever unit is convenient to write them in -- rpm, km/h,
 * feet -- and are converted to the function's system unit for storage, which is
 * the direction `Parameter::GetUserBands()` undoes.
 */
Parameter MakeParameter(const can::DirectNOD &nod, can::Id eId, Function eFunction,
                        unit::Key eUserKey, const Bands &bands, unit::Key eBandKey,
                        const char *pcLong, const char *pcShort, const char *pcTiny)
{
    const unit::Key eSystemKey = parameter::function_util::GetSystemUnit(eFunction);

    Parameter p(eId, eFunction, eSystemKey, 1,
                [&nod](can::Id eIdRead, uint8_t uIndex) { return nod.GetFloat(eIdRead, uIndex); });

    p.SetUnitKeyUser(eUserKey);
    p.GetNames().Set(pcLong, pcShort, pcTiny);
    p.GetBands() = bands.Convert(eBandKey, eSystemKey);
    return p;
}

} // namespace

// --------------------------------------------------------------------------

Parameters::Parameters(const can::DirectNOD &nod)
{
    /* Engine rpm. The first band carries no colour, so the scale draws no arc
     * below idle -- the same shape the tachometer had before. */
    Bands rpm;
    rpm.SetLow(0.0f);
    rpm.Append(1400.0f, Color::NoColor, 0.0f);
    rpm.Append(2500.0f, Color::Green,   0.0f);
    rpm.Append(2800.0f, Color::Yellow,  0.0f);
    rpm.Append(3000.0f, Color::Red,     0.0f);
    Insert(MakeParameter(nod, can::Id::EngineRPM_1, Function::EngineRPM,
                         unit::Key::RPM, rpm, unit::Key::RPM,
                         "Engine RPM", "RPM", "RPM"));

    /* Rotor rpm, the other half of a helicopter tachometer. Its interesting
     * band is narrow and sits high: a red no-go below the green, an overspeed
     * red above it. */
    Bands rotor;
    rotor.SetLow(0.0f);
    rotor.Append(380.0f, Color::NoColor, 0.0f);
    rotor.Append(420.0f, Color::Red,     0.0f);
    rotor.Append(480.0f, Color::Yellow,  0.0f);
    rotor.Append(540.0f, Color::Green,   0.0f);
    rotor.Append(600.0f, Color::Red,     0.0f);
    Insert(MakeParameter(nod, can::Id::RotorRPM_1, Function::RotorRPM,
                         unit::Key::RPM, rotor, unit::Key::RPM,
                         "Rotor RPM", "Rotor", "ROT"));

    /* Indicated airspeed. The red band is the Vne dash rather than an arc;
     * DrawArcIAS() picks that out of the last band itself. */
    Bands ias;
    ias.SetLow(40.0f);
    ias.Append( 75.0f, Color::NoColor, 0.0f);
    ias.Append(200.0f, Color::Green,   0.0f);
    ias.Append(250.0f, Color::Yellow,  0.0f);
    ias.Append(260.0f, Color::Red,     0.0f);
    Insert(MakeParameter(nod, can::Id::IndicatedAirspeed, Function::Airspeed,
                         unit::Key::km_h, ias, unit::Key::km_h,
                         "Indicated airspeed", "IAS", "IAS"));

    /* Altitude. One uncoloured band spanning a single revolution of the
     * hundreds pointer -- the altimeter wants a range, not colours. */
    Bands alt;
    alt.SetLow(0.0f);
    alt.Append(1000.0f, Color::NoColor, 0.0f);
    Insert(MakeParameter(nod, can::Id::BaroCorrectedAltitude, Function::Altitude,
                         unit::Key::feet, alt, unit::Key::feet,
                         "Altitude", "ALT", "ALT"));
}

// --------------------------------------------------------------------------

float Parameters::GetUserValue(can::Id eId) const
{
    const parameter::Parameter *pP = Find(eId);
    return pP != nullptr ? pP->GetValueUser() : 0.0f;
}

// --------------------------------------------------------------------------

parameter::Bands Parameters::GetUserBands(can::Id eId) const
{
    const parameter::Parameter *pP = Find(eId);
    return pP != nullptr ? pP->GetUserBands() : parameter::Bands();
}

// --------------------------------------------------------------------------

can::Id Parameters::ApplyPushedParameter(std::span<const uint8_t> sBlob)
{
    constexpr const char *TAG = "params";

    if (sBlob.empty()) return can::Id::Invalid;

    /* Verified upstream: CanProcessor checked the CRC-16 the sender packed
     * into the apply-buffer message before handing the bytes over. */
    const parameter::fbs::ParamItem *pItem =
        parameter::fbs::GetParamItem(sBlob.data());
    if (pItem == nullptr) {
        ESP_LOGW(TAG, "pushed buffer is not a ParamItem");
        return can::Id::Invalid;
    }

    const can::Id eId = can::Id(pItem->can_id());
    parameter::Parameter *pP = Find(eId);
    if (pP == nullptr) {
        /* A tool pushing a whole panel will name parameters we do not show.
         * That is not an error, it is just not ours. */
        ESP_LOGI(TAG, "pushed parameter for id %u, which this unit does not hold",
                 static_cast<unsigned>(pItem->can_id()));
        return can::Id::Invalid;
    }

    parameter::ParamStorage::ApplyTo(pP, pItem);
    SetIfDirty(true);

    ESP_LOGI(TAG, "parameter %u updated from the bus: %d bands, tc %.0f ms",
             static_cast<unsigned>(pItem->can_id()),
             pP->GetBands().GetCount(),
             static_cast<double>(pP->GetTimeConstant()));
    return eId;
}

} // namespace app
