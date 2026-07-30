// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_CANVASMETRICLABELOVERLAY_H
#define RUWA_UI_WIDGETS_CANVASMETRICLABELOVERLAY_H

#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>

class QGraphicsOpacityEffect;
class QLabel;
class QPaintEvent;
class QPropertyAnimation;

namespace ruwa::ui::widgets {

/// Shared canvas metric capsule used by selection dimensions and transform snap spacing.
class CanvasMetricLabelOverlay : public QWidget {
public:
    explicit CanvasMetricLabelOverlay(QWidget* parent = nullptr);
    ~CanvasMetricLabelOverlay() override;

    void presentAtPoint(const QString& text, const QPointF& anchorPanel);
    void presentNearRect(const QString& text, const QRectF& rectPanel);
    void dismiss();
    void hideImmediately();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void applyTheme();
    void setMetricText(const QString& text);
    void fadeTo(qreal opacity, int durationMs);
    void moveClamped(int x, int y);

    QLabel* m_label = nullptr;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_fadeAnimation = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CANVASMETRICLABELOVERLAY_H
