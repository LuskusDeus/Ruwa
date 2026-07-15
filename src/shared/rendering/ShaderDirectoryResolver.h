// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   S H A D E R   D I R E C T O R Y   R E S O L V E R
// ==========================================================================

#ifndef RUWA_SHARED_RENDERING_SHADERDIRECTORYRESOLVER_H
#define RUWA_SHARED_RENDERING_SHADERDIRECTORYRESOLVER_H

#include "shared/types/Result.h"

#include <QString>

namespace aether {

Result<QString> resolveRuntimeShaderDirectory();

} // namespace aether

#endif // RUWA_SHARED_RENDERING_SHADERDIRECTORYRESOLVER_H
