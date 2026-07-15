// SPDX-License-Identifier: MPL-2.0

// DockCompassWidget.h
#ifndef RUWA_UI_DOCKING_OVERLAY_DOCKCOMPASSWIDGET_H
#define RUWA_UI_DOCKING_OVERLAY_DOCKCOMPASSWIDGET_H

#include "shell/docking/DockTypes.h"
#include "features/theme/manager/ThemeColors.h"

#include <QWidget>
#include <QVariantAnimation>
#include <QMap>

namespace ruwa::ui::docking {

/**
 * @brief Compass-style widget for selecting drop zones
 *
 * Displays a central "+" pattern with 4 directional arrows:
 *
 *        ┌───┐
 *        │ ▲ │
 *    ┌───┼───┼───┐
 *    │ ◄ │   │ ► │
 *    └───┼───┼───┘
 *        │ ▼ │
 *        └───┘
 *
 * Features:
 * - Black semi-transparent background (30%)
 * - White arrows that scale up on hover
 * - Smooth hover animations
 * - No border
 */
class DockCompassWidget : public QWidget {
    Q_OBJECT

public:
    explicit DockCompassWidget(QWidget* parent = nullptr);
    ~DockCompassWidget() override;

    // === Visibility Animation ===

    /// Show with fade-in animation
    void showAnimated();

    /// Hide with fade-out animation
    void hideAnimated();

    /// Hide immediately without animation
    void hideImmediate();

    /// Check if visible or animating to visible
    bool isActiveOrShowing() const;

    // === State ===

    /// Set highlighted zone (from external source like DockOverlay)
    void setHighlightedZone(DropZone zone);
    DropZone highlightedZone() const { return m_highlightedZone; }

    /// Get zone at local position
    DropZone zoneAt(const QPoint& localPos) const;

    /// Check if position is inside compass
    bool containsPoint(const QPoint& localPos) const;

    // === Appearance ===

    /// Size of compass widget
    int compassSize() const { return m_size; }
    void setCompassSize(int size);

    /// Size of each zone button
    int zoneSize() const { return m_zoneSize; }
    void setZoneSize(int size);

    /// Animation duration in ms
    void setAnimationDuration(int ms) { m_animationDuration = ms; }
    int animationDuration() const { return m_animationDuration; }

    void applyTheme(const ruwa::ui::core::ThemeColors& colors);

signals:
    void zoneHovered(DropZone zone);
    void zoneClicked(DropZone zone);

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void leaveEvent(QEvent* event) override;

private slots:
    void onOpacityAnimationValueChanged(const QVariant& value);
    void onOpacityAnimationFinished();

private:
    struct ZoneState {
        qreal hoverProgress = 0.0; // 0.0 = normal, 1.0 = fully hovered
        QVariantAnimation* animation = nullptr;
    };

    QRect zoneRect(DropZone zone) const;
    void drawZone(QPainter& painter, DropZone zone);
    void drawArrow(QPainter& painter, const QRect& rect, DropZone zone, qreal scale);
    void startHoverAnimation(DropZone zone, bool hovering);
    void ensureAnimation(DropZone zone);
    void startOpacityAnimation(qreal targetOpacity);

private:
    int m_size = 100; // Total widget size
    int m_zoneSize = 32; // Each zone button size
    int m_spacing = 4; // Space between zones
    int m_padding = 2; // Padding for antialiasing
    int m_animationDuration = 150; // ms

    DropZone m_highlightedZone = DropZone::None;

    // Per-zone animation state. All four entries are created eagerly in the
    // constructor so hover events never allocate.
    QMap<DropZone, ZoneState> m_zoneStates;

    // Colors
    QColor m_bgNormalColor; // Black with 30% opacity
    QColor m_bgHoverColor; // Dark gray
    QColor m_arrowColor; // White/text color
    int m_borderRadius = 6;

    // Widget opacity animation
    qreal m_widgetOpacity = 0.0;
    QVariantAnimation* m_opacityAnimation = nullptr;
    bool m_hiding = false;
    int m_opacityAnimationDuration = 150; // ms
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_OVERLAY_DOCKCOMPASSWIDGET_H
