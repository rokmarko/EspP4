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
#include "BLOB/BLOB.h"
#include "CanPort/AbstractCanPort.h"

#include "Application/uCUnitInfoContainer.h"

#include <atomic>
#include <cstdint>
#include <mutex>
#include <vector>

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
#if defined(USE_CAN_DDS_B)
    /** Accept a DDS_BUFFER push that fits the scratch buffer; reject the rest. */
    bool AcceptDownload(uint32_t uiDataId, uint8_t byMsgCount) const override;
    /** File one 4-byte word of the pushed buffer at its message index. */
    int32_t StoreDownloadMessage(uint32_t uiDataId, const can::Message &msg) override;
#endif
#if defined(USE_CAN_MCS_B)
    /** Configuration commands. Only MCS_APPLY_BUFFER_DATA does anything here. */
    int32_t ConfigureModule(const can::Message &msg) override;
#endif
#if defined(USE_CAN_DDS_A)
    /** Serve one 4-byte word of the buffer we are pushing. Index is 1-based. */
    bool GetDownloadData(uint32_t uiDataId, uint8_t byIndex,
                         can::oldservice::DataType &eDataType,
                         can::Register &rData) const override;
#endif
#if defined(USE_CAN_MCS_A)
    /** The answer to our MCS_APPLY_BUFFER_DATA. Non-zero means it was refused. */
    void ProcessModuleConfigResponse(const can::Message &msg) override;
#endif

    /**
     * Push a buffer at another node, the way Nesis pushes one.
     *
     * This is `UnitInfoBase::Download(BufferDownloadCommand, blob)` in the
     * shared tree, and it does what that does: offer the bytes as a DDS_BUFFER
     * download, then commit them with an MCS_APPLY_BUFFER_DATA carrying the
     * length above a CRC-16 and the command in the data index. The difference
     * is that Nesis runs on a desktop and blocks through the transfer, while
     * here DDS_A drives itself -- Pump() sends the commit once the download
     * lands.
     *
     * @return false if a push is already running, or the offer could not be
     *         posted. Progress after that is reported by GetParameterPushCount()
     *         on the receiving side.
     */
    bool PushBuffer(uint8_t byNodeB, can::oldservice::BufferDownloadCommand eCmd,
                    common::SpanBLOB blob);

    /**
     * Drive the outgoing services. Call often -- every 50 ms is right.
     *
     * `OldServices::Update()` posts at most one download message per call, so
     * a several-hundred-byte push needs pumping rather than a once-a-second
     * poke. Each call sends until the controller's transmit queue is full.
     */
    void Pump();

    /** Whether a buffer push is still in flight. */
    bool IsPushActive() const;

    /**
     * Hand over a parameter another node pushed, if one is waiting.
     *
     * The bytes arrive on the port's receive thread, but applying them mutates
     * `parameter::Parameter` objects the LVGL task reads every frame --
     * including a vector resize. So the receive side only publishes the
     * verified buffer here, and whoever owns the parameters picks it up on
     * their own thread.
     *
     * @param vOut  filled with the flatbuffer, and left alone if none is ready.
     * @return true if a parameter was taken; the slot is empty afterwards.
     */
    bool TakePendingParameter(std::vector<uint8_t> &vOut);

    /** How many parameter pushes have been accepted since boot. */
    uint32_t GetParameterPushCount() const { return m_uParamPush; }

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

    /* OldServices is a plain state machine with no locking of its own, and it
     * is reached from two threads here: Process() runs on the port's receive
     * thread, Pump() and Update1s() on the model task. A half-sent download
     * whose response lands mid-Update() would corrupt DDS_A's index. */
    mutable std::mutex m_mxServices;

#if defined(USE_CAN_DDS_A)
    /* The buffer we are pushing out, one register per download message, and
     * what the commit message will have to say about it. */
    std::vector<can::Register> m_vSend;
    uint32_t m_uPushApply = 0;      /* (size << 16) | crc16 */
    uint8_t  m_uPushNode  = 0;
    uint8_t  m_uPushCmd   = 0;
    bool     m_bPushCommitPending = false;
#endif

#if defined(USE_CAN_DDS_B)
    /* Scratch for the pushed buffer. 255 messages is all a message code can
     * count, and each carries four bytes, so 1020 is the protocol's ceiling --
     * round up and nothing a sender can legally offer will be refused for
     * size. Indu gets by with 384. */
    static constexpr uint32_t BUFFER_SIZE = 1024;
    uint8_t m_abyBuffer[BUFFER_SIZE] = {};
#endif

    /* The verified parameter waiting to be applied, and its lock. Touched from
     * the receive thread and from whoever calls TakePendingParameter(). */
    mutable std::mutex    m_mxPending;
    std::vector<uint8_t>  m_vPending;
    std::atomic<uint32_t> m_uParamPush{0};

    std::atomic<uint32_t> m_uNod{0};
    std::atomic<uint32_t> m_uService{0};
    std::atomic<uint32_t> m_uOther{0};
};

} // namespace app
