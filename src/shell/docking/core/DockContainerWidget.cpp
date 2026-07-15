// SPDX-License-Identifier: MPL-2.0

// DockContainerWidget.cpp
#include "DockContainerWidget.h"
#include "DockFloatingContainer.h"
#include "shell/docking/overlay/DockOverlay.h"
#include "shell/docking/widgets/DockPanel.h"
#include "shell/docking/layout/DockLayoutRoot.h"
#include "shell/docking/layout/DockSplitNode.h"
#include "shell/docking/layout/DockLeafNode.h"
#include "features/theme/manager/ThemeManager.h"

#include <QVBoxLayout>
#include <QResizeEvent>
#include <QTimer>
namespace ruwa::ui::docking {

// ============================================================================
// RAII Guard
// ============================================================================

class ContainerOperationGuard {
public:
    explicit ContainerOperationGuard(bool& flag)
        : m_flag(flag)
        , m_acquired(false)
    {
        if (!m_flag) {
            m_flag = true;
            m_acquired = true;
        }
    }
    ~ContainerOperationGuard()
    {
        if (m_acquired) {
            m_flag = false;
        }
    }
    bool acquired() const { return m_acquired; }
    operator bool() const { return m_acquired; }

private:
    bool& m_flag;
    bool m_acquired;
};

// ============================================================================
// Constructor / Destructor
// ============================================================================

DockContainerWidget::DockContainerWidget(QWidget* parent)
    : QWidget(parent)
{
    setupUI();

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() {
            // Defer while hidden (background workspace tab): the owning
            // WorkspaceTab re-applies the container theme on activation via
            // WorkspaceTab::onApplyThemeRefresh -> applyTheme(). This keeps a
            // theme change from re-styling every open project's dock chrome.
            if (!m_destroying && isVisible()) {
                applyTheme(ruwa::ui::core::ThemeManager::instance().colors());
            }
        });

    // Update corner radii when layout changes
    connect(this, &DockContainerWidget::layoutChanged, this,
        &DockContainerWidget::updateAllPanelCornerRadii);

    applyTheme(ruwa::ui::core::ThemeManager::instance().colors());
}

DockContainerWidget::~DockContainerWidget()
{
    m_destroying = true;
    // Clear floating containers first
    for (auto* container : m_floatingContainers) {
        if (container) {
            delete container;
        }
    }
    m_floatingContainers.clear();
    m_containersBeingRemoved.clear();

    // Panels are owned externally (by DockManager)
    m_panels.clear();
}

// ============================================================================
// Validation
// ============================================================================

bool DockContainerWidget::validateOperation(const char* opName) const
{
    if (m_destroying) {
        return false;
    }

    if (m_inOperation) {
        return false;
    }

    return true;
}

// ============================================================================
// Panel Operations
// ============================================================================

void DockContainerWidget::addPanel(DockPanel* panel, DockPosition position)
{
    if (!panel) {
        return;
    }

    if (!validateOperation("addPanel"))
        return;

    ContainerOperationGuard guard(m_inOperation);
    if (!guard)
        return;

    // Register panel
    m_panels[panel->id()] = panel;

    if (m_layoutRoot) {
        // Ensure panel has a single representation in the tree.
        while (m_layoutRoot->findLeafForPanel(panel)) {
            m_layoutRoot->removePanel(panel);
        }

        // If panel is still hosted by a floating container, detach it first.
        for (auto* container : m_floatingContainers) {
            if (container && container->panel() == panel) {
                panel->setFloatingContainer(nullptr);
                if (auto* layout = container->layout()) {
                    layout->removeWidget(panel);
                }
                m_inOperation = false;
                removeFloatingContainer(container);
                m_inOperation = true;
                break;
            }
        }

        // Panel is a direct child of container
        panel->setParent(this);
        if (panel->floatingContainer()) {
            panel->setFloatingContainer(nullptr);
        }
        panel->setState(PanelState::Docked);
        m_layoutRoot->addPanel(panel, position);

        // Ensure bounds are set if container has valid geometry
        if (rect().isValid()) {
            m_layoutRoot->setRootBounds(layoutBounds());
        }

        panel->show();
    }

    emit panelAdded(panel);
    emit layoutChanged();
}

void DockContainerWidget::addPanelRelativeTo(
    DockPanel* panel, DockPanel* relativeTo, DockPosition position)
{
    if (!panel || !relativeTo) {
        return;
    }

    // Verify relativeTo is still valid
    QPointer<DockPanel> relativePtr = m_panels.value(relativeTo->id(), nullptr);
    if (relativePtr.isNull()) {
        addPanel(panel, position);
        return;
    }

    if (!validateOperation("addPanelRelativeTo"))
        return;

    ContainerOperationGuard guard(m_inOperation);
    if (!guard)
        return;

    // Register panel
    m_panels[panel->id()] = panel;

    if (m_layoutRoot) {
        // Ensure panel has a single representation in the tree.
        while (m_layoutRoot->findLeafForPanel(panel)) {
            m_layoutRoot->removePanel(panel);
        }

        // If panel is still hosted by a floating container, detach it first.
        for (auto* container : m_floatingContainers) {
            if (container && container->panel() == panel) {
                panel->setFloatingContainer(nullptr);
                if (auto* layout = container->layout()) {
                    layout->removeWidget(panel);
                }
                m_inOperation = false;
                removeFloatingContainer(container);
                m_inOperation = true;
                break;
            }
        }

        panel->setParent(this);
        if (panel->floatingContainer()) {
            panel->setFloatingContainer(nullptr);
        }
        panel->setState(PanelState::Docked);
        m_layoutRoot->addPanelRelativeTo(panel, relativeTo, position);

        // Ensure bounds are set if container has valid geometry
        if (rect().isValid()) {
            m_layoutRoot->setRootBounds(layoutBounds());
        }

        panel->show();
    }

    emit panelAdded(panel);
    emit layoutChanged();
}

void DockContainerWidget::removePanel(DockPanel* panel)
{
    if (!panel)
        return;

    if (!validateOperation("removePanel"))
        return;

    ContainerOperationGuard guard(m_inOperation);
    if (!guard)
        return;

    // Remove from registry
    m_panels.remove(panel->id());

    if (m_layoutRoot) {
        while (m_layoutRoot->removePanel(panel)) { }
    }

    // Remove from floating containers
    for (auto* container : m_floatingContainers) {
        if (container && container->panel() == panel) {
            panel->setFloatingContainer(nullptr);
            if (auto* layout = container->layout()) {
                layout->removeWidget(panel);
            }
            panel->setParent(this);
            panel->hide();
            m_inOperation = false;
            removeFloatingContainer(container);
            m_inOperation = true;
            break;
        }
    }

    panel->setState(PanelState::Hidden);
    emit panelRemoved(panel);
    emit layoutChanged();
}

QList<DockPanel*> DockContainerWidget::dockedPanels() const
{
    return m_layoutRoot ? m_layoutRoot->allPanels() : QList<DockPanel*>();
}

QList<DockPanel*> DockContainerWidget::floatingPanels() const
{
    QList<DockPanel*> panels;
    for (auto* container : m_floatingContainers) {
        if (container && container->panel()) {
            panels.append(container->panel());
        }
    }
    return panels;
}

QList<DockPanel*> DockContainerWidget::allPanels() const
{
    QList<DockPanel*> result;
    for (auto it = m_panels.begin(); it != m_panels.end(); ++it) {
        if (!it.value().isNull()) {
            result.append(it.value().data());
        }
    }
    return result;
}

DockPanel* DockContainerWidget::findPanel(const DockPanelId& id) const
{
    QPointer<DockPanel> p = m_panels.value(id, nullptr);
    return p.isNull() ? nullptr : p.data();
}

bool DockContainerWidget::containsPanel(DockPanel* panel) const
{
    if (!panel)
        return false;
    QPointer<DockPanel> p = m_panels.value(panel->id(), nullptr);
    return !p.isNull() && p.data() == panel;
}

std::optional<PanelPlacement> DockContainerWidget::getPanelPlacement(DockPanel* panel) const
{
    if (!panel)
        return std::nullopt;
    if (m_layoutRoot) {
        return m_layoutRoot->getPanelPlacement(panel);
    }
    return std::nullopt;
}

// ============================================================================
// Floating Operations
// ============================================================================

void DockContainerWidget::floatPanel(DockPanel* panel, const QPoint& pos, bool exactPosition)
{
    if (!panel) {
        return;
    }

    if (!validateOperation("floatPanel"))
        return;

    ContainerOperationGuard guard(m_inOperation);
    if (!guard)
        return;

    // Save panel's current docked size before floating
    // IMPORTANT: Only save size in the relevant direction based on the split type
    // to avoid overwriting user-set sizes for the other direction
    if (panel->isDocked() && m_layoutRoot) {
        DockLeafNode* leaf = m_layoutRoot->findLeafForPanel(panel);
        if (leaf && leaf->parent() && leaf->parent()->isSplit()) {
            auto* parentSplit = static_cast<DockSplitNode*>(leaf->parent());
            QSize currentSize = panel->size();

            if (parentSplit->direction() == SplitDirection::Horizontal) {
                // Panel was in horizontal split - save width only
                panel->setUserHorizontalDockedWidth(currentSize.width());
            } else {
                // Panel was in vertical split - save height only
                panel->setUserVerticalDockedHeight(currentSize.height());
            }
        }
        // If panel has no parent split (it's the only panel), don't save sizes
        // because it was stretched to fill the entire container
    }

    // Save panel's current geometry in container coordinates BEFORE removing
    QRect sourceGeom;

    if (m_layoutRoot) {
        // Capture layout state for animation (before removing panel)
        if (m_animationsEnabled) {
            m_layoutRoot->captureLayoutState();
        }

        // Get geometry and remove from layout
        QPoint panelPos = panel->mapTo(this, QPoint(0, 0));
        sourceGeom = QRect(panelPos, panel->size());

        m_inOperation = false;
        m_layoutRoot->removePanel(panel);
        m_inOperation = true;

        // Animate remaining panels in layout
        if (m_animationsEnabled) {
            m_layoutRoot->animateLayoutChange(panel);
        }
    }

    // Create floating container
    DockFloatingContainer* container = createFloatingContainer(panel);
    if (!container) {
        return;
    }

    container->setAnimationDuration(m_animationDuration);

    // Animate appearance if enabled and we have source geometry
    QPoint localPos = mapFromGlobal(pos);
    if (m_animationsEnabled && sourceGeom.isValid() && !exactPosition) {
        container->show();
        container->raise();
        container->animateAppearance(sourceGeom, localPos, m_animationDuration);
    } else {
        // No animation - position directly. exactPosition: pos is top-left; else pos is cursor
        // (center under it)
        if (!exactPosition) {
            localPos.rx() -= container->width() / 2;
            localPos.ry() -= 14;
        }
        container->moveTo(localPos);
        container->show();
        container->raise();
    }
    emit panelFloated(panel);
    emit layoutChanged();
}

void DockContainerWidget::dockPanel(DockPanel* panel, DockPosition position)
{
    if (!panel) {
        return;
    }

    if (!validateOperation("dockPanel"))
        return;

    ContainerOperationGuard guard(m_inOperation);
    if (!guard)
        return;

    // Capture floating container's geometry for animation
    QRect sourceGeom;
    bool wasFloating = false;

    // Remove from floating containers first
    for (auto* container : m_floatingContainers) {
        if (container && container->panel() == panel) {
            // Don't process if already being removed
            if (m_containersBeingRemoved.contains(container)) {
                continue;
            }

            // Capture geometry for animation
            sourceGeom = container->geometry();
            wasFloating = true;

            // Save floating size before docking
            // This ensures the panel returns to the same size when floated again
            panel->setUserFloatingSize(sourceGeom.width(), sourceGeom.height());

            panel->setFloatingContainer(nullptr);

            if (auto* layout = container->layout()) {
                layout->removeWidget(panel);
            }

            panel->setParent(this);
            panel->hide();

            m_inOperation = false;
            removeFloatingContainer(container);
            m_inOperation = true;
            break;
        }
    }

    if (m_layoutRoot) {
        // Capture layout state for animation (before adding panel)
        if (m_animationsEnabled) {
            m_layoutRoot->captureLayoutState();
        }

        m_inOperation = false;
        m_layoutRoot->addPanel(panel, position);
        m_inOperation = true;

        // Ensure bounds are set if container has valid geometry
        if (rect().isValid()) {
            m_layoutRoot->setRootBounds(layoutBounds());
        }

        // Update panel state
        panel->setFloatingContainer(nullptr); // Clear floating state
        panel->setState(PanelState::Docked);
        panel->show();

        // Animate docking if panel was floating and animations enabled
        if (m_animationsEnabled && wasFloating && sourceGeom.isValid()) {
            QRect targetGeom = panel->geometry();
            panel->animateDocking(sourceGeom, targetGeom, m_animationDuration);

            // Raise panel above handles during animation to prevent visual overlap
            panel->raise();

            // Restore z-order when animation finishes
            connect(panel, &DockPanel::dockingAnimationFinished, this,
                &DockContainerWidget::onPanelDockingAnimationFinished, Qt::SingleShotConnection);
        }

        // Animate other panels in layout (excluding the docked panel)
        if (m_animationsEnabled) {
            m_layoutRoot->animateLayoutChange(panel);
        }
    }

    emit panelDocked(panel);
    emit layoutChanged();
}

void DockContainerWidget::dockPanelRelativeTo(
    DockPanel* panel, DockPanel* relativeTo, DockPosition position)
{
    if (!panel || !relativeTo) {
        return;
    }

    // Verify relativeTo
    if (!containsPanel(relativeTo)) {
        dockPanel(panel, position);
        return;
    }

    if (!validateOperation("dockPanelRelativeTo"))
        return;

    ContainerOperationGuard guard(m_inOperation);
    if (!guard)
        return;

    // Capture floating container's geometry for animation
    QRect sourceGeom;
    bool wasFloating = false;

    // Remove from floating containers first
    for (auto* container : m_floatingContainers) {
        if (container && container->panel() == panel) {
            if (m_containersBeingRemoved.contains(container))
                continue;

            // Capture geometry for animation
            sourceGeom = container->geometry();
            wasFloating = true;

            // Save floating size before docking
            // This ensures the panel returns to the same size when floated again
            panel->setUserFloatingSize(sourceGeom.width(), sourceGeom.height());

            panel->setFloatingContainer(nullptr);

            if (auto* layout = container->layout()) {
                layout->removeWidget(panel);
            }

            panel->setParent(this);
            panel->hide();

            m_inOperation = false;
            removeFloatingContainer(container);
            m_inOperation = true;
            break;
        }
    }

    if (m_layoutRoot) {
        // Capture layout state for animation (before adding panel)
        if (m_animationsEnabled) {
            m_layoutRoot->captureLayoutState();
        }

        m_inOperation = false;
        m_layoutRoot->addPanelRelativeTo(panel, relativeTo, position);
        m_inOperation = true;

        // Ensure bounds are set if container has valid geometry
        if (rect().isValid()) {
            m_layoutRoot->setRootBounds(layoutBounds());
        }

        // Update panel state
        panel->setFloatingContainer(nullptr); // Clear floating state
        panel->setState(PanelState::Docked);
        panel->show();

        // Animate docking if panel was floating and animations enabled
        if (m_animationsEnabled && wasFloating && sourceGeom.isValid()) {
            QRect targetGeom = panel->geometry();
            panel->animateDocking(sourceGeom, targetGeom, m_animationDuration);

            // Raise panel above handles during animation to prevent visual overlap
            panel->raise();

            // Restore z-order when animation finishes
            connect(panel, &DockPanel::dockingAnimationFinished, this,
                &DockContainerWidget::onPanelDockingAnimationFinished, Qt::SingleShotConnection);
        }

        // Animate other panels in layout (excluding the docked panel)
        if (m_animationsEnabled) {
            m_layoutRoot->animateLayoutChange(panel);
        }
    }

    emit panelDocked(panel);
    emit layoutChanged();
}

// ============================================================================
// Animation Settings
// ============================================================================

void DockContainerWidget::setAnimationsEnabled(bool enabled)
{
    m_animationsEnabled = enabled;

    if (m_layoutRoot) {
        m_layoutRoot->setLayoutAnimationEnabled(enabled);
    }
}

void DockContainerWidget::setAnimationDuration(int ms)
{
    m_animationDuration = qMax(0, ms);

    if (m_layoutRoot) {
        m_layoutRoot->setLayoutAnimationDuration(ms);
    }
}

void DockContainerWidget::setContainerPadding(int padding)
{
    m_containerPadding = qMax(0, padding);

    // Update layout bounds
    if (m_layoutRoot && rect().isValid()) {
        m_layoutRoot->setRootBounds(layoutBounds());
    }

    update();
}

bool DockContainerWidget::repairDockLayout()
{
    if (!m_layoutRoot) {
        return false;
    }

    const bool repaired = m_layoutRoot->repairLayout();

    if (rect().isValid()) {
        m_layoutRoot->setRootBounds(layoutBounds());
    }

    updateAllPanelCornerRadii();
    raiseFloatingContainers();
    return repaired;
}

void DockContainerWidget::restoreDockedPanel(DockPanel* panel)
{
    if (!panel) {
        return;
    }

    // Register panel in the container's registry so subsequent relative docking
    // operations (e.g. addPanelRelativeTo) recognize it as a valid target.
    // Without this, panels loaded from a preset are known to the layout tree
    // but not to m_panels, causing addPanelRelativeTo to fall back to root docking.
    if (!m_panels.contains(panel->id())) {
        m_panels[panel->id()] = panel;
    }

    panel->setParent(this);
    panel->setFloatingContainer(nullptr);
    panel->setState(PanelState::Docked);
    panel->show();
}

// ============================================================================
// Theme
// ============================================================================

void DockContainerWidget::applyTheme(const ruwa::ui::core::ThemeColors& colors)
{
    if (m_destroying)
        return;

    m_colors = colors;

    // The perimeter gap around the docked panels (between the top bar and the
    // workspace, and along every edge) is this container's background. Ensure the
    // stylesheet background is actually painted and force a repaint so it tracks
    // theme changes at runtime instead of only after an app restart.
    setAttribute(Qt::WA_StyledBackground, true);
    setStyleSheet(QString("background: %1;").arg(colors.background.name()));
    update();

    if (m_layoutRoot) {
        m_layoutRoot->applyTheme(colors);
    }

    for (auto* container : m_floatingContainers) {
        if (container) {
            container->applyTheme(colors);
        }
    }

    if (m_overlay) {
        m_overlay->applyTheme(colors);
    }

    // Defer layout bounds update: theme change (scale, fonts) invalidates parent layouts.
    // rect() may be stale until Qt's layout system runs. Deferring ensures we read
    // the correct size after layout has adjusted to the new theme.
    if (m_layoutRoot) {
        QTimer::singleShot(0, this, [this]() {
            if (!m_destroying && m_layoutRoot && rect().isValid()) {
                m_layoutRoot->setRootBounds(layoutBounds());
                repairDockLayout();
                if (m_overlay) {
                    m_overlay->setGeometry(rect());
                }
                updateAllPanelCornerRadii();
            }
        });
    }
}

// ============================================================================
// Events
// ============================================================================

void DockContainerWidget::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);

    if (m_overlay) {
        m_overlay->setGeometry(rect());
    }

    // Update root bounds (with padding)
    if (m_layoutRoot) {
        m_layoutRoot->setRootBounds(layoutBounds());
    }

    // Update corner radii for all docked panels
    updateAllPanelCornerRadii();
}

void DockContainerWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);

    // Initialize layout bounds when first shown (with padding)
    if (m_layoutRoot && rect().isValid()) {
        m_layoutRoot->setRootBounds(layoutBounds());
    }

    // Update corner radii when first shown
    updateAllPanelCornerRadii();

    // Defer a second layout pass: when shown inside a tab/stack, parent layout may not
    // have finalized our geometry yet. One event-loop tick ensures correct bounds.
    if (m_layoutRoot) {
        QTimer::singleShot(0, this, [this]() {
            if (!m_destroying && m_layoutRoot && rect().isValid()) {
                m_layoutRoot->setRootBounds(layoutBounds());
                repairDockLayout();
                if (m_overlay) {
                    m_overlay->setGeometry(rect());
                }
                updateAllPanelCornerRadii();
            }
        });
    }
}

// ============================================================================
// Private
// ============================================================================

void DockContainerWidget::setupUI()
{
    QVBoxLayout* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);

    // Create node-based layout system
    m_layoutRoot = std::make_unique<DockLayoutRoot>(this, this);
    connect(m_layoutRoot.get(), &DockLayoutRoot::layoutChanged, this,
        &DockContainerWidget::layoutChanged);
    // Ensure floating containers stay above handles after layout changes
    connect(m_layoutRoot.get(), &DockLayoutRoot::layoutChanged, this,
        &DockContainerWidget::raiseFloatingContainers);
    // When layout changes (e.g. after handle drag), re-apply bounds so all panels
    // including Layers get correct geometry and no empty space remains
    connect(m_layoutRoot.get(), &DockLayoutRoot::layoutChanged, this, [this]() {
        if (m_layoutRoot && rect().isValid()) {
            m_layoutRoot->setRootBounds(layoutBounds());
        }
    });

    createOverlay();
}

void DockContainerWidget::createOverlay()
{
    m_overlay = new DockOverlay(this);
    m_overlay->setGeometry(rect());
    m_overlay->hide();
    m_overlay->raise();
}

DockFloatingContainer* DockContainerWidget::createFloatingContainer(DockPanel* panel)
{
    if (!panel)
        return nullptr;

    auto* container = new DockFloatingContainer(this, panel);
    container->applyTheme(m_colors);
    m_floatingContainers.append(container);

    // Register panel if not already
    if (!m_panels.contains(panel->id())) {
        m_panels[panel->id()] = panel;
    }
    return container;
}

void DockContainerWidget::removeFloatingContainer(DockFloatingContainer* container)
{
    if (!container)
        return;

    // Prevent double-removal
    if (m_containersBeingRemoved.contains(container)) {
        return;
    }

    m_containersBeingRemoved.insert(container);
    m_floatingContainers.removeOne(container);
    // Use delete for immediate cleanup
    delete container;

    m_containersBeingRemoved.remove(container);
}

void DockContainerWidget::updateAllPanelCornerRadii()
{
    if (m_destroying)
        return;

    // Update all docked panels
    for (DockPanel* panel : dockedPanels()) {
        if (panel) {
            panel->updateCornerRadii();
        }
    }
}

void DockContainerWidget::raiseFloatingContainers()
{
    if (m_destroying)
        return;

    // Raise all floating containers to ensure they are above
    // docked panels and split handles
    for (auto* container : m_floatingContainers) {
        if (container && container->isVisible()) {
            container->raise();
        }
    }

    // Also raise the overlay to be on top of everything
    if (m_overlay) {
        m_overlay->raise();
    }
}

void DockContainerWidget::onPanelDockingAnimationFinished()
{
    if (m_destroying)
        return;

    // Restore proper z-order: handles should be above panels
    if (m_layoutRoot) {
        m_layoutRoot->raiseHandles();
    }

    // Also raise floating containers above everything
    raiseFloatingContainers();
}

QRect DockContainerWidget::layoutBounds() const
{
    // Return rect adjusted for container padding
    return rect().adjusted(
        m_containerPadding, m_containerPadding, -m_containerPadding, -m_containerPadding);
}

} // namespace ruwa::ui::docking
