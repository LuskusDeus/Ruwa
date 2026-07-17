// SPDX-License-Identifier: MPL-2.0

// WidgetFadeInOverlay.h
#ifndef RUWA_UI_WIDGETS_COMMON_WIDGETFADEINOVERLAY_H
#define RUWA_UI_WIDGETS_COMMON_WIDGETFADEINOVERLAY_H

#include <QWidget>
#include <QColor>
#include <QEasingCurve>
#include <QPixmap>

class QPropertyAnimation;

namespace ruwa::ui::widgets {

/**
 * @brief Overlay widget for fade-in animation effect
 *
 * Creates a colored overlay on top of a target widget and fades it out,
 * creating a smooth appearance effect. Auto-deletes after animation.
 *
 * Usage:
 *   auto* fadeIn = new WidgetFadeInOverlay(myWidget, backgroundColor);
 *   fadeIn->startAnimation(400); // 400ms fade-in
 *
 * Features:
 * - Automatically positions itself over target widget
 * - Matches target widget size (even during resize)
 * - Customizable duration and easing curve
 * - Auto-cleanup after animation completes
 */
class WidgetFadeInOverlay : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal opacity READ opacity WRITE setOpacity)

public:
    /**
     * @brief Create fade-in overlay for a widget
     * @param target Widget to overlay (must be visible)
     * @param backgroundColor Color of the overlay (usually theme background)
     * @param parent Retained for source compatibility; the target owns the overlay
     */
    explicit WidgetFadeInOverlay(
        QWidget* target, const QColor& backgroundColor, QWidget* parent = nullptr);

    ~WidgetFadeInOverlay() override = default;

    /**
     * @brief Show the overlay (call this when ready to display)
     *
     * Overlay is created hidden. Call showOverlay() to make it visible
     * at the exact moment you need it.
     */
    void showOverlay();

    /**
     * @brief Start the fade-in animation
     * @param durationMs Animation duration in milliseconds
     * @param delayMs Delay before starting animation (for cascade effects)
     * @param easingCurve Easing curve for animation (default: OutCubic)
     */
    void startAnimation(int durationMs = 400, int delayMs = 0,
        QEasingCurve::Type easingCurve = QEasingCurve::OutCubic);

    /// Get current opacity (0.0 = transparent, 1.0 = opaque)
    qreal opacity() const { return m_opacity; }

    /// Set opacity (used by animation)
    void setOpacity(qreal opacity);

signals:
    /// Emitted when animation finishes (just before auto-deletion)
    void animationFinished();

protected:
    void paintEvent(QPaintEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void updateGeometry();

private:
    QWidget* m_target = nullptr;
    QColor m_backgroundColor;
    QPixmap m_targetSnapshot;
    qreal m_opacity = 1.0;
    QPropertyAnimation* m_animation = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_WIDGETS_COMMON_WIDGETFADEINOVERLAY_H
