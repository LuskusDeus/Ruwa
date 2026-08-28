// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C U R S O R   O V E R L A Y   S T A T E
// ==========================================================================
// Renderer-neutral state models for the rendered cursor overlays (brush,
// eyedropper, tool, parameter circles). The state models live in the
// renderer-neutral workspace namespace; the `aether` aliases below only keep
// the legacy GL overlay internals building.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_OVERLAYS_CURSOROVERLAYSTATE_H
#define RUWA_FEATURES_CANVAS_OVERLAYS_CURSOROVERLAYSTATE_H

#include "features/canvas/overlays/ToolCursorIcons.h"

#include <QColor>
#include <QString>

#include <vector>

namespace ruwa::ui::workspace {

/**
 * @brief Surface-pixel rectangle a rendered cursor covers (origin top-left).
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

/// One extensible canvas-parameter circle, expressed in surface pixels.
struct ParameterCircleOverlayState {
    float centerX = 0.0f;
    float centerY = 0.0f;
    float radius = 0.0f;
    float hoverProgress = 0.0f;
    QColor primaryColor;
};

/**
 * @brief State for brush and eyedropper cursor overlays.
 */
struct CursorOverlayState {
    std::vector<ParameterCircleOverlayState> parameterCircles;

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

    /// Plain pointer, pointer + badge, or crosshair (see ToolCursorIcons.h).
    bool toolCursorVisible = false;
    float toolCursorCenterX = 0.0f;
    float toolCursorCenterY = 0.0f;
    ToolCursorStyle toolCursorStyle = ToolCursorStyle::PointerBadge;
    QString toolCursorIcon; ///< Engine-integration badge asset path.
};

} // namespace ruwa::ui::workspace

namespace aether {

using ruwa::ui::workspace::CursorCaptureRect;
using ruwa::ui::workspace::CursorOverlayState;
using ruwa::ui::workspace::ParameterCircleOverlayState;

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_OVERLAYS_CURSOROVERLAYSTATE_H
