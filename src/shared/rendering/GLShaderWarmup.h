// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   G L   S H A D E R   W A R M U P
// ==========================================================================

#ifndef RUWA_SHARED_RENDERING_GLSHADERWARMUP_H
#define RUWA_SHARED_RENDERING_GLSHADERWARMUP_H

#include "shared/types/Result.h"

#include <functional>
#include <QString>

class QOffscreenSurface;
class QOpenGLContext;

namespace aether {

Result<void> warmUpOpenGLShaderPrograms(QOpenGLContext* context, QOffscreenSurface* surface,
    const std::function<void(const QString&, int)>& progressCallback = {});

} // namespace aether

#endif // RUWA_SHARED_RENDERING_GLSHADERWARMUP_H
