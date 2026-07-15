// SPDX-License-Identifier: MPL-2.0

// DockManager.cpp
#include "DockManager.h"
#include "DockContainerWidget.h"
#include "DockFloatingContainer.h"
#include "shell/docking/widgets/DockPanel.h"
#include "shell/docking/widgets/DockPanelTitleBar.h"
#include "shell/docking/overlay/DockOverlay.h"

#include <QJsonArray>
#include <QApplication>
namespace ruwa::ui::docking {

// ============================================================================
// Constructor / Destructor
// ============================================================================

DockManager::DockManager(QObject* parent)
    : QObject(parent)
{
    m_dragState.updateTimer.start();
}

DockManager::~DockManager()
{
    // Cancel any active drag
    if (m_dragState.active) {
        m_dragState.reset();
    }

    // Disconnect all panels before deletion
    for (auto it = m_panels.begin(); it != m_panels.end(); ++it) {
        if (DockPanel* panel = it.value()) {
            disconnectPanel(panel);
        }
    }

    // Delete all owned panels
    for (auto it = m_panels.begin(); it != m_panels.end(); ++it) {
        if (DockPanel* panel = it.value()) {
            delete panel;
        }
    }
    m_panels.clear();
}

// ============================================================================
// Container
// ============================================================================

void DockManager::setContainer(DockContainerWidget* container)
{
    if (m_dragState.active) {
        return;
    }

    if (m_container) {
        disconnect(m_container, nullptr, this, nullptr);
    }

    m_container = container;

    if (m_container) {
        connect(m_container, &DockContainerWidget::layoutChanged, this, [this]() {
            if (!m_inLayoutChange) {
                OperationGuard layoutGuard(m_inLayoutChange);
                emit layoutChanged();
            }
        });
    }
}

// ============================================================================
// Panel Registration
// ============================================================================

void DockManager::registerPanel(DockPanel* panel)
{
    if (!panel) {
        return;
    }

    if (m_panels.contains(panel->id())) {
        return;
    }

    // Guard against re-entrant registration
    OperationGuard guard(m_inPanelOperation);
    if (!guard) {
        return;
    }

    m_panels[panel->id()] = panel;
    connectPanel(panel);

    // Track panel destruction
    connect(panel, &QObject::destroyed, this, &DockManager::onPanelDestroyed);
    emit panelRegistered(panel);
}

void DockManager::unregisterPanel(DockPanel* panel)
{
    if (!panel)
        return;

    // Don't unregister during drag of this panel
    if (m_dragState.active && m_dragState.panel == panel) {
        return;
    }

    OperationGuard guard(m_inPanelOperation);
    if (!guard) {
        return;
    }

    disconnectPanel(panel);
    disconnect(panel, &QObject::destroyed, this, &DockManager::onPanelDestroyed);
    m_panels.remove(panel->id());

    if (m_container) {
        m_container->removePanel(panel);
    }
    emit panelUnregistered(panel);
}

DockPanel* DockManager::panel(const DockPanelId& id) const
{
    QPointer<DockPanel> p = m_panels.value(id, nullptr);
    return p.isNull() ? nullptr : p.data();
}

DockPanel* DockManager::panelByTitle(const QString& title) const
{
    for (auto it = m_panels.begin(); it != m_panels.end(); ++it) {
        if (DockPanel* p = it.value()) {
            if (p->title() == title) {
                return p;
            }
        }
    }
    return nullptr;
}

DockPanel* DockManager::panelByPersistentKey(const QString& key) const
{
    const QString normalized = key.trimmed();
    if (normalized.isEmpty()) {
        return nullptr;
    }

    for (auto it = m_panels.begin(); it != m_panels.end(); ++it) {
        if (DockPanel* p = it.value()) {
            if (p->persistentKey() == normalized) {
                return p;
            }
        }
    }
    return nullptr;
}

bool DockManager::isPanelValid(DockPanel* panel) const
{
    if (!panel)
        return false;

    QPointer<DockPanel> stored = m_panels.value(panel->id(), nullptr);
    return !stored.isNull() && stored.data() == panel;
}

// ============================================================================
// Panel Operations
// ============================================================================

void DockManager::addPanel(DockPanel* panel, DockPosition position)
{
    if (!panel || !m_container) {
        return;
    }

    // Check if already in operation
    if (m_panelsInOperation.contains(panel->id())) {
        return;
    }

    OperationGuard guard(m_inPanelOperation);
    if (!guard) {
        return;
    }

    m_panelsInOperation.insert(panel->id());

    // Register if not already
    if (!m_panels.contains(panel->id())) {
        // Temporarily release guard for registration
        m_inPanelOperation = false;
        registerPanel(panel);
        m_inPanelOperation = true;
    }

    m_container->addPanel(panel, position);

    m_panelsInOperation.remove(panel->id());

    // Emit layout changed (guarded)
    if (!m_inLayoutChange) {
        OperationGuard layoutGuard(m_inLayoutChange);
        emit layoutChanged();
    }
}

void DockManager::addPanelRelativeTo(DockPanel* panel, DockPanel* relativeTo, DockPosition position)
{
    if (!panel || !relativeTo || !m_container) {
        return;
    }

    if (!isPanelValid(relativeTo)) {
        addPanel(panel, position); // Fallback
        return;
    }

    OperationGuard guard(m_inPanelOperation);
    if (!guard) {
        return;
    }

    if (!m_panels.contains(panel->id())) {
        m_inPanelOperation = false;
        registerPanel(panel);
        m_inPanelOperation = true;
    }

    m_container->addPanelRelativeTo(panel, relativeTo, position);

    if (!m_inLayoutChange) {
        OperationGuard layoutGuard(m_inLayoutChange);
        emit layoutChanged();
    }
}

void DockManager::removePanel(DockPanel* panel)
{
    if (!panel || !m_container)
        return;

    if (m_dragState.active && m_dragState.panel == panel) {
        return;
    }

    OperationGuard guard(m_inPanelOperation);
    if (!guard)
        return;

    m_container->removePanel(panel);

    if (!m_inLayoutChange) {
        OperationGuard layoutGuard(m_inLayoutChange);
        emit layoutChanged();
    }
}

void DockManager::closePanel(DockPanel* panel)
{
    if (!panel)
        return;

    if (m_dragState.active && m_dragState.panel == panel) {
        cancelDrag();
    }

    OperationGuard guard(m_inPanelOperation);
    if (!guard)
        return;

    panel->hide();
    panel->setState(PanelState::Hidden);

    if (m_container) {
        m_container->removePanel(panel);
    }

    emit panelClosed(panel);

    if (!m_inLayoutChange) {
        OperationGuard layoutGuard(m_inLayoutChange);
        emit layoutChanged();
    }
}

void DockManager::showPanel(DockPanel* panel, DockPosition position)
{
    if (!panel || !m_container)
        return;

    OperationGuard guard(m_inPanelOperation);
    if (!guard)
        return;

    m_inPanelOperation = false;
    addPanel(panel, position);
    m_inPanelOperation = true;

    panel->show();
    emit panelShown(panel);
}

void DockManager::floatPanel(DockPanel* panel, const QPoint& globalPos, bool exactPosition)
{
    if (!panel || !m_container)
        return;

    if (!isPanelValid(panel)) {
        return;
    }

    OperationGuard guard(m_inPanelOperation);
    if (!guard)
        return;

    m_container->floatPanel(panel, globalPos, exactPosition);

    emit panelFloated(panel);

    if (!m_inLayoutChange) {
        OperationGuard layoutGuard(m_inLayoutChange);
        emit layoutChanged();
    }
}

void DockManager::dockPanel(DockPanel* panel, DockPosition position)
{
    if (!panel || !m_container)
        return;

    if (!isPanelValid(panel)) {
        return;
    }

    OperationGuard guard(m_inPanelOperation);
    if (!guard)
        return;

    m_container->dockPanel(panel, position);

    emit panelDocked(panel);

    if (!m_inLayoutChange) {
        OperationGuard layoutGuard(m_inLayoutChange);
        emit layoutChanged();
    }
}

// ============================================================================
// Drag & Drop
// ============================================================================

bool DockManager::validateDragOperation() const
{
    if (!m_container) {
        return false;
    }

    if (!m_dragState.panel) {
        return false;
    }

    // Check if panel was deleted during drag
    if (m_dragState.panel.isNull()) {
        return false;
    }

    return true;
}

void DockManager::cleanupDragState()
{
    if (m_container && m_container->overlay()) {
        m_container->overlay()->hideOverlay();
    }

    if (m_dragState.panel && !m_dragState.panel.isNull()) {
        if (auto* floatingContainer = m_dragState.panel->floatingContainer()) {
            floatingContainer->endDrag();
        }
    }

    m_dragState.reset();
}

void DockManager::startDrag(DockPanel* panel, const QPoint& globalPos)
{
    // Prevent double start
    if (m_dragState.active) {
        return;
    }

    if (!panel || !m_container) {
        return;
    }

    if (!isPanelValid(panel)) {
        return;
    }

    // Check if panel allows dragging
    if (!panel->isMovable()) {
        return;
    }

    // Check if panel can be floated (required for drag)
    if (!panel->isFloatable()) {
        return;
    }

    OperationGuard guard(m_inDragOperation);
    if (!guard) {
        return;
    }

    // Initialize drag state
    m_dragState.active = true;
    m_dragState.panel = panel;
    m_dragState.startPos = globalPos;
    m_dragState.startedFromDocked = panel->isDocked();
    m_dragState.updateTimer.restart();
    m_dragState.lastUpdatePos = globalPos;

    // If docked, convert to floating first
    if (m_dragState.startedFromDocked) {
        m_inDragOperation = false; // Allow nested operation
        m_container->floatPanel(panel, globalPos);
        m_inDragOperation = true;
    }

    // Start drag on floating container
    if (auto* floatingContainer = panel->floatingContainer()) {
        floatingContainer->startDrag(globalPos);
    } else {
        cleanupDragState();
        return;
    }

    // Start overlay cooldown timer (overlays will appear after cooldown expires)
    m_dragState.overlayCooldownTimer.start();
    m_dragState.overlaysEnabled = false;

    // Set floating container for z-order management (do this now, overlay will use it later)
    if (m_container->overlay() && panel->floatingContainer()) {
        m_container->overlay()->setFloatingContainer(panel->floatingContainer());
    }
    emit dragStarted(panel);
}

void DockManager::updateDrag(const QPoint& globalPos)
{
    if (!m_dragState.active) {
        return; // Silent return - normal case after drag ends
    }

    if (!validateDragOperation()) {
        cleanupDragState();
        return;
    }

    // Throttle updates
    if (m_dragState.updateTimer.elapsed() < m_dragUpdateIntervalMs) {
        return;
    }

    // Skip if position unchanged
    if (globalPos == m_dragState.lastUpdatePos) {
        return;
    }

    m_dragState.updateTimer.restart();
    m_dragState.lastUpdatePos = globalPos;

    // Update floating container position
    if (auto* floatingContainer = m_dragState.panel->floatingContainer()) {
        floatingContainer->updateDrag(globalPos);
    }

    // Floating panel with docking disabled should ignore all dock zones.
    if (shouldIgnoreDockTargets()) {
        if (m_container->overlay()) {
            m_container->overlay()->hideOverlay();
        }
        return;
    }

    // Check if overlay cooldown has expired
    if (!m_dragState.overlaysEnabled) {
        if (m_dragState.overlayCooldownTimer.elapsed() >= m_overlayCooldownMs) {
            m_dragState.overlaysEnabled = true;
        } else {
            // Cooldown still active - skip overlay updates
            return;
        }
    }

    // Update overlay (container mode)
    if (m_container->overlay()) {
        if (!m_container->overlay()->isContainerMode()) {
            m_container->overlay()->showForContainer();
        }

        m_container->overlay()->updateDropZone(globalPos);
    }
}

void DockManager::endDrag(const QPoint& globalPos)
{
    if (!m_dragState.active) {
        return;
    }

    OperationGuard guard(m_inDragOperation);
    if (!guard) {
        return;
    }

    // Capture state before cleanup
    DockPanel* panel = m_dragState.panel;
    bool wasValid = validateDragOperation();

    DropZone zone = DropZone::None;
    DockPanel* targetPanel = nullptr;
    bool isContainerMode = false;

    if (wasValid && m_container->overlay() && !shouldIgnoreDockTargets()) {
        zone = m_container->overlay()->currentDropZone();
        targetPanel = m_container->overlay()->targetPanel();
        isContainerMode = m_container->overlay()->isContainerMode();
    }

    // Always cleanup overlay first
    if (m_container && m_container->overlay()) {
        m_container->overlay()->hideOverlay();
    }

    // End floating container drag
    if (panel && !m_dragState.panel.isNull()) {
        if (auto* floatingContainer = panel->floatingContainer()) {
            floatingContainer->endDrag();
        }
    }

    // Reset state before applying drop (prevents re-entrancy issues)
    m_dragState.reset();

    // Apply drop if valid
    bool dropped = false;
    bool canDrop = wasValid && panel && zone != DropZone::None && isContainerMode;

    if (canDrop) {
        DockPosition position = zoneToDockPosition(zone);

        m_inDragOperation = false; // Allow nested dock operation

        if (isInnerZone(zone) && targetPanel) {
            // Inner zone with target panel - dock relative to that panel
            m_container->dockPanelRelativeTo(panel, targetPanel, position);
        } else {
            // Outer zone or no target panel - dock at root level
            m_container->dockPanel(panel, position);
        }

        m_inDragOperation = true;
        dropped = true;
    }

    if (panel) {
        emit dragFinished(panel, dropped);
    }

    if (!m_inLayoutChange) {
        OperationGuard layoutGuard(m_inLayoutChange);
        emit layoutChanged();
    }
}

void DockManager::cancelDrag()
{
    if (!m_dragState.active) {
        return;
    }

    OperationGuard guard(m_inDragOperation);
    if (!guard) {
        // Force cleanup even if guard fails
        cleanupDragState();
        return;
    }

    DockPanel* panel = m_dragState.panel;
    bool shouldRedock = m_dragState.startedFromDocked;

    cleanupDragState();

    // Try to re-dock if originally docked
    if (shouldRedock && panel && m_container) {
        m_inDragOperation = false;
        m_container->dockPanel(panel, DockPosition::Right);
        m_inDragOperation = true;
    }
    if (panel) {
        emit dragFinished(panel, false);
    }
}

// ============================================================================
// State Serialization
// ============================================================================

QJsonObject DockManager::saveState() const
{
    QJsonObject state;

    QJsonArray panelsArray;
    for (auto it = m_panels.begin(); it != m_panels.end(); ++it) {
        DockPanel* panel = it.value();
        if (!panel)
            continue; // Skip null (deleted) panels

        QJsonObject panelObj;
        panelObj["id"] = panel->id().toString();
        panelObj["title"] = panel->title();
        panelObj["state"] = static_cast<int>(panel->state());
        panelObj["features"] = static_cast<int>(panel->features());

        if (panel->isFloating() && panel->floatingContainer()) {
            QRect geom = panel->floatingContainer()->geometry();
            QJsonObject geomObj;
            geomObj["x"] = geom.x();
            geomObj["y"] = geom.y();
            geomObj["width"] = geom.width();
            geomObj["height"] = geom.height();
            panelObj["floatingGeometry"] = geomObj;
        }

        panelsArray.append(panelObj);
    }
    state["panels"] = panelsArray;

    return state;
}

bool DockManager::restoreState(const QJsonObject& state)
{
    if (state.isEmpty()) {
        return false;
    }

    if (m_dragState.active) {
        return false;
    }

    // TODO: Implement state restoration

    return true;
}

void DockManager::resetLayout()
{
    if (!m_container)
        return;

    if (m_dragState.active) {
        cancelDrag();
    }

    const bool hadAnimations = m_container->animationsEnabled();
    m_container->setAnimationsEnabled(false);

    OperationGuard guard(m_inPanelOperation);
    if (!guard) {
        m_container->setAnimationsEnabled(hadAnimations);
        return;
    }

    // Snapshot current panels first to avoid iterator invalidation and ensure
    // we clear the exact set that existed before reset started.
    QList<QPointer<DockPanel>> panelSnapshot;
    panelSnapshot.reserve(m_panels.size());
    for (auto it = m_panels.begin(); it != m_panels.end(); ++it) {
        if (DockPanel* panel = it.value()) {
            panelSnapshot.append(panel);
        }
    }

    // Normalize floating panels back into the dock tree first. This prevents
    // reset from losing panels that currently live inside floating containers.
    for (const QPointer<DockPanel>& panelRef : panelSnapshot) {
        DockPanel* panel = panelRef.data();
        if (panel && panel->isFloating() && panel->floatingContainer()) {
            m_container->dockPanel(panel, DockPosition::Right);
        }
    }

    // Fully clear the current layout before a preset is applied.
    for (const QPointer<DockPanel>& panelRef : panelSnapshot) {
        DockPanel* panel = panelRef.data();
        if (!panel) {
            continue;
        }

        if (m_container->containsPanel(panel)) {
            m_container->removePanel(panel);
        }

        panel->hide();
    }

    if (!m_inLayoutChange) {
        OperationGuard layoutGuard(m_inLayoutChange);
        emit layoutChanged();
    }

    m_container->setAnimationsEnabled(hadAnimations);
}

// ============================================================================
// Slots
// ============================================================================

void DockManager::onPanelCloseRequested()
{
    if (auto* panel = qobject_cast<DockPanel*>(sender())) {
        closePanel(panel);
    }
}

void DockManager::onPanelFloatRequested()
{
    if (auto* panel = qobject_cast<DockPanel*>(sender())) {
        if (panel->isDocked()) {
            QPoint globalPos = panel->mapToGlobal(QPoint(0, 0));
            floatPanel(panel, globalPos);
        }
    }
}

void DockManager::onPanelDockRequested()
{
    if (auto* panel = qobject_cast<DockPanel*>(sender())) {
        if (panel->isFloating()) {
            dockPanel(panel, DockPosition::Right);
        }
    }
}

void DockManager::onTitleBarDragStarted(const QPoint& globalPos)
{
    if (auto* titleBar = qobject_cast<DockPanelTitleBar*>(sender())) {
        if (auto* panel = titleBar->panel()) {
            startDrag(panel, globalPos);
        }
    }
}

void DockManager::onTitleBarDragging(const QPoint& globalPos)
{
    updateDrag(globalPos);
}

void DockManager::onTitleBarDragFinished(const QPoint& globalPos)
{
    endDrag(globalPos);
}

void DockManager::onPanelDestroyed(QObject* obj)
{
    // Find and remove the destroyed panel from registry
    for (auto it = m_panels.begin(); it != m_panels.end();) {
        if (it.value().isNull() || it.value().data() == obj) {
            it = m_panels.erase(it);
        } else {
            ++it;
        }
    }

    // Cancel drag if dragging the destroyed panel
    if (m_dragState.active && m_dragState.panel.isNull()) {
        cleanupDragState();
    }
}

// ============================================================================
// Private
// ============================================================================

void DockManager::connectPanel(DockPanel* panel)
{
    connect(panel, &DockPanel::closeRequested, this, &DockManager::onPanelCloseRequested);
    connect(panel, &DockPanel::floatRequested, this, &DockManager::onPanelFloatRequested);
    connect(panel, &DockPanel::dockRequested, this, &DockManager::onPanelDockRequested);

    if (auto* titleBar = panel->titleBar()) {
        connect(
            titleBar, &DockPanelTitleBar::dragStarted, this, &DockManager::onTitleBarDragStarted);
        connect(titleBar, &DockPanelTitleBar::dragging, this, &DockManager::onTitleBarDragging);
        connect(
            titleBar, &DockPanelTitleBar::dragFinished, this, &DockManager::onTitleBarDragFinished);
    }
}

void DockManager::disconnectPanel(DockPanel* panel)
{
    disconnect(panel, nullptr, this, nullptr);

    if (auto* titleBar = panel->titleBar()) {
        disconnect(titleBar, nullptr, this, nullptr);
    }
}

bool DockManager::shouldIgnoreDockTargets() const
{
    DockPanel* panel = m_dragState.panel.data();
    if (!panel) {
        return false;
    }

    // Standard semantics:
    // Dockable=true enables docking; false keeps free-floating drag.
    return panel->isFloating() && !panel->isDockable();
}

} // namespace ruwa::ui::docking
