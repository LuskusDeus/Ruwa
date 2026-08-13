// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C U R S O R   O V E R L A Y   S T A T E
// ==========================================================================
// Holds brush and eyedropper cursor overlay state for rendering.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_OVERLAYS_CURSOROVERLAYSTATE_H
#define RUWA_FEATURES_CANVAS_OVERLAYS_CURSOROVERLAYSTATE_H

#include "features/canvas/overlays/ToolCursorIcons.h"

#include <QString>

namespace aether {

/**
 * @brief Surface-pixel rectangle a GL cursor covers (origin top-left).
 *
 * Every cursor overlay inverts what is under it by sampling the scene texture
 * at gl_FragCoord, so the rectangle it draws into is also the only part of the
 * scene it reads. That lets the frame render straight to the screen and hand
 * the cursor a copy of just this rectangle, instead of routing the whole scene
 * through an offscreen render + full-surface blit.
 */
struct CursorCaptureRect {
    float left = 0.0f;
    float top = 0.0f;
    float right = 0.0f;
    float bottom = 0.0f;
};

/**
 * @brief State for brush and eyedropper cursor overlays.
 */
struct CursorOverlayState {
    bool brushVisible = false;
    float brushCenterX = 0.0f;
    float brushCenterY = 0.0f;
    float brushRadius = 0.0f;

    bool eyedropperVisible = false;
    float eyedropperCenterX = 0.0f;
    float eyedropperCenterY = 0.0f;
    float eyedropperSelectedR = 0.0f;
    float eyedropperSelectedG = 0.0f;
    float eyedropperSelectedB = 0.0f;
    float eyedropperSelectedA = 1.0f;

    /// Pointer + badge or crosshair cursor, per tool (see ToolCursorIcons.h).
    bool toolCursorVisible = false;
    float toolCursorCenterX = 0.0f;
    float toolCursorCenterY = 0.0f;
    ToolCursorStyle toolCursorStyle = ToolCursorStyle::PointerBadge;
    QString toolCursorIcon; ///< QRC path of the badge icon.
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_OVERLAYS_CURSOROVERLAYSTATE_H
