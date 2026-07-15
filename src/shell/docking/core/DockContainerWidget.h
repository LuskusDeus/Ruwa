// SPDX-License-Identifier: MPL-2.0

// DockContainerWidget.h
#ifndef RUWA_UI_DOCKING_CORE_DOCKCONTAINERWIDGET_H
#define RUWA_UI_DOCKING_CORE_DOCKCONTAINERWIDGET_H

#include "shell/docking/DockTypes.h"
#include "features/theme/manager/ThemeColors.h"
#include "shell/docking/state/DockLayoutPreset.h"

#include <QWidget>
#include <QList>
#include <QMap>
#include <QPointer>
#include <QSet>
#include <memory>
#include <optional>

namespace ruwa::ui::docking {

class DockManager;
class DockPanel;
class DockFloatingContainer;
class DockOverlay;
class DockLayoutRoot;

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

    // === Container Padding ===

    /// Set padding around container edges (space between panels and container border)
    void setContainerPadding(int padding);
    int containerPadding() const { return m_containerPadding; }

    // === Theme ===

    void applyTheme(const ruwa::ui::core::ThemeColors& colors);

signals:
    void panelAdded(DockPanel* panel);
    void panelRemoved(DockPanel* panel);
    void panelFloated(DockPanel* panel);
    void panelDocked(DockPanel* panel);
    void layoutChanged();

protected:
    void resizeEvent(QResizeEvent* event) override;
    void showEvent(QShowEvent* event) override;

private:
    friend class DockManager;

    void setupUI();
    void createOverlay();
    DockFloatingContainer* createFloatingContainer(DockPanel* panel);
    void removeFloatingContainer(DockFloatingContainer* container);
    void updateAllPanelCornerRadii();

    /// Raise all floating containers above docked panels and handles
    void raiseFloatingContainers();

    /// Called when a panel's docking animation finishes to restore z-order
    void onPanelDockingAnimationFinished();

    /// Get layout bounds (rect adjusted for container padding)
    QRect layoutBounds() const;

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

    // Theme
    ruwa::ui::core::ThemeColors m_colors;

    // Animation settings
    bool m_animationsEnabled = true;
    int m_animationDuration = 350; // ms

    // Container padding (space around edges)
    int m_containerPadding = 6; // 6px padding like in HTML reference

    // State flags
    bool m_destroying = false;
    mutable bool m_inOperation = false;
};

} // namespace ruwa::ui::docking

#endif // RUWA_UI_DOCKING_CORE_DOCKCONTAINERWIDGET_H
