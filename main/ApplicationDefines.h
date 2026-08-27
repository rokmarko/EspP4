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
 * Services are split into an A side (the one that asks) and a B side (the one
 * that answers). This board only listens and asks, so only MIS_A is on: we
 * request module information from whatever we hear on the bus, and answer
 * nothing ourselves.
 */

#include "KanardiaHardwareUnits.h"

/** Our node id on the bus. Overridable from the build. */
#ifndef CAN_NODE_ID
#define CAN_NODE_ID     KHU_INDU
#endif

/* Module information service, node A: ask other modules to identify. */
#define USE_CAN_MIS_A
