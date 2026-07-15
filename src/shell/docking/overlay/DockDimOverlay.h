// SPDX-License-Identifier: MPL-2.0

// DockDimOverlay.h
#ifndef RUWA_UI_DOCKING_OVERLAY_DOCKDIMOVERLAY_H
#define RUWA_UI_DOCKING_OVERLAY_DOCKDIMOVERLAY_H

#include "features/theme/manager/ThemeColors.h"

#include <QWidget>
#include <QPointer>

class QVariantAnimation;

namespace ruwa::ui::docking {

/**
 * @brief Semi-transparent overlay for dimming dock areas during drag
 *
 * Shows a smooth animated dimming effect over the target dock area
 * when a valid drop zone is detected. The overlay respects the
 * target widget's geometry and transformations.
 */
class DockDimOverlay : public QWidget {
    Q_OBJECT

public:
    explicit DockDimOverlay(QWidget* parent = nullptr);
    ~DockDimOverlay() override;

    // === State ===

    /// Show overlay for target widget with animation
    void showForTarget(QWidget* target);

    /// Hide overlay with animation
    void hideAnimated();

    /// Hide immediately without animation
    void hideImmediate();

    /// Check if overlay is visible or animating to visible
    bool isActiveOrShowing() const;

    // === Appearance ===

    /// Set dim color (alpha will be animated)
    void setDimColor(const QColor& color);
    QColor dimColor() const { return m_dimColor; }

    /// Set maximum opacity (0.0 - 1.0)
    void setMaxOpacity(qreal opacity);
    qreal maxOpacity() const { return m_maxOpacity; }

    /// Set animation duration in ms
    void setAnimationDuration(int ms);
    int animationDuration() const { return m_animationDuration; }

    void applyTheme(const ruwa::ui::core::ThemeColors& colors);

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onAnimationValueChanged(const QVariant& value);
    void onAnimationFinished();

private:
    void updateGeometryFromTarget();
    void startAnimation(qreal targetOpacity);

private:
    QPointer<QWidget> m_target;
    QPointer<QVariantAnimation> m_animation;

    QColor m_dimColor;
    qreal m_currentOpacity = 0.0;
    qreal m_maxOpacity = 0.4;
    int m_animationDuration = 150;

    bool m_hiding = false;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_OVERLAY_DOCKDIMOVERLAY_H
