// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P A N E L   T Y P E S
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_CANVASPANELTYPES_H
#define RUWA_UI_WORKSPACE_CANVASPANELTYPES_H

#include "features/brush/manager/BrushSettings.h"
#include "shared/types/ToolId.h"

#include <QColor>
#include <QString>
#include <Qt>

#include <cstdint>

namespace ruwa::ui::workspace {

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
    int currentTool = persistentValueForToolId(ToolId::Brush);
    int lastDrawTool = persistentValueForToolId(ToolId::Brush);
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
    ToolId previousTool = ToolId::Hand;
    int heldKey = 0;
    Qt::MouseButton heldButton = Qt::NoButton;
    bool toolWasUsed = false;
    bool alwaysRevert = false;
    bool shiftSpaceCombo = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_CANVASPANELTYPES_H
