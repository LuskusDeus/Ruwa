// SPDX-License-Identifier: MPL-2.0

// BaseAnimatedButton.h
#ifndef RUWA_UI_WIDGETS_COMMON_BASEANIMATEDBUTTON_H
#define RUWA_UI_WIDGETS_COMMON_BASEANIMATEDBUTTON_H

#include <QPushButton>
#include <QPropertyAnimation>

class QEnterEvent;
class QEvent;

namespace ruwa::ui::widgets {

/**
 * @brief Abstract base class for animated button-like widgets
 *
 * This class provides common functionality for all button widgets in the application:
 * - Hover animations (fade in/out)
 * - Active/selected state animations
 * - Mouse event handling
 * - Color interpolation utilities
 *
 * Derived classes only need to implement paintEvent() for custom rendering.
 *
 * Common features:
 * - hoverProgress: 0.0 (not hovered) to 1.0 (fully hovered)
 * - activeProgress: 0.0 (inactive) to 1.0 (active)
 * - Smooth transitions with configurable durations
 */
class BaseAnimatedButton : public QPushButton {
    Q_OBJECT
    Q_PROPERTY(qreal hoverProgress READ hoverProgress WRITE setHoverProgress)
    Q_PROPERTY(qreal activeProgress READ activeProgress WRITE setActiveProgress)

public:
    explicit BaseAnimatedButton(QWidget* parent = nullptr);
    ~BaseAnimatedButton() override = default;

    /// Set whether this button is active/selected
    void setActive(bool active);
    /// Set the active/selected state instantly, skipping the transition animation.
    /// Useful when a button should appear pre-selected on creation (e.g. the current
    /// tool in a freshly opened group popup) rather than animating into the state.
    void setActiveImmediate(bool active);
    bool isActive() const { return m_isActive; }

    /// Check if button is currently pressed
    bool isPressed() const { return m_isPressed; }
    bool isHovered() const { return m_isHovered; }

    // Animation properties
    qreal hoverProgress() const { return m_hoverProgress; }
    void setHoverProgress(qreal progress);

    qreal activeProgress() const { return m_activeProgress; }
    void setActiveProgress(qreal progress);

    /// Force hover state transition (useful for stylus-driven hover).
    void setHovered(bool hovered);

protected:
    /// Override these in derived classes for custom rendering
    void paintEvent(QPaintEvent* event) override = 0;

    /// Mouse events with base implementation (can be overridden)
    void enterEvent(QEnterEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void mouseReleaseEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;

    /// Configure animation durations (can be called in derived constructors)
    void setHoverDuration(int ms);
    void setActiveDuration(int ms);

    /// State flags accessible by derived classes
    bool m_isActive { false };
    bool m_isPressed { false };
    bool m_isHovered { false };

private:
    /// Animation state
    qreal m_hoverProgress { 0.0 };
    qreal m_activeProgress { 0.0 };

    /// Animations
    QPropertyAnimation* m_hoverAnimation { nullptr };
    QPropertyAnimation* m_activeAnimation { nullptr };
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_BASEANIMATEDBUTTON_H
