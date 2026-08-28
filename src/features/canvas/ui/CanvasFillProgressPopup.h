// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   F I L L   P R O G R E S S   P O P U P
// ==========================================================================
// Application-owned presentation of engine fill activity (plan 7.15.2 /
// 7.14.5). The engine publishes CanvasFillActivityState facts; everything
// user-facing here is UI policy: the ~2 second classic-fill wait delay, the
// wording, the theme, the fade animation and the anchor clamping.
//
// The widget itself moved out of the legacy renderer (where it was the
// FillProgressPopupWidget class inside OpenGLCanvasWidget.cpp). The renderer's
// never-called "Done!" morph state was dropped in the move; translation
// strings deliberately keep the "OpenGLCanvasWidget" context so existing
// translations keep matching.
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_CANVASFILLPROGRESSPOPUP_H
#define RUWA_UI_WORKSPACE_CANVASFILLPROGRESSPOPUP_H

#include "features/canvas/engine/CanvasEngineTypes.h"

#include <QPoint>
#include <QWidget>

#include <functional>
#include <optional>

class QGraphicsOpacityEffect;
class QLabel;
class QPropertyAnimation;
class QTimer;

namespace ruwa::ui::widgets {
class DotGridLoadingIndicator;
}

namespace ruwa::ui::workspace {

class CanvasFillProgressPopup final : public QWidget {
    Q_OBJECT

public:
    explicit CanvasFillProgressPopup(QWidget* parent = nullptr);

    /// Map a document point to viewport-local coordinates for anchoring.
    using DocumentToViewport = std::function<QPointF(const QPointF&)>;
    void setDocumentToViewport(DocumentToViewport mapper);

    /// Consume one fill activity snapshot. Only the classic-fill wait state
    /// surfaces a popup, matching the presentation this replaces: progressive
    /// fills show their own live preview, and no fill shows anything idle.
    void presentFillActivity(const CanvasFillActivityState& state);
    /// Re-anchor while visible (view moved/zoomed under the fill origin).
    void updateAnchor();

private:
    void showProcessingAt(const QPoint& anchorPoint, const QString& text, int textWidth);
    void updateTheme();
    void applyStateSizing();
    QPoint anchorPointForFillOrigin() const;
    QPoint popupTopLeftForAnchor(const QPoint& anchorPoint, const QSize& popupSize) const;
    void startShow(const QPoint& topLeft);
    void startHide();
    void hideImmediate();

    bool isProcessingVisible() const;

    DocumentToViewport m_documentToViewport;
    CanvasFillActivityState m_lastActivity;
    QTimer* m_waitDelayTimer = nullptr;
    bool m_waitPopupVisible = false;

    ruwa::ui::widgets::DotGridLoadingIndicator* m_indicator = nullptr;
    QLabel* m_label = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_opacityAnim = nullptr;
    QPropertyAnimation* m_posAnim = nullptr;
    int m_processingTextWidth = 400;
    bool m_processingState = false;
    bool m_isHiding = false;
    int m_transitionToken = 0;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_CANVASFILLPROGRESSPOPUP_H
