// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   S E L E C T I O N   S I Z E   O V E R L A Y
// ==========================================================================
// Small capsule panel that reports the pixel resolution (W x H) of the
// rectangular selection currently being dragged, shown live until release.
// ==========================================================================

#ifndef RUWA_UI_WIDGETS_CANVASSELECTIONSIZEOVERLAY_H
#define RUWA_UI_WIDGETS_CANVASSELECTIONSIZEOVERLAY_H

#include <QRectF>
#include <QString>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QPaintEvent;
class QPropertyAnimation;

namespace ruwa::ui::widgets {

class CanvasSelectionSizeOverlay : public QWidget {
public:
    explicit CanvasSelectionSizeOverlay(QWidget* parent = nullptr);
    ~CanvasSelectionSizeOverlay() override;

    /// Set the reported dimensions, anchor the capsule next to the live
    /// selection rectangle (panel-local coords), and fade in if hidden.
    void present(int widthPx, int heightPx, const QRectF& selectionRectPanel);
    /// Fade out and hide (called on selection release).
    void dismiss();
    /// Hide with no animation (interaction reset / teardown).
    void hideImmediately();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void applyTheme();
    void fadeTo(qreal opacity, int durationMs);
    void moveNearRect(const QRectF& selectionRectPanel);

    static constexpr int kFadeInDurationMs = 90;
    static constexpr int kFadeOutDurationMs = 120;

    QLabel* m_label = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_fadeAnimation = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CANVASSELECTIONSIZEOVERLAY_H
