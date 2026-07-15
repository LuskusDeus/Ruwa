// SPDX-License-Identifier: MPL-2.0

// DockOverlay.h
#ifndef RUWA_UI_DOCKING_OVERLAY_DOCKOVERLAY_H
#define RUWA_UI_DOCKING_OVERLAY_DOCKOVERLAY_H

#include "shell/docking/DockTypes.h"
#include "features/theme/manager/ThemeColors.h"

#include <QWidget>
#include <QRect>
#include <QPointer>
#include <QTimer>
#include <QElapsedTimer>

namespace ruwa::ui::docking {

class DockContainerWidget;
class DockPanel;
class DockCompassWidget;
class DropZoneIndicator;
class DockDimOverlay;
class DockFloatingContainer;

/**
 * @brief Overlay widget shown during drag & drop operations
 *
 * Drop zones can be triggered either by:
 * 1. Clicking compass buttons (explicit)
 * 2. Hovering over area regions (implicit - 30% edge zones)
 *
 * Protected against:
 * - Invalid target areas
 * - Re-entrant updates
 * - Null pointer access
 */
class DockOverlay : public QWidget {
    Q_OBJECT

public:
    explicit DockOverlay(DockContainerWidget* container);
    ~DockOverlay() override;

    // === Drag State ===

    void showForContainer(); // Show for entire container
    void updateDropZone(const QPoint& globalPos);
    void hideOverlay();

    DropZone currentDropZone() const { return m_currentZone; }
    DockPanel* targetPanel() const { return m_targetPanel.data(); }
    bool hasValidDropZone() const { return m_currentZone != DropZone::None; }
    bool isContainerMode() const { return m_containerMode; }

    /// Check if overlay is in valid state
    bool isActive() const { return isVisible(); }

    /// Set floating container to keep on top during drag
    void setFloatingContainer(DockFloatingContainer* container);

    // === Appearance ===

    void applyTheme(const ruwa::ui::core::ThemeColors& colors);

signals:
    void dropZoneChanged(DropZone zone);

protected:
    void paintEvent(QPaintEvent* event) override;

private:
    void updateDropIndicator();
    void updateIndicatorGeometry(); // Update geometry without changing state
    QRect calculateDropRect(DropZone zone) const;
    DropZone zoneAtContainerEdge(const QPoint& localPos) const;
    DropZone zoneAtContainerPosition(const QPoint& localPos) const; // For container mode
    DropZone zoneAtPosition(const QPoint& localPos) const;

private:
    QPointer<DockContainerWidget> m_container;
    QPointer<DockPanel> m_targetPanel; // Panel under cursor in container mode
    QPointer<DockFloatingContainer> m_floatingContainer;

    // Widgets
    DockCompassWidget* m_compass = nullptr;
    DockCompassWidget* m_compassFading = nullptr; // Fading out (previous)
    DropZoneIndicator* m_dropIndicator = nullptr; // Current/appearing
    DropZoneIndicator* m_dropIndicatorFading = nullptr; // Fading out (previous)
    DockDimOverlay* m_dimOverlay = nullptr;
    DockDimOverlay* m_dimOverlayFading = nullptr; // Fading out (previous)

    // State
    DropZone m_currentZone = DropZone::None;
    DropZone m_prevZone = DropZone::None;
    QRect m_prevDropRect;
    bool m_inUpdate = false;
    bool m_containerMode = false; // Always container mode (whole-container drop zones)

    // Indicator cooldown (prevents flickering at zone boundaries)
    QElapsedTimer m_indicatorCooldownTimer;
    bool m_indicatorCooldownActive = false;
    int m_indicatorCooldownMs = 200; // 0.2 sec

    // Cursor speed throttle (suppresses indicators while the panel is dragged too fast).
    // Same idea as TransformController::SHIFT_MOVE_AXIS_SWITCH_MAX_SPEED_SCREEN_PX_PER_SEC.
    QElapsedTimer m_cursorSpeedTimer;
    QPoint m_lastCursorPos;
    bool m_cursorSpeedSampleValid = false;
    static constexpr float kMaxCursorSpeedPxPerSec = 1400.0f;

    // Geometry update timer (for animating targets). Auto-stops once the
    // target widget's geometry has been stable for kGeometryStableFrames
    // consecutive ticks; re-armed on cursor moves and zone changes via
    // armGeometryTimer(). This avoids the ~60Hz idle wake-up while the user
    // is hovering with no layout animation in progress.
    QTimer* m_geometryUpdateTimer = nullptr;
    QRect m_lastTargetGeometry;
    int m_geometryStableTicks = 0;
    static constexpr int kGeometryStableFrames = 5; // ~80ms at 16ms tick

    void armGeometryTimer();

    // Theme
    ruwa::ui::core::ThemeColors m_colors;

    // Helper
    // force=true (default): structural callers (show / zone change / swap) that
    // genuinely changed the widget set and must fix the z-order.
    // force=false: the per-frame geometry poll, which is skipped — during steady
    // drag movement nothing re-stacks, and calling raise()/stackUnder() each
    // frame forces the QOpenGLWidget canvas under the panel to recomposite.
    void raiseFloatingContainer(bool force = true);
    void swapIndicators();
    void swapCompasses();
    void updateTargetPanel(const QPoint& globalPos);
    void updateCompassForTargetPanel();
    DropZone zoneAtPanelPosition(const QPoint& globalPos) const;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_OVERLAY_DOCKOVERLAY_H
