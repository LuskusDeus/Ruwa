// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C U R S O R   O V E R L A Y   S T A T E
// ==========================================================================
// Holds brush and eyedropper cursor overlay state for rendering.
// ==========================================================================

#ifndef RUWA_FEATURES_CANVAS_OVERLAYS_CURSOROVERLAYSTATE_H
#define RUWA_FEATURES_CANVAS_OVERLAYS_CURSOROVERLAYSTATE_H

#include <QString>

namespace aether {

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

    /// Pointer + tool badge cursor (fill, move, magic wand, lasso tools).
    bool toolCursorVisible = false;
    float toolCursorCenterX = 0.0f;
    float toolCursorCenterY = 0.0f;
    QString toolCursorIcon; ///< QRC path of the badge icon.
};

} // namespace aether

#endif // RUWA_FEATURES_CANVAS_OVERLAYS_CURSOROVERLAYSTATE_H
