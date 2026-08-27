/**
 * @file CanProcessor.cpp
 */

#include "CanProcessor.h"

#include "CanAerospace/MessageAerospace.h"

#include "esp_log.h"

namespace app {

namespace {
constexpr const char *TAG = "canproc";
} // namespace

// --------------------------------------------------------------------------

CanProcessor::CanProcessor(can::DirectNOD &nod, can::AbstractCanPort &port)
    : m_nod(nod)
    , m_port(port)
    , m_services(this)
{
    SetNodeId(CAN_NODE_ID);
    Units().SetOldService(&m_services);
    ESP_LOGI(TAG, "node id %u (CAN_NODE_ID=%u)",
             static_cast<unsigned>(GetNodeId()), static_cast<unsigned>(CAN_NODE_ID));
}

// --------------------------------------------------------------------------

bool CanProcessor::PostMessage(const can::Message &msg) const
{
    return m_port.Send(msg);
}

// --------------------------------------------------------------------------

bool CanProcessor::IsSpaceFor(uint32_t uMessages) const
{
    return m_port.IsSpaceFor(uMessages);
}

// --------------------------------------------------------------------------

#if defined(USE_CAN_MIS_A)
void CanProcessor::ProcessModuleInfoResponse(const can::Message &msg)
{
    /* Versions, SVN revision and the module-specific answers all land in the
     * unit container, which is what asked for them in the first place. */
    Units().ProcessModuleInfoResponse(msg);
}
#endif

// --------------------------------------------------------------------------

void CanProcessor::Process(const can::Message &msg)
{
    /* Same acceptance rule as CANHandler::Process(): ours, or broadcast. */
    const uint8_t uReceiver = msg.GetReceiver();
    if (uReceiver != GetNodeId() && uReceiver != can::EVERYBODY)
        return;

    const auto eId = msg.GetId();
    if (eId <= can::Id::LastValidService) {
        ProcessService(msg);
    }
    else if (eId <= can::Id::LastValidStatus) {
        m_uOther++;   /* status ids: nothing on this board consumes them yet */
    }
    else if (eId <= can::Id::LastValidNOD) {
        ProcessNOD(msg);
    }
    else {
        m_uOther++;
    }
}

// --------------------------------------------------------------------------

void CanProcessor::ProcessService(const can::Message &msg)
{
    m_uService++;

    /* Sign of life is what populates the unit container: every Kanardia module
     * announces itself, and the container asks the new ones to identify. */
    if (can::oldservice::GetServiceCode(msg) == can::oldservice::sSignOfLife) {
        Units().ProcessSignOfLife(msg);
        return;
    }

    m_services.Process(msg);
}

// --------------------------------------------------------------------------

void CanProcessor::ProcessNOD(const can::Message &msg)
{
    /* Exactly what Horis does: the data index is the service-code byte, and
     * register B carries the value. */
    m_uNod++;
    m_nod.Set(msg.GetId(), can::oldservice::GetServiceCode(msg), msg.GetRegisterB());
}

// --------------------------------------------------------------------------

void CanProcessor::Update1s()
{
    Units().DecrementUnitsAge();
    m_services.CheckTimeouts();
    m_services.Update();
}

// --------------------------------------------------------------------------

int CanProcessor::GetAliveUnitCount() const
{
    const auto &units = Units();
    int iCount = 0;
    for (int i = 0; i < units.GetCapacity(); i++) {
        const can::MiniUnitInfo *pUnit = units.GetInfo(static_cast<uint8_t>(i));
        if (pUnit != nullptr && pUnit->IsAlive()) iCount++;
    }
    return iCount;
}

// --------------------------------------------------------------------------

int CanProcessor::GetIdentifiedUnitCount() const
{
    return Units().GetUnitCount();
}

} // namespace app
