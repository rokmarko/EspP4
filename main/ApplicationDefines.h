#pragma once

/**
 * @file ApplicationDefines.h
 * @brief This product's CAN identity and the CANaerospace services it uses.
 *
 * Common/CanAerospace expects every product to supply this header -- it is the
 * extension point where a unit declares who it is on the bus and which halves
 * of the optional services it implements. Every Kanardia product has its own
 * (Private/<product>/ApplicationDefines.h); this one is ours.
 *
 * Services are split into an A side (the one that asks or sends) and a B side
 * (the one that answers or receives).
 */

#include "KanardiaHardwareUnits.h"

/** Our node id on the bus. Overridable from the build. */
#ifndef CAN_NODE_ID
#define CAN_NODE_ID     KHU_INDU
#endif

/* Module information service, node A: ask other modules to identify. */
#define USE_CAN_MIS_A

/*
 * Data download service, both halves.
 *
 * B receives a buffer another node pushes at us; together with MCS_B below,
 * that is how a configuration tool loads a new parameter into this unit -- the
 * bytes arrive as a DDS_BUFFER download, then an MCS_APPLY_BUFFER_DATA message
 * commits them. Same pair Indu implements, which is why the same tooling can
 * talk to this board.
 *
 * A is the sending half, on for the same reason Nesis has it: that is how a
 * parameter is pushed *out*. `UnitInfoBase::Download(BufferDownloadCommand::
 * Parameter, blob)` is Nesis's one-liner for it, and CanProcessor::PushBuffer()
 * is the same sequence against the same services.
 */
#define USE_CAN_DDS_A
#define USE_CAN_DDS_B

/* Module configuration service. B answers configuration commands; A is what
 * sends the MCS_APPLY_BUFFER_DATA that commits a pushed buffer. */
#define USE_CAN_MCS_A
#define USE_CAN_MCS_B

/*
 * Application programming service, node B: accept a firmware update pushed
 * over CAN, the way every Kanardia unit is updated in the field.
 *
 * APS_B_NO_BOOTLOADER picks the variant that does not hand the transfer to a
 * uC bootloader -- there is none here. It buffers a 2 kB page in RAM, asks the
 * sender for the next one, checks the page CRC32 and calls the product back
 * with WriteUpdate() / FinishUpdate(). Those land on ESP-IDF's OTA API and the
 * ota_0 / ota_1 slots partitions.csv carries.
 *
 * The buffer is a member, so it is 8 kB of .bss; CONFIG_LV_DRAW_SW_DRAW_UNIT_CNT=1
 * is what makes room for it -- see sdkconfig.defaults.
 */
#define USE_CAN_APS_B
#define APS_B_NO_BOOTLOADER
