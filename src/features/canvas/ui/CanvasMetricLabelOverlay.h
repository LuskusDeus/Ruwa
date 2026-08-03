// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_UI_WIDGETS_CANVASMETRICLABELOVERLAY_H
#define RUWA_UI_WIDGETS_CANVASMETRICLABELOVERLAY_H

#include <QList>
#include <QPixmap>
#include <QPointF>
#include <QRectF>
#include <QString>
#include <QWidget>

#include <vector>

class QGraphicsOpacityEffect;
class QLabel;
class QPaintEvent;
class QPropertyAnimation;

namespace ruwa::ui::widgets {

/// One icon + value pair inside the capsule. A move readout uses two of them
/// (horizontal and vertical), rotation and scale one each. The icon is named by
/// resource path rather than passed as a pixmap so the capsule can re-tint it
/// itself when the theme changes.
struct MetricSegment {
    QString iconResource; ///< Empty for a text-only segment.
    bool mirrorHorizontally = false;
    bool mirrorVertically = false;
    QString text;
    /// Widest value this segment can show (e.g. "888.8%"). Reserves the value
    /// column, so the capsule keeps a steady width as digits come and go instead
    /// of twitching on every pixel of drag.
    QString widthTemplate;
};

/// Shared canvas metric capsule used by selection dimensions, transform snap
/// spacing and the live transform readouts.
class CanvasMetricLabelOverlay : public QWidget {
public:
    explicit CanvasMetricLabelOverlay(QWidget* parent = nullptr);
    ~CanvasMetricLabelOverlay() override;

    void presentAtPoint(const QString& text, const QPointF& anchorPanel);
    void presentNearRect(const QString& text, const QRectF& rectPanel);
    /// Park the capsule beside the cursor — to its right, flipping to the left
    /// when the right side has no room. For readouts the user watches while
    /// dragging, so their eyes stay on the pointer.
    void presentAtCursor(const QList<MetricSegment>& segments, const QPointF& cursorPanel);
    /// Re-anchor an already visible capsule without touching its content or
    /// stacking order. For callers that must follow a moving anchor every frame,
    /// where a full present() would re-raise over the canvas.
    void refreshNearRect(const QRectF& rectPanel);
    void refreshAtCursor(const QPointF& cursorPanel);
    void dismiss();
    void hideImmediately();

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    struct SegmentWidgets {
        QWidget* container = nullptr;
        QLabel* icon = nullptr;
        QLabel* text = nullptr;
        /// Icon identity currently on screen (resource + mirroring), so an
        /// unchanged icon is never re-tinted.
        QString iconKey;
    };

    void applyTheme();
    void setSegments(const QList<MetricSegment>& segments);
    /// Widgets are pooled and reused — never destroyed and re-created for a
    /// content change, which would make the whole capsule blink.
    void ensureSegmentCount(int count);
    /// Font, colour and spacing — everything a segment takes from the theme.
    void applySegmentStyling(SegmentWidgets& widgets);
    void applySegment(SegmentWidgets& widgets, const MetricSegment& segment);
    QPixmap themedIconPixmap(const MetricSegment& segment) const;
    void positionNearRect(const QRectF& rectPanel);
    void positionAtCursor(const QPointF& cursorPanel);
    void fadeTo(qreal opacity, int durationMs);
    void moveClamped(int x, int y);

    QList<MetricSegment> m_segments;
    std::vector<SegmentWidgets> m_segmentWidgets;
    QGraphicsOpacityEffect* m_opacityEffect = nullptr;
    QPropertyAnimation* m_fadeAnimation = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_CANVASMETRICLABELOVERLAY_H
