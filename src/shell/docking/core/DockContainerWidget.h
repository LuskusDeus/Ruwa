// SPDX-License-Identifier: MPL-2.0

// DockContainerWidget.h
#ifndef RUWA_UI_DOCKING_CORE_DOCKCONTAINERWIDGET_H
#define RUWA_UI_DOCKING_CORE_DOCKCONTAINERWIDGET_H

#include "shell/docking/DockTypes.h"
#include "features/theme/manager/ThemeColors.h"
#include "shell/docking/state/DockLayoutPreset.h"

#include <QWidget>
#include <QElapsedTimer>
#include <QList>
#include <QMap>
#include <QPointer>
#include <QSet>

class QTimer;
#include <memory>
#include <optional>

namespace ruwa::ui::docking {

class DockManager;
class DockPanel;
class DockFloatingContainer;
class DockOverlay;
class DockLayoutRoot;
class DockPanelEntranceOverlay;

/**
 * @brief Main container widget for the docking system
 *
 * All operations are protected against:
 * - Null pointer access (using QPointer)
 * - Re-entrant modifications
 * - Invalid state during destruction
 * - Race conditions with floating containers
 */
class DockContainerWidget : public QWidget {
    Q_OBJECT

public:
    explicit DockContainerWidget(QWidget* parent = nullptr);
    ~DockContainerWidget() override;

    // === Panels ===

    void addPanel(DockPanel* panel, DockPosition position);
    void addPanelRelativeTo(DockPanel* panel, DockPanel* relativeTo, DockPosition position);
    void removePanel(DockPanel* panel);

    QList<DockPanel*> dockedPanels() const;
    QList<DockPanel*> floatingPanels() const;
    QList<DockPanel*> allPanels() const;
    DockPanel* findPanel(const DockPanelId& id) const;

    /// Check if panel exists in this container
    bool containsPanel(DockPanel* panel) const;

    /// Get placement info for a docked panel (for restoring after hide)
    std::optional<PanelPlacement> getPanelPlacement(DockPanel* panel) const;

    // === Floating ===

    void floatPanel(DockPanel* panel, const QPoint& pos, bool exactPosition = false);
    void dockPanel(DockPanel* panel, DockPosition position);
    void dockPanelRelativeTo(DockPanel* panel, DockPanel* relativeTo, DockPosition position);

    /**
     * @brief Drop a panel into another panel's cell as a tab group
     *
     * Falls back to dockPanelRelativeTo(Right) if the two panels turn out not
     * to be groupable, so a caller never has to pre-validate.
     */
    void dockPanelIntoGroup(DockPanel* panel, const DockDropTarget& target);

    /**
     * @brief Take a panel out of its tab group into a cell of its own
     *
     * Docks it to the right of the group it leaves. If that leaves the group
     * with a single member, the group collapses and its header animates away.
     *
     * @return false if the panel is not currently grouped
     */
    bool ungroupPanel(DockPanel* panel);

    QList<DockFloatingContainer*> floatingContainers() const { return m_floatingContainers; }

    // === Layout System ===

    /// Get the node-based layout root
    DockLayoutRoot* layoutRoot() const { return m_layoutRoot.get(); }

    /// Repair broken tree/layout state caused by invalid restore or geometry drift.
    bool repairDockLayout();

    /// Force a panel that already exists in the dock tree into a visible docked state.
    void restoreDockedPanel(DockPanel* panel);

    // === Overlay ===

    DockOverlay* overlay() const { return m_overlay; }

    // === Animation Settings ===

    /// Enable/disable animations for dock operations
    void setAnimationsEnabled(bool enabled);
    bool animationsEnabled() const { return m_animationsEnabled; }

    /// Animation duration in milliseconds
    void setAnimationDuration(int ms);
    int animationDuration() const { return m_animationDuration; }

    /**
     * Capture the settled docked panels for a visual-only entrance animation.
     * The stationary panel (normally the canvas) remains visible and is not captured.
     * No panel geometry or dock-layout state is changed.
     */
    bool preparePanelEntranceAnimation(DockPanel* stationaryPanel);

    /// Start a previously prepared entrance animation.
    bool startPanelEntranceAnimation();

    /// Drop a prepared/running entrance overlay and reveal the settled panels.
    void cancelPanelEntranceAnimation();

    // === Container Padding ===

    /// Set padding around container edges (space between panels and container border)
    void setContainerPadding(int padding);
    int containerPadding() const { return m_containerPadding; }

    // === Canvas frame throttling ===

    /**
     * @brief Report one composited canvas frame (wired from CanvasPanel).
     *
     * Qt repaints every widget overlapping a QOpenGLWidget on each of its frames, so
     * floating panels are fully redrawn at canvas frame rate. While frames keep
     * streaming (stroke, pan, zoom, transform) the panels are parked behind a cached
     * snapshot; they come back as soon as frames stop or the cursor reaches one.
     * Must stay cheap — this runs at display refresh rate.
     */
    void notifyCanvasFrame();

    // === Theme ===

    void applyTheme(const ruwa::ui::core::ThemeColors& colors);

signals:
    void panelAdded(DockPanel* panel);
    void panelRemoved(DockPanel* panel);
    void panelFloated(DockPanel* panel);
    void panelDocked(DockPanel* panel);
    void layoutChanged();

    /// Forwarded from a group header (via DockLayoutRoot) for DockManager,
    /// which owns panel lifetime and drag state.
    void groupPanelCloseRequested(DockPanel* panel);
    void groupPanelDragStarted(DockPanel* panel, const QPoint& globalPos);
    void groupPanelDragMoved(DockPanel* panel, const QPoint& globalPos);
    void groupPanelDragFinished(DockPanel* panel, const QPoint& globalPos);

    void panelEntranceAnimationFinished();
    void panelEntranceAnimationCancelled();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    friend class DockManager;

    void setupUI();
    void createOverlay();
    DockFloatingContainer* createFloatingContainer(DockPanel* panel);
    void removeFloatingContainer(DockFloatingContainer* container);

    /**
     * @brief Take @p panel out of its floating container, if it has one
     *
     * @return The container's geometry (the source rect of a docking
     *         animation), or an invalid rect if the panel was not floating.
     */
    QRect detachFromFloating(DockPanel* panel);
    void updateAllPanelCornerRadii();

    /// Raise all floating containers above docked panels and handles
    void raiseFloatingContainers();

    /// Called when a panel's docking animation finishes to restore z-order
    void onPanelDockingAnimationFinished();

    /// Get layout bounds (rect adjusted for container padding)
    QRect layoutBounds() const;

    void setFloatingPanelsFrozen(bool frozen);
    bool isPointOverFloatingContainer(const QPoint& globalPos) const;

    /// Validate operation can proceed
    bool validateOperation(const char* opName) const;

private:
    // Node-based layout system
    std::unique_ptr<DockLayoutRoot> m_layoutRoot;

    // Floating containers
    QList<DockFloatingContainer*> m_floatingContainers;

    // Containers being removed (prevent double-removal)
    QSet<DockFloatingContainer*> m_containersBeingRemoved;

    // Panel registry (all panels, docked and floating) - using QPointer
    QMap<DockPanelId, QPointer<DockPanel>> m_panels;

    // Drag overlay
    DockOverlay* m_overlay = nullptr;

    // Initial workspace appearance overlay (visual snapshots only)
    QPointer<DockPanelEntranceOverlay> m_panelEntranceOverlay;

    // Theme
    ruwa::ui::core::ThemeColors m_colors;

    // Animation settings
    bool m_animationsEnabled = true;
    int m_animationDuration = 350; // ms

    // Container padding (space around edges)
    int m_containerPadding = 6; // 6px padding like in HTML reference

    // Canvas frame streaming / floating panel freeze
    QElapsedTimer m_canvasFrameClock;
    qint64 m_lastCanvasFrameMs = -1;
    int m_canvasFrameStreak = 0;
    bool m_floatingPanelsFrozen = false;
    QTimer* m_freezeWatchdog = nullptr;

    // State flags
    bool m_destroying = false;
    mutable bool m_inOperation = false;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_CORE_DOCKCONTAINERWIDGET_H
