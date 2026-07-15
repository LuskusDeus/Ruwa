// SPDX-License-Identifier: MPL-2.0

// DropZoneIndicator.h
#ifndef RUWA_UI_DOCKING_OVERLAY_DROPZONEINDICATOR_H
#define RUWA_UI_DOCKING_OVERLAY_DROPZONEINDICATOR_H

#include "shell/docking/DockTypes.h"
#include "features/theme/manager/ThemeColors.h"

#include <QWidget>
#include <QRect>
#include <QPixmap>
#include <QPointer>

class QVariantAnimation;

namespace ruwa::ui::docking {

/**
 * @brief Visual indicator showing where a panel will be dropped
 *
 * Displays a semi-transparent highlighted area that animates
 * sliding out from the edge where the panel will be placed.
 */
class DropZoneIndicator : public QWidget {
    Q_OBJECT

public:
    explicit DropZoneIndicator(QWidget* parent = nullptr);
    ~DropZoneIndicator() override;

    // === State ===

    /// Show indicator for drop zone with slide-in animation
    void showForZone(const QRect& targetRect, DropZone zone);

    /// Hide indicator with slide-out animation
    void hideIndicator();

    /// Hide indicator immediately without animation
    void hideImmediate();

    /// Current zone
    DropZone currentZone() const { return m_zone; }

    // === Appearance ===

    void applyTheme(const ruwa::ui::core::ThemeColors& colors);

    /// Animation duration in ms
    void setAnimationDuration(int ms) { m_animationDuration = ms; }
    int animationDuration() const { return m_animationDuration; }

protected:
    void paintEvent(QPaintEvent* event) override;

private slots:
    void onAnimationValueChanged(const QVariant& value);
    void onAnimationFinished();

private:
    void startAnimation(bool showing);
    QRect calculateAnimatedRect(qreal progress) const;
    void captureGlassBackdrop();

private:
    DropZone m_zone = DropZone::None;
    QRect m_targetRect; // Final target rectangle (in parent overlay coords)

    // Animation
    QPointer<QVariantAnimation> m_animation;
    qreal m_animationProgress = 0.0;
    int m_animationDuration = 150;
    bool m_hiding = false;

    // Colors
    QColor m_fillColor;

    // Tinted-glass backdrop. Captured once for the final target rect when the
    // indicator appears (or when the target changes); during the slide-in
    // animation the widget geometry alone clips the visible portion, anchored
    // to the final position.
    QPixmap m_glassBackdrop;
    QRect m_glassBackdropRect; // m_targetRect at the time of capture
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_OVERLAY_DROPZONEINDICATOR_H
