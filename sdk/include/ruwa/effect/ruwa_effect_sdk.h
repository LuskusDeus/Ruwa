// SPDX-License-Identifier: MPL-2.0

/* ==========================================================================
 *   R U W A   |   E F F E C T   S D K   —   umbrella header
 * ==========================================================================
 *
 *   Include this single header to author a Ruwa effect plugin against ABI v1.
 *   It pulls in the foundation, GPU and plugin contracts. Nothing here depends
 *   on Qt, the STL or any private Ruwa header — a plugin builds with this
 *   include directory alone (that is the SDK-sufficiency guarantee, proved by
 *   the reference plugin).
 * ========================================================================== */

#ifndef RUWA_EFFECT_SDK_H
#define RUWA_EFFECT_SDK_H

#include "ruwa_effect_abi.h"
#include "ruwa_effect_gpu.h"
#include "ruwa_effect_plugin.h"

#endif /* RUWA_EFFECT_SDK_H */
