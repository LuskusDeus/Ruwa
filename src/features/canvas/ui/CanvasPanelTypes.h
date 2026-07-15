// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   T Y P E S
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_CANVASPANELTYPES_H
#define RUWA_UI_WORKSPACE_CANVASPANELTYPES_H

#include "features/brush/manager/BrushSettings.h"

#include <QColor>
#include <QString>
#include <Qt>

#include <cstdint>

namespace ruwa::ui::workspace {

enum class CanvasToolMode {
    Hand = 0,
    Brush = 1,
    Eraser = 2,
    Fill = 3,
    Eyedropper = 4,
    Lasso = 5,
    LassoFill = 6,
    SquareSelection = 7,
    CircleSelection = 8,
    Move = 9,
    RotateView = 10,
    CanvasResize = 11,
    Zoom = 12,
    ClassicFill = 13,
    Blur = 14,
    Text = 15,
    Smudge = 16,
    Liquify = 17
};

struct CanvasPersistedToolState {
    QString brushId;
    qreal brushSize = 0.3;
    qreal brushOpacity = 1.0;
    QColor color = QColor(0, 0, 0, 255);
    bool valid = false;
};

struct CanvasToolBrushStateSnapshot {
    QString brushId;
    qreal brushSize = 0.3;
    qreal brushOpacity = 1.0;
    QRgb colorRgba = QColor(0, 0, 0, 255).rgba();
    bool valid = false;
};

struct CanvasToolStateSnapshot {
    int currentTool = static_cast<int>(CanvasToolMode::Hand);
    int lastDrawTool = static_cast<int>(CanvasToolMode::Brush);
    QRgb currentColorRgba = QColor(0, 0, 0, 255).rgba();
    qreal lassoStabilization = 0.0;
    qreal lassoFillStabilization = 0.0;
    bool brushEraserActive = false;
    CanvasToolBrushStateSnapshot brush;
    CanvasToolBrushStateSnapshot eraser;
    CanvasToolBrushStateSnapshot blur;
    CanvasToolBrushStateSnapshot smudge;
};

struct CanvasToolBrushState {
    QString brushId;
    ruwa::core::brushes::BrushSettingsData settings;
    qreal brushSize = 0.3;
    qreal brushOpacity = 1.0;
    struct {
        uint8_t r = 0;
        uint8_t g = 0;
        uint8_t b = 0;
        uint8_t a = 255;
    } color;
    bool valid = false;
};

struct CanvasTemporaryToolHold {
    bool active = false;
    CanvasToolMode previousTool = CanvasToolMode::Hand;
    int heldKey = 0;
    Qt::MouseButton heldButton = Qt::NoButton;
    bool toolWasUsed = false;
    bool alwaysRevert = false;
    bool shiftSpaceCombo = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_CANVASPANELTYPES_H
