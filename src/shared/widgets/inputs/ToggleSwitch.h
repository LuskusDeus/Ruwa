// SPDX-License-Identifier: MPL-2.0

// ToggleSwitch.h
#ifndef RUWA_UI_WIDGETS_COMMON_TOGGLESWITCH_H
#define RUWA_UI_WIDGETS_COMMON_TOGGLESWITCH_H

#include "shared/widgets/BaseStyledWidget.h"
#include <QEvent>

namespace ruwa::ui::widgets {

/**
 * @brief Animated toggle switch widget
 *
 * Uses BaseStyledWidget with "ToggleSwitch" style.
 * Features:
 * - Smooth on/off animation
 * - Animated thumb position
 * - Inverted thumb color (light when off, dark when on)
 * - Hover effects on track
 *
 * Can be used standalone or embedded in other widgets.
 */
class ToggleSwitch : public BaseStyledWidget {
    Q_OBJECT
    Q_PROPERTY(qreal thumbPosition READ thumbPosition WRITE setThumbPosition)

public:
    enum class TransitionMode { Animated, Instant };

    struct InitOptions {
        bool checked = false;
        bool enabled = true;
    };

    explicit ToggleSwitch(QWidget* parent = nullptr);
    explicit ToggleSwitch(bool initialState, QWidget* parent = nullptr);
    explicit ToggleSwitch(const InitOptions& options, QWidget* parent = nullptr);
    ~ToggleSwitch() override = default;

    /// Get/set checked state
    bool isChecked() const { return isActive(); }
    void setChecked(bool checked, TransitionMode mode = TransitionMode::Animated);
    void setCheckedInstant(bool checked) { setChecked(checked, TransitionMode::Instant); }

    /// Toggle state
    void toggle();

    /// Thumb animation property
    qreal thumbPosition() const { return m_thumbPosition; }
    void setThumbPosition(qreal position);

signals:
    /// Emitted when state changes
    void toggled(bool checked);

protected:
    void drawBackgroundLayer(QPainter& painter, const QRectF& rect) override;
    void drawBorderLayer(QPainter& painter, const QRectF& rect) override;
    void drawHoverLayer(QPainter& painter, const QRectF& rect) override;
    void drawActiveBackgroundLayer(QPainter& painter, const QRectF& rect) override;
    void drawActiveBorderLayer(QPainter& painter, const QRectF& rect) override;

    /// Custom content: draws the thumb circle
    void drawContentLayer(QPainter& painter, const QRectF& rect) override;

    /// Override click to toggle state
    void mousePressEvent(QMouseEvent* event) override;
    void changeEvent(QEvent* event) override;

private:
    void setupThumbAnimation();
    void animateThumb(bool checked, TransitionMode mode);

private:
    qreal m_thumbPosition = 0.0; // 0 = off (left), 1 = on (right)
    QPropertyAnimation* m_thumbAnimation = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_TOGGLESWITCH_H
