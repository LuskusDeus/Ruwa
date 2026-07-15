// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   P O S I T I O N   P I C K E R   O V E R L A Y
// ==========================================================================

#ifndef RUWA_UI_WIDGETS_CANVASPOSITIONPICKEROVERLAY_H
#define RUWA_UI_WIDGETS_CANVASPOSITIONPICKEROVERLAY_H

#include <QPoint>
#include <QPointF>
#include <QWidget>

class QLabel;
class QPaintEvent;

namespace ruwa::ui::widgets {

/// Small cursor-following capsule shown while CanvasPanel::beginPositionPicking
/// is active: reads "X: 123  Y: 456" for the document-pixel point that would
/// be picked if the user clicked right now. Purely informational — ignores
/// mouse input, same as CanvasZoomInfoOverlay.
class CanvasPositionPickerOverlay : public QWidget {
public:
    explicit CanvasPositionPickerOverlay(QWidget* parent = nullptr);

    void setDocumentPosition(const QPointF& pos);

    /// Places the capsule just below-right of \a localCursorPos (parent-local
    /// coordinates), clamped to stay inside the parent's bounds.
    void followCursor(const QPoint& localCursorPos);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void applyTheme();

    QLabel* m_label = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CANVASPOSITIONPICKEROVERLAY_H
