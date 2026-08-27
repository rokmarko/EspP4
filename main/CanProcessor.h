#pragma once

/**
 * @file CanProcessor.h
 * @brief The CANaerospace side of the bus: classify what arrives and file it.
 *
 * This mirrors can::CANHandler::Process() -- incoming messages split by id
 * range into services, status, NOD and special -- without the CANHandler
 * template itself, which needs a product's whole sender/service stack. What it
 * does use from Common is the part that matters:
 *
 *   - can::oldservice::OldServices for the module-information service, so the
 *     units on the bus can be asked to identify themselves;
 *   - can::uCUnitInfoContainer, the microcontroller-side unit container, which
 *     tracks who is alive from their sign-of-life messages;
 *   - can::DirectNOD for the network object data, which is what the flight
 *     model reads.
 *
 * Everything runs on the port's receive thread. DirectNOD is mutex-protected,
 * so the UI may read it from the LVGL task at any time.
 */

#include "KanardiaCommon.h"
#include "ApplicationDefines.h"

#include "CanAerospace/AbstractUnit.h"
#include "CanAerospace/CanNOD.h"
#include "CanAerospace/CanOldServices.h"
#include "CanPort/AbstractCanPort.h"

#include "Application/uCUnitInfoContainer.h"

#include <atomic>

namespace app {

class CanProcessor : public can::oldservice::AbstractUnit
{
public:
    CanProcessor(can::DirectNOD &nod, can::AbstractCanPort &port);

    /** Called on the port's receive thread, once per accepted frame. */
    void Process(const can::Message &msg);

    /** Once per second: age the unit table and let the services time out. */
    void Update1s();

    /* --- can::oldservice::AbstractUnit ---------------------------------- */
    bool PostMessage(const can::Message &msg) const override;
    bool IsSpaceFor(uint32_t uMessages) const override;
#if defined(USE_CAN_MIS_A)
    void ProcessModuleInfoResponse(const can::Message &msg) override;
#endif

    /** The units heard on the bus. */
    static can::uCUnitInfoContainer &Units()
    { return *can::uCUnitInfoContainer::GetInstance(); }

    uint32_t GetNodCount() const     { return m_uNod; }
    uint32_t GetServiceCount() const { return m_uService; }
    uint32_t GetOtherCount() const   { return m_uOther; }
    /**
     * Units heard from recently -- a sign-of-life inside the last few seconds.
     *
     * Distinct from GetIdentifiedUnitCount(): the container only calls a unit
     * "valid" once it has answered the module-information request with its
     * hardware, software and SVN versions. Nothing answers here, because this
     * board implements only the asking half of the service (MIS_A), so that
     * count stays at zero on a bus of one.
     */
    int      GetAliveUnitCount() const;
    /** Units that have answered the module-information request in full. */
    int      GetIdentifiedUnitCount() const;

private:
    void ProcessService(const can::Message &msg);
    void ProcessNOD(const can::Message &msg);

    can::DirectNOD           &m_nod;
    can::AbstractCanPort     &m_port;
    can::oldservice::OldServices m_services;

    std::atomic<uint32_t> m_uNod{0};
    std::atomic<uint32_t> m_uService{0};
    std::atomic<uint32_t> m_uOther{0};
};

} // namespace app
