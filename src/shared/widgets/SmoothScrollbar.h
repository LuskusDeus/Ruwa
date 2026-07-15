// SPDX-License-Identifier: MPL-2.0

// SmoothScrollBar.h
#ifndef RUWA_UI_WIDGETS_COMMON_SMOOTHSCROLLBAR_H
#define RUWA_UI_WIDGETS_COMMON_SMOOTHSCROLLBAR_H

#include <QScrollBar>
#include <QPropertyAnimation>

class QTimer;

namespace ruwa::ui::widgets {

/**
 * @brief Custom scrollbar with smooth animations and modern design
 *
 * Features:
 * - Smooth fade in/out on hover
 * - Animated handle size on hover
 * - Modern minimal design
 * - Theme integration
 * - Auto-hide when not in use
 */
class SmoothScrollBar : public QScrollBar {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal visibilityProgress READ visibilityProgress WRITE setVisibilityProgress)

public:
    explicit SmoothScrollBar(Qt::Orientation orientation, QWidget* parent = nullptr);
    ~SmoothScrollBar() override;

    // Animation properties
    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    qreal visibilityProgress() const { return m_visibilityProgress; }
    void setVisibilityProgress(qreal progress);

    // Show/hide with animation
    void showAnimated();
    void hideAnimated();

    // Check if currently being dragged or buttons pressed
    bool isDragging() const { return m_isDragging; }
    bool isButtonPressed() const { return m_upButtonPressed || m_downButtonPressed; }

    /// When true, track area is transparent (parent card shows through). Default: false.
    void setTransparentTrack(bool transparent);
    bool transparentTrack() const { return m_transparentTrack; }

signals:
    /// Emitted when arrow buttons request a smooth scroll step (positive = down, negative = up)
    void stepScrollRequested(int delta);

protected:
    void paintEvent(QPaintEvent* event) override;
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;

private:
    QRect getHandleRect() const;
    int getHandleSize() const;
    QRect getUpButtonRect() const;
    QRect getDownButtonRect() const;
    void drawArrowButton(QPainter& painter, const QRect& rect, bool isUp, bool isPressed);
    void requestRepaint();

private:
    qreal m_hoverProgress { 0.0 };
    qreal m_visibilityProgress { 0.0 };
    bool m_isDragging { false };
    int m_dragStartPos { 0 };
    int m_dragStartValue { 0 };

    bool m_upButtonPressed { false };
    bool m_downButtonPressed { false };
    // Last non-empty handle geometry, so we can keep fading it out even after the
    // range collapses to 0 (maximum() == 0) when content stops needing a scrollbar.
    QRect m_cachedHandleRect;
    bool m_transparentTrack { false };
    QTimer* m_scrollRepeatTimer { nullptr };
    QTimer* m_scrollInitialDelayTimer { nullptr };

    QPropertyAnimation* m_hoverAnimation { nullptr };
    QPropertyAnimation* m_visibilityAnimation { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_SMOOTHSCROLLBAR_H
