// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   Z O O M   I N F O   O V E R L A Y
// ==========================================================================

#ifndef RUWA_UI_WIDGETS_CANVASZOOMINFOOVERLAY_H
#define RUWA_UI_WIDGETS_CANVASZOOMINFOOVERLAY_H

#include <QString>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QPaintEvent;
class QPropertyAnimation;
class QTimer;

namespace ruwa::ui::widgets {

class CanvasZoomInfoOverlay : public QWidget {
public:
    explicit CanvasZoomInfoOverlay(QWidget* parent = nullptr);
    ~CanvasZoomInfoOverlay() override;

    void showZoom(qreal zoom);
    void setZoom(qreal zoom);
    bool isPresentationActive() const;
    void updateAnchorPosition();
    void hideImmediately();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void applyTheme();
    void fadeTo(qreal opacity, int durationMs);
    QString zoomText(qreal zoom) const;

    static constexpr int kFadeInDurationMs = 90;
    static constexpr int kFadeOutDurationMs = 140;
    static constexpr int kHideDelayMs = 500;

    QLabel* m_label = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_fadeAnimation = nullptr;
    QTimer* m_hideTimer = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CANVASZOOMINFOOVERLAY_H
