// SPDX-License-Identifier: MPL-2.0

// DockLayoutRoot.cpp
#include "DockLayoutRoot.h"
#include "DockSplitHandle.h"
#include "shell/docking/widgets/DockGroupHeader.h"
#include "shell/docking/widgets/DockGroupHost.h"
#include "shell/docking/widgets/DockPanel.h"
#include "shell/docking/state/DockLayoutPreset.h"
#include <QSet>
#include <functional>
#include <QEasingCurve>
#include <QJsonArray>
#include <QCoreApplication>
#include <QTimer>

namespace ruwa::ui::docking {

namespace {

// Shared with CanvasCursorManager / WorkspaceTab: while a UI-manipulation drag is in
// progress, the canvas suppresses its custom tool cursor. We raise it during a dock
// splitter resize so that the resize lag (the panel edge trails the pointer, briefly
// uncovering the canvas) doesn't flicker the brush/tool cursor on.
constexpr auto kUiDragActiveProperty = "ruwa_ui_drag_active";

void setUiDragActive(bool active)
{
    if (qApp) {
        qApp->setProperty(kUiDragActiveProperty, active);
    }
}

void applyEdgeAnchoring(DockLeafNode* leaf, DockPanel* panel, DockPosition position)
{
    if (!leaf || !panel) {
        return;
    }

    if (position == DockPosition::Left) {
        leaf->setAnchor(Anchor::Left);
        leaf->setAnchoredSize(qMax(1, panel->effectiveHorizontalDockedWidth()));
    } else if (position == DockPosition::Right) {
        leaf->setAnchor(Anchor::Right);
        leaf->setAnchoredSize(qMax(1, panel->effectiveHorizontalDockedWidth()));
    } else if (position == DockPosition::Top) {
        leaf->setAnchor(Anchor::Top);
        leaf->setAnchoredSize(qMax(1, panel->effectiveVerticalDockedHeight()));
    } else if (position == DockPosition::Bottom) {
        leaf->setAnchor(Anchor::Bottom);
        leaf->setAnchoredSize(qMax(1, panel->effectiveVerticalDockedHeight()));
    }
}

QString anchorToString(Anchor anchor)
{
    switch (anchor) {
    case Anchor::Left:
        return QStringLiteral("left");
    case Anchor::Right:
        return QStringLiteral("right");
    case Anchor::Top:
        return QStringLiteral("top");
    case Anchor::Bottom:
        return QStringLiteral("bottom");
    case Anchor::None:
    default:
        return QStringLiteral("none");
    }
}

Anchor anchorFromString(const QString& value)
{
    if (value == QLatin1String("left"))
        return Anchor::Left;
    if (value == QLatin1String("right"))
        return Anchor::Right;
    if (value == QLatin1String("top"))
        return Anchor::Top;
    if (value == QLatin1String("bottom"))
        return Anchor::Bottom;
    return Anchor::None;
}

} // namespace

DockLayoutRoot::DockLayoutRoot(QWidget* containerWidget, QObject* parent)
    : QObject(parent)
    , m_containerWidget(containerWidget)
{
    // Setup layout animation
    m_layoutAnimation = new QVariantAnimation(this);
    m_layoutAnimation->setStartValue(0.0);
    m_layoutAnimation->setEndValue(1.0);
    m_layoutAnimation->setEasingCurve(QEasingCurve::OutCubic);

    connect(m_layoutAnimation, &QVariantAnimation::valueChanged, this,
        &DockLayoutRoot::onLayoutAnimationValueChanged);
    connect(m_layoutAnimation, &QVariantAnimation::finished, this,
        &DockLayoutRoot::onLayoutAnimationFinished);
}

DockLayoutRoot::~DockLayoutRoot()
{
    m_destroying = true;

    // Stop layout animation
    if (m_layoutAnimation) {
        m_layoutAnimation->disconnect();
        m_layoutAnimation->stop();
    }

    // Delete all handles
    for (auto it = m_handles.begin(); it != m_handles.end(); ++it) {
        qDeleteAll(it.value());
    }
    m_handles.clear();

    // Group frames own no panels: give them back to the container first, then
    // drop the frames (immediately — nothing will run a deferred delete once
    // the layout root is gone).
    for (DockGroupHost* host : m_groupHosts) {
        if (!host) {
            continue;
        }
        for (DockPanel* panel : host->hostedPanels()) {
            host->releasePanel(panel);
        }
        delete host;
    }
    m_groupHosts.clear();
}

DockLeafNode* DockLayoutRoot::addPanel(DockPanel* panel, DockPosition position)
{
    if (!panel || position == DockPosition::None) {
        return nullptr;
    }

    m_inOperation = true;

    DockLeafNode* result = nullptr;

    if (isEmpty()) {
        // Center or first panel - just add as root leaf
        result = addPanelToEmpty(panel);
    } else if (position == DockPosition::Center) {
        // Center when not empty - add to the right by default
        result = addPanelAtRoot(panel, DockPosition::Right);
    } else {
        result = addPanelAtRoot(panel, position);
    }

    if (result) {
        // Edge-docked side panels keep fixed width when container resizes.
        // This allows center content (canvas) to absorb monitor-dependent growth.
        applyEdgeAnchoring(result, panel, position);

        syncHandleWidgets();

        if (m_rootBounds.isValid()) {
            setRootBounds(m_rootBounds);
        }

        emit panelAdded(panel);
        emit layoutChanged();
    }

    m_inOperation = false;
    return result;
}

DockLeafNode* DockLayoutRoot::addPanelRelativeTo(
    DockPanel* panel, DockPanel* relativeTo, DockPosition position)
{
    if (!panel || !relativeTo || position == DockPosition::None) {
        return nullptr;
    }

    // Center means "next to" - default to Right
    if (position == DockPosition::Center) {
        position = DockPosition::Right;
    }

    DockLeafNode* targetLeaf = findLeafForPanel(relativeTo);
    if (!targetLeaf) {
        return nullptr;
    }

    m_inOperation = true;

    DockLeafNode* result = addPanelByLeafSplit(panel, targetLeaf, position);

    if (result) {
        // Preserve fixed-width behavior for Left/Right panel insertions.
        applyEdgeAnchoring(result, panel, position);

        syncHandleWidgets();

        if (m_rootBounds.isValid()) {
            setRootBounds(m_rootBounds);
        }

        emit panelAdded(panel);
        emit layoutChanged();
    }

    m_inOperation = false;
    return result;
}

bool DockLayoutRoot::removePanel(DockPanel* panel)
{
    if (!panel) {
        return false;
    }

    DockLeafNode* leaf = findLeafForPanel(panel);
    if (!leaf) {
        return false;
    }

    m_inOperation = true;

    // Remove this exact panel — takePanel() would drop the group's CURRENT
    // member, which is not necessarily the one being removed.
    leaf->removePanel(panel);
    // syncGroupHosts() only reaches panels still in the tree; this one is out.
    panel->setGrouped(false);

    // Cleanup empty nodes
    cleanupEmptyNodes();

    syncHandleWidgets();

    if (m_rootBounds.isValid() && m_root) {
        setRootBounds(m_rootBounds);
    }

    emit panelRemoved(panel);
    emit layoutChanged();

    m_inOperation = false;
    return true;
}

DockLeafNode* DockLayoutRoot::findLeafForPanel(DockPanel* panel) const
{
    if (!m_root || !panel) {
        return nullptr;
    }

    if (m_root->isLeaf()) {
        auto* leaf = static_cast<DockLeafNode*>(m_root.get());
        return leaf->containsPanel(panel) ? leaf : nullptr;
    }

    auto* split = static_cast<DockSplitNode*>(m_root.get());
    return split->findLeafForPanel(panel);
}

QList<DockPanel*> DockLayoutRoot::allPanels() const
{
    QList<DockPanel*> result;

    if (!m_root) {
        return result;
    }

    if (m_root->isLeaf()) {
        result.append(static_cast<DockLeafNode*>(m_root.get())->panels());
    } else {
        auto* split = static_cast<DockSplitNode*>(m_root.get());
        for (DockLeafNode* leaf : split->allLeaves()) {
            result.append(leaf->panels());
        }
    }

    return result;
}

QList<DockPanel*> DockLayoutRoot::visiblePanels() const
{
    QList<DockPanel*> result;

    if (!m_root) {
        return result;
    }

    if (m_root->isLeaf()) {
        if (DockPanel* p = static_cast<DockLeafNode*>(m_root.get())->panel()) {
            result.append(p);
        }
    } else {
        auto* split = static_cast<DockSplitNode*>(m_root.get());
        for (DockLeafNode* leaf : split->allLeaves()) {
            if (DockPanel* p = leaf->panel()) {
                result.append(p);
            }
        }
    }

    return result;
}

bool DockLayoutRoot::canGroupPanels(DockPanel* panel, DockPanel* relativeTo) const
{
    if (!panel || !relativeTo || panel == relativeTo) {
        return false;
    }

    DockLeafNode* targetLeaf = findLeafForPanel(relativeTo);
    if (!targetLeaf) {
        return false;
    }

    // Both sides must accept grouping, otherwise the caller falls back to a
    // normal split.
    if (!panel->features().testFlag(PanelFeature::Groupable)) {
        return false;
    }
    for (DockPanel* member : targetLeaf->panels()) {
        if (member && !member->features().testFlag(PanelFeature::Groupable)) {
            return false;
        }
    }

    return true;
}

DockLeafNode* DockLayoutRoot::addPanelToGroup(
    DockPanel* panel, DockPanel* relativeTo, int insertIndex, bool makeCurrent)
{
    if (!canGroupPanels(panel, relativeTo)) {
        return nullptr;
    }

    DockLeafNode* targetLeaf = findLeafForPanel(relativeTo);
    if (!targetLeaf) {
        return nullptr;
    }

    m_inOperation = true;

    // The panel may already live elsewhere in the tree (re-grouping a docked
    // panel); take it out first so it is never present twice.
    if (DockLeafNode* sourceLeaf = findLeafForPanel(panel)) {
        sourceLeaf->removePanel(panel);
        if (sourceLeaf != targetLeaf && sourceLeaf->isEmpty()) {
            cleanupEmptyNodes();
            // cleanupEmptyNodes may have collapsed the tree around the target,
            // but never destroys a non-empty leaf, so targetLeaf stays valid.
        }
    }

    if (insertIndex < 0) {
        insertIndex = targetLeaf->indexOfPanel(relativeTo) + 1;
    }
    targetLeaf->insertPanel(insertIndex, panel);
    if (makeCurrent) {
        targetLeaf->setCurrentPanel(panel);
    }

    syncHandleWidgets();

    if (m_rootBounds.isValid()) {
        setRootBounds(m_rootBounds);
    }

    emit panelAdded(panel);
    emit layoutChanged();

    m_inOperation = false;
    return targetLeaf;
}

bool DockLayoutRoot::setCurrentPanelInGroup(DockPanel* panel)
{
    DockLeafNode* leaf = findLeafForPanel(panel);
    if (!leaf || !leaf->setCurrentPanel(panel)) {
        return false;
    }

    if (m_rootBounds.isValid()) {
        setRootBounds(m_rootBounds);
    }

    emit layoutChanged();
    return true;
}

DockLeafNode* DockLayoutRoot::findLeafAt(const QPoint& globalPos) const
{
    DockPanel* panel = findPanelAt(globalPos);
    return panel ? findLeafForPanel(panel) : nullptr;
}

DockPanel* DockLayoutRoot::findPanelAt(const QPoint& globalPos) const
{
    QList<DockPanel*> panels = allPanels();

    for (DockPanel* panel : panels) {
        if (!panel || !panel->isVisible()) {
            continue;
        }

        QRect panelRect = panel->rect();
        QPoint panelTopLeft = panel->mapToGlobal(panelRect.topLeft());
        QRect globalRect(panelTopLeft, panelRect.size());

        if (globalRect.contains(globalPos)) {
            return panel;
        }
    }

    return nullptr;
}

void DockLayoutRoot::setRootBounds(const QRect& bounds)
{
    m_rootBounds = bounds;

    if (m_root && bounds.isValid()) {
        m_root->setBounds(bounds);
    }
}

void DockLayoutRoot::syncHandleWidgets()
{
    if (m_destroying) {
        return;
    }

    // Collect all current split nodes
    QSet<DockSplitNode*> currentSplits;

    std::function<void(DockLayoutNode*)> collectSplits = [&](DockLayoutNode* node) {
        if (!node)
            return;
        if (node->isSplit()) {
            auto* split = static_cast<DockSplitNode*>(node);
            currentSplits.insert(split);
            for (int i = 0; i < split->childCount(); ++i) {
                collectSplits(split->childAt(i));
            }
        }
    };

    collectSplits(m_root.get());

    // Remove handles for splits that no longer exist
    QList<DockSplitNode*> toRemove;
    for (auto it = m_handles.begin(); it != m_handles.end(); ++it) {
        if (!currentSplits.contains(it.key())) {
            toRemove.append(it.key());
        }
    }

    for (DockSplitNode* split : toRemove) {
        removeHandlesForSplit(split);
    }

    // Create/update handles for current splits
    for (DockSplitNode* split : currentSplits) {
        int neededHandles = split->childCount() - 1;

        if (!m_handles.contains(split)) {
            m_handles[split] = QList<DockSplitHandle*>();
        }

        QList<DockSplitHandle*>& handles = m_handles[split];

        // If handle count changed (not just excess removal), we need to recreate
        // all handles to ensure correct ordering. This is because when a child
        // is inserted at the beginning, existing handles shift indices but stay
        // at the end of the list, causing incorrect index assignment.
        if (handles.size() != neededHandles) {
            // Delete all existing handles for this split
            qDeleteAll(handles);
            handles.clear();

            // Create all handles fresh in correct order
            for (int i = 0; i < neededHandles; ++i) {
                auto* handle = new DockSplitHandle(split, i, split->direction(), m_containerWidget);
                handle->applyTheme(m_colors);

                connect(handle, &DockSplitHandle::dragStarted, this,
                    &DockLayoutRoot::onHandleDragStarted);
                connect(
                    handle, &DockSplitHandle::dragMoved, this, &DockLayoutRoot::onHandleDragMoved);
                connect(handle, &DockSplitHandle::dragFinished, this,
                    &DockLayoutRoot::onHandleDragFinished);

                handles.append(handle);
            }
        } else {
            // Same count - just update indices and directions
            for (int i = 0; i < handles.size(); ++i) {
                handles[i]->setHandleIndex(i);
                handles[i]->setDirection(split->direction());
            }
        }

        // Setup geometry callback
        setupHandleCallback(split);
    }

    // Group frames follow the tree's structure exactly like handles do.
    syncGroupHosts();
}

// ============================================================================
// Group Hosts
// ============================================================================

QList<DockLeafNode*> DockLayoutRoot::allLeafNodes() const
{
    QList<DockLeafNode*> result;
    if (!m_root) {
        return result;
    }

    if (m_root->isLeaf()) {
        result.append(static_cast<DockLeafNode*>(m_root.get()));
    } else {
        result = static_cast<DockSplitNode*>(m_root.get())->allLeaves();
    }
    return result;
}

QWidget* DockLayoutRoot::cellWidget(DockLeafNode* leaf) const
{
    if (!leaf) {
        return nullptr;
    }
    if (QWidget* host = leaf->groupHost()) {
        return host;
    }
    return leaf->panel();
}

QList<QWidget*> DockLayoutRoot::allCellWidgets() const
{
    QList<QWidget*> result;
    for (DockLeafNode* leaf : allLeafNodes()) {
        if (QWidget* w = cellWidget(leaf)) {
            result.append(w);
        }
    }
    return result;
}

DockGroupHost* DockLayoutRoot::groupHostForPanel(DockPanel* panel) const
{
    DockLeafNode* leaf = findLeafForPanel(panel);
    return leaf ? static_cast<DockGroupHost*>(leaf->groupHost()) : nullptr;
}

DockGroupHost* DockLayoutRoot::groupHostAtHeader(const QPoint& globalPos) const
{
    for (DockGroupHost* host : m_groupHosts) {
        if (!host || !host->isVisible()) {
            continue;
        }
        DockGroupHeader* header = host->header();
        if (!header || !header->isVisible()) {
            continue;
        }
        const QRect headerRect(header->mapToGlobal(QPoint(0, 0)), header->size());
        if (headerRect.contains(globalPos)) {
            return host;
        }
    }
    return nullptr;
}

void DockLayoutRoot::syncGroupHosts()
{
    if (m_destroying || !m_containerWidget) {
        return;
    }

    QSet<DockGroupHost*> live;

    for (DockLeafNode* leaf : allLeafNodes()) {
        // The panels cannot see the tree, so the tree tells them. Done for
        // every leaf, group or not, so a cell that just collapsed clears it.
        const bool grouped = leaf->isGroup();
        for (DockPanel* member : leaf->panels()) {
            if (member) {
                member->setGrouped(grouped);
            }
        }

        if (!grouped) {
            auto* host = static_cast<DockGroupHost*>(leaf->groupHost());
            if (host) {
                // The cell has dropped to one member. If the VISIBLE one is what
                // left, the survivor slides into its place first — inside the
                // frame, which still clips it — and the frame reports back when
                // that has landed. Either way it stays attached to the leaf, so
                // it must count as live or the stale sweep below would delete it
                // mid-animation.
                if (host->isCollapsing() || host->beginCollapse()) {
                    live.insert(host);
                } else {
                    destroyGroupHost(leaf);
                }
            }
            continue;
        }

        auto* host = static_cast<DockGroupHost*>(leaf->groupHost());
        if (!host) {
            host = new DockGroupHost(leaf, m_containerWidget);
            host->setAnimationsEnabled(m_layoutAnimationEnabled);
            host->setAnimationDuration(m_layoutAnimationDuration);
            host->applyTheme(m_colors);
            connectGroupHost(host);

            leaf->setGroupHost(host);
            m_groupHosts.insert(host);

            // The node reserves space for the header in its size constraints,
            // so it must know the theme-scaled height the widget actually uses.
            leaf->setHeaderHeight(host->headerHeight());

            if (leaf->bounds().isValid()) {
                host->setGeometry(leaf->bounds());
            }
            host->show();
            emit groupHostCreated(host);
        }

        host->syncFromLeaf();
        live.insert(host);
    }

    // Whatever is left belongs to a leaf that no longer exists (or no longer
    // groups). Its key would be dangling, hence the pointer-set sweep.
    const QSet<DockGroupHost*> stale = m_groupHosts - live;
    for (DockGroupHost* host : stale) {
        m_groupHosts.remove(host);
        emit groupHostAboutToBeDestroyed(host);
        // Its leaf is already gone (a whole-tree replacement destroys every
        // leaf at once), so the frame's m_leaf dangles from here on and the
        // panels it lists may already belong to the frames built above.
        // Detaching now, synchronously, is what keeps the deferred delete from
        // reclaiming a panel another frame is showing.
        host->detachFromLayout();
        host->hide();
        host->deleteLater();
    }
}

void DockLayoutRoot::connectGroupHost(DockGroupHost* host)
{
    connect(host, &DockGroupHost::panelActivationRequested, this, [this, host](DockPanel* panel) {
        DockLeafNode* leaf = host->leaf();
        if (!leaf || !leaf->setCurrentPanel(panel)) {
            return;
        }
        host->setCurrentPanel(panel, /*animated=*/true);
        emit layoutChanged();
    });

    connect(host, &DockGroupHost::collapseFinished, this, [this, host]() {
        if (m_destroying) {
            return;
        }
        // The leaf pointer captured at connect time could be dangling by now —
        // the collapse takes a couple of hundred ms and tree surgery is free to
        // run in between. Find the frame by identity among the LIVE leaves; if
        // it belongs to none, it was orphaned and syncGroupHosts' sweep owns it.
        for (DockLeafNode* leaf : allLeafNodes()) {
            if (leaf->groupHost() != host) {
                continue;
            }

            destroyGroupHost(leaf);

            // Re-apply the layout by hand. Every other call site runs INSIDE a
            // layout update, so the cell is re-laid out right after; this one
            // arrives from an animation, long after the layout settled. Without
            // it the survivor keeps the member-area geometry it had under the
            // strip — a header-high strip of dead space above it, forever — and
            // destroyGroupHost's grow-in animation reads that same rect as its
            // target and does nothing.
            setRootBounds(m_rootBounds);
            return;
        }
    });

    connect(
        host, &DockGroupHost::panelCloseRequested, this, &DockLayoutRoot::groupPanelCloseRequested);
    connect(host, &DockGroupHost::panelDragStarted, this, &DockLayoutRoot::groupPanelDragStarted);
    connect(host, &DockGroupHost::panelDragMoved, this, &DockLayoutRoot::groupPanelDragMoved);
    connect(host, &DockGroupHost::panelDragFinished, this, &DockLayoutRoot::groupPanelDragFinished);
}

void DockLayoutRoot::destroyGroupHost(DockLeafNode* leaf)
{
    if (!leaf) {
        return;
    }

    auto* host = static_cast<DockGroupHost*>(leaf->groupHost());
    if (!host) {
        leaf->setGroupHost(nullptr);
        return;
    }

    // One last sync while the frame still owns its members: it releases the
    // departed one and promotes the survivor to visible current member. Without
    // this, losing the member that WAS current leaves the cell blank — the
    // survivor was hidden, and a solo leaf's setBounds only moves its panel, it
    // never shows it.
    host->syncFromLeaf();

    leaf->setGroupHost(nullptr);

    // Where the survivor is standing right now — below the header strip it is
    // about to reclaim. This is the only part of the flight we can know here.
    DockPanel* survivor = leaf->panel();
    const QRect memberRect = host->memberAreaInParent();

    // Hand every hosted panel back to the container before the frame goes, so
    // the survivor keeps painting while the layout settles.
    for (DockPanel* panel : host->hostedPanels()) {
        host->releasePanel(panel);
        if (panel) {
            panel->setGrouped(false);
        }
    }

    m_groupHosts.remove(host);
    emit groupHostAboutToBeDestroyed(host);

    const bool animate = m_layoutAnimationEnabled && m_layoutAnimationDuration > 0
        && host->isVisible() && survivor && survivor->isVisible() && memberRect.isValid();

    if (!animate) {
        host->hide();
        // deleteLater, not delete: this runs from inside header signal handlers
        // (dragging the last-but-one tab out of the group).
        host->deleteLater();
        return;
    }

    // The survivor grows into the vacated strip while the strip collapses into
    // it: same duration, so they finish as one motion.
    //
    // The DESTINATION cannot be read here. This runs mid-surgery, and the
    // caller often is not done — ungroupPanel() splits this very cell moments
    // later, and cleanupEmptyNodes() can move it. Taking leaf->bounds() now
    // would fly the panel to a rectangle that no longer exists by the time it
    // lands. So the target is read on the next turn, once every synchronous
    // setRootBounds has settled the layout; this handler runs before those, so
    // its queued call is delivered before the frame is composited.
    QPointer<DockPanel> survivorRef(survivor);
    const int duration = m_layoutAnimationDuration;
    QTimer::singleShot(0, survivor, [survivorRef, memberRect, duration]() {
        DockPanel* panel = survivorRef.data();
        if (!panel || !panel->isVisible() || panel->isAnimatingDocking()) {
            return;
        }
        const QRect target = panel->geometry();
        if (!target.isValid() || target == memberRect) {
            return;
        }
        panel->animateDocking(memberRect, target, duration);
    });

    host->runFarewell(duration);
}

void DockLayoutRoot::applyTheme(const ruwa::ui::core::ThemeColors& colors)
{
    m_colors = colors;

    for (auto it = m_handles.begin(); it != m_handles.end(); ++it) {
        for (DockSplitHandle* handle : it.value()) {
            handle->applyTheme(colors);
        }
    }

    for (DockGroupHost* host : m_groupHosts) {
        if (!host) {
            continue;
        }
        host->applyTheme(colors);

        // The strip is theme-scaled, so a theme with a different scale changes
        // how much of the cell the members actually get. The node reserves that
        // space in its size constraints and has to be told.
        if (DockLeafNode* leaf = host->leaf()) {
            leaf->setHeaderHeight(host->headerHeight());
        }
    }
}

void DockLayoutRoot::setLayoutAnimationEnabled(bool enabled)
{
    m_layoutAnimationEnabled = enabled;
    for (DockGroupHost* host : m_groupHosts) {
        if (host) {
            host->setAnimationsEnabled(enabled);
        }
    }
}

void DockLayoutRoot::setLayoutAnimationDuration(int ms)
{
    m_layoutAnimationDuration = qMax(0, ms);
    for (DockGroupHost* host : m_groupHosts) {
        if (host) {
            host->setAnimationDuration(m_layoutAnimationDuration);
        }
    }
}

void DockLayoutRoot::raiseHandles()
{
    if (m_destroying) {
        return;
    }

    // Raise all handle widgets to ensure they are above panels
    for (auto it = m_handles.begin(); it != m_handles.end(); ++it) {
        for (DockSplitHandle* handle : it.value()) {
            if (handle && handle->isVisible()) {
                handle->raise();
            }
        }
    }
}

QString DockLayoutRoot::debugString() const
{
    if (!m_root) {
        return QStringLiteral("(empty)");
    }
    return m_root->debugString();
}

std::optional<PanelPlacement> DockLayoutRoot::getPanelPlacement(DockPanel* panel) const
{
    if (!panel || !m_root)
        return std::nullopt;

    DockLeafNode* leaf = findLeafForPanel(panel);
    if (!leaf)
        return std::nullopt;

    // Center means "back into the same cell as this sibling".
    // DockContainerWidget::addPanelRelativeTo reads it that way and re-groups;
    // if the two are no longer groupable it degrades to docking beside it.
    if (leaf->isGroup()) {
        for (DockPanel* member : leaf->panels()) {
            if (member && member != panel) {
                return PanelPlacement::relativeToPanel(
                    panel->persistentKey(), member->persistentKey(), DockPosition::Center);
            }
        }
    }

    DockLayoutNode* parent = leaf->parent();
    if (!parent) {
        // Single panel at root
        return PanelPlacement::atEdge(panel->persistentKey(), DockPosition::Center);
    }

    auto* split = static_cast<DockSplitNode*>(parent);
    const int index = split->indexOf(leaf);
    if (index < 0)
        return std::nullopt;

    const SplitDirection dir = split->direction();
    DockPanel* siblingPanel = nullptr;
    DockPosition position = DockPosition::Right;

    if (index > 0) {
        siblingPanel = getFirstPanelInNode(split->childAt(index - 1));
        position = (dir == SplitDirection::Horizontal) ? DockPosition::Right : DockPosition::Bottom;
    } else if (index + 1 < split->childCount()) {
        siblingPanel = getFirstPanelInNode(split->childAt(index + 1));
        position = (dir == SplitDirection::Horizontal) ? DockPosition::Left : DockPosition::Top;
    }

    if (!siblingPanel)
        return std::nullopt;

    return PanelPlacement::relativeToPanel(
        panel->persistentKey(), siblingPanel->persistentKey(), position);
}

QJsonObject DockLayoutRoot::toJson() const
{
    QJsonObject rootObj;
    rootObj["version"] = 1;
    rootObj["hasRoot"] = static_cast<bool>(m_root);
    if (m_root) {
        rootObj["root"] = serializeNode(m_root.get());
    }
    return rootObj;
}

bool DockLayoutRoot::fromJson(const QJsonObject& json,
    const std::function<DockPanel*(const QString& panelId, const QString& panelTitle)>&
        panelResolver)
{
    if (json.isEmpty() || !panelResolver) {
        return false;
    }

    const int version = json.value("version").toInt(0);
    if (version <= 0 || version > 1) {
        return false;
    }

    if (!json.value("hasRoot").toBool(false)) {
        m_root.reset();
        syncHandleWidgets();
        emit layoutChanged();
        return true;
    }

    const QJsonObject rootNodeObj = json.value("root").toObject();
    DockLayoutNodePtr newRoot = deserializeNode(rootNodeObj, panelResolver);
    if (!newRoot) {
        return false;
    }

    m_root = std::move(newRoot);
    const bool repaired = repairLayout();
    if (!repaired) {
        syncHandleWidgets();
        if (m_rootBounds.isValid()) {
            setRootBounds(m_rootBounds);
        }
    }

    emit layoutChanged();
    return true;
}

bool DockLayoutRoot::repairLayout()
{
    if (!m_root) {
        syncHandleWidgets();
        return false;
    }

    QSet<DockPanel*> seenPanels;
    const bool changed = sanitizeNodeRecursive(m_root, seenPanels);

    syncHandleWidgets();

    if (m_root && m_rootBounds.isValid()) {
        m_root->setBounds(m_rootBounds);
    }

    if (changed) {
        emit layoutChanged();
    }

    return changed;
}

QJsonObject DockLayoutRoot::serializeNode(const DockLayoutNode* node) const
{
    QJsonObject obj;
    if (!node) {
        return obj;
    }

    obj["anchor"] = anchorToString(node->anchor());
    obj["anchoredSize"] = node->anchoredSize();

    if (node->isLeaf()) {
        const auto* leaf = static_cast<const DockLeafNode*>(node);
        obj["type"] = QStringLiteral("leaf");

        const QList<DockPanel*> members = leaf->panels();

        // Single-panel leaves keep the flat pre-group format so older builds
        // (and every existing preset resource) still read them.
        if (DockPanel* current = leaf->panel()) {
            obj["panelId"] = current->persistentKey();
            obj["panelTitle"] = current->title();
        }

        if (members.size() > 1) {
            QJsonArray panelsArray;
            for (DockPanel* member : members) {
                if (!member) {
                    continue;
                }
                QJsonObject memberObj;
                memberObj["panelId"] = member->persistentKey();
                memberObj["panelTitle"] = member->title();
                panelsArray.append(memberObj);
            }
            obj["panels"] = panelsArray;
            obj["currentIndex"] = leaf->currentIndex();
            obj["headerHeight"] = leaf->headerHeight();
        }
        return obj;
    }

    const auto* split = static_cast<const DockSplitNode*>(node);
    obj["type"] = QStringLiteral("split");
    obj["direction"] = (split->direction() == SplitDirection::Horizontal)
        ? QStringLiteral("horizontal")
        : QStringLiteral("vertical");

    QJsonArray sizesArray;
    for (int size : split->sizes()) {
        sizesArray.append(size);
    }
    obj["sizes"] = sizesArray;

    QJsonArray childrenArray;
    for (int i = 0; i < split->childCount(); ++i) {
        if (DockLayoutNode* child = split->childAt(i)) {
            childrenArray.append(serializeNode(child));
        }
    }
    obj["children"] = childrenArray;
    return obj;
}

DockLayoutNodePtr DockLayoutRoot::deserializeNode(const QJsonObject& nodeObj,
    const std::function<DockPanel*(const QString& panelId, const QString& panelTitle)>&
        panelResolver) const
{
    if (nodeObj.isEmpty()) {
        return nullptr;
    }

    const QString type = nodeObj.value("type").toString();
    const Anchor anchor = anchorFromString(nodeObj.value("anchor").toString());
    const int anchoredSize = nodeObj.value("anchoredSize").toInt(0);

    if (type == QLatin1String("leaf")) {
        auto leaf = std::make_unique<DockLeafNode>();

        // Grouped leaf: "panels" is authoritative. The flat panelId/panelTitle
        // pair is still written for the current member, so a group read by an
        // older build degrades to that one panel instead of failing.
        const QJsonArray panelsArray = nodeObj.value("panels").toArray();
        if (!panelsArray.isEmpty()) {
            for (const QJsonValue& value : panelsArray) {
                const QJsonObject memberObj = value.toObject();
                DockPanel* member = panelResolver(memberObj.value("panelId").toString(),
                    memberObj.value("panelTitle").toString());
                if (member) {
                    leaf->addPanel(member);
                }
            }

            if (!leaf->hasPanel()) {
                return nullptr;
            }

            leaf->setCurrentIndex(nodeObj.value("currentIndex").toInt(0));
            const int header = nodeObj.value("headerHeight").toInt(kDefaultGroupHeaderHeight);
            if (header > 0) {
                leaf->setHeaderHeight(header);
            }
        } else {
            DockPanel* panel = panelResolver(
                nodeObj.value("panelId").toString(), nodeObj.value("panelTitle").toString());
            if (!panel) {
                return nullptr;
            }
            leaf->setPanel(panel);
        }

        leaf->setAnchor(anchor);
        leaf->setAnchoredSize(anchoredSize);
        return leaf;
    }

    if (type != QLatin1String("split")) {
        return nullptr;
    }

    const QString directionValue = nodeObj.value("direction").toString();
    const SplitDirection direction = (directionValue == QLatin1String("vertical"))
        ? SplitDirection::Vertical
        : SplitDirection::Horizontal;

    auto split = std::make_unique<DockSplitNode>(direction);
    split->setAnchor(anchor);
    split->setAnchoredSize(anchoredSize);

    const QJsonArray childrenArray = nodeObj.value("children").toArray();
    for (const QJsonValue& childValue : childrenArray) {
        DockLayoutNodePtr child = deserializeNode(childValue.toObject(), panelResolver);
        if (!child) {
            return nullptr;
        }
        split->addChild(std::move(child));
    }

    if (split->childCount() == 0) {
        return nullptr;
    }

    const QJsonArray sizesArray = nodeObj.value("sizes").toArray();
    if (sizesArray.size() == split->childCount()) {
        QList<int> sizes;
        sizes.reserve(sizesArray.size());
        for (const QJsonValue& value : sizesArray) {
            sizes.append(value.toInt());
        }
        split->setSizes(sizes);
    }

    return split;
}

bool DockLayoutRoot::sanitizeNodeRecursive(DockLayoutNodePtr& node, QSet<DockPanel*>& seenPanels)
{
    if (!node) {
        return false;
    }

    if (node->isLeaf()) {
        auto* leaf = static_cast<DockLeafNode*>(node.get());
        const bool changed = sanitizeLeafNode(leaf, seenPanels);
        if (!leaf->hasPanel()) {
            node.reset();
            return true;
        }
        return changed;
    }

    auto* split = static_cast<DockSplitNode*>(node.get());
    bool changed = sanitizeSplitNode(split, seenPanels);

    if (split->childCount() == 0) {
        node.reset();
        return true;
    }

    if (split->childCount() == 1) {
        DockLayoutNodePtr collapsedChild = split->removeChildAt(0);
        if (collapsedChild) {
            collapsedChild->setParent(node->parent());
            node = std::move(collapsedChild);
            return true;
        }
    }

    return changed;
}

bool DockLayoutRoot::sanitizeLeafNode(DockLeafNode* leaf, QSet<DockPanel*>& seenPanels)
{
    if (!leaf) {
        return false;
    }

    bool changed = false;

    // Drop null and duplicate members. A panel may appear exactly once in the
    // whole tree; a group leaf makes it possible to violate that per member,
    // not just per leaf.
    for (DockPanel* member : leaf->panels()) {
        if (!member || seenPanels.contains(member)) {
            leaf->removePanel(member);
            changed = true;
            continue;
        }
        seenPanels.insert(member);
    }

    return changed;
}

bool DockLayoutRoot::sanitizeSplitNode(DockSplitNode* split, QSet<DockPanel*>& seenPanels)
{
    if (!split) {
        return false;
    }

    bool changed = false;

    for (int i = split->childCount() - 1; i >= 0; --i) {
        DockLayoutNode* child = split->childAt(i);
        if (!child) {
            split->removeChildAt(i);
            changed = true;
            continue;
        }

        if (child->isLeaf()) {
            auto* leaf = static_cast<DockLeafNode*>(child);
            changed = sanitizeLeafNode(leaf, seenPanels) || changed;
            if (!leaf->hasPanel()) {
                split->removeChildAt(i);
                changed = true;
            }
            continue;
        }

        auto* childSplit = static_cast<DockSplitNode*>(child);
        changed = sanitizeSplitNode(childSplit, seenPanels) || changed;

        if (childSplit->childCount() == 0) {
            split->removeChildAt(i);
            changed = true;
            continue;
        }

        if (childSplit->childCount() == 1) {
            DockLayoutNodePtr collapsedChild = childSplit->removeChildAt(0);
            if (collapsedChild) {
                collapsedChild->setParent(split);
                split->replaceChild(i, std::move(collapsedChild));
                changed = true;
            }
        }
    }

    return changed;
}

void DockLayoutRoot::onHandleDragStarted(DockSplitNode* node, int handleIndex)
{
    if (node) {
        node->beginHandleDrag(handleIndex);
    }

    // Suppress the canvas custom cursor for the duration of the resize: the panel edge
    // trails the pointer slightly, so the cursor keeps crossing onto the canvas and would
    // otherwise flicker the brush/tool cursor on. The split cursor stays (handle keeps the
    // implicit mouse grab).
    setUiDragActive(true);
}

void DockLayoutRoot::onHandleDragMoved(DockSplitNode* node, int handleIndex, int delta)
{
    if (!node) {
        return;
    }

    node->handleDrag(handleIndex, delta);
    emit layoutChanged();
}

void DockLayoutRoot::onHandleDragFinished(DockSplitNode* node, int handleIndex)
{
    Q_UNUSED(handleIndex)

    // Clear unconditionally (before the null guard) so the suppression flag can never
    // get stuck if the drag ends with a stale node.
    setUiDragActive(false);

    if (!node) {
        return;
    }

    node->endHandleDrag();

    // Save the new sizes of affected panels as their user preferred docked sizes
    // This ensures that when a panel is re-docked later, it gets its last user-set size
    SplitDirection direction = node->direction();
    const QList<int>& sizes = node->sizes();

    for (int i = 0; i < node->childCount(); ++i) {
        DockLayoutNode* child = node->childAt(i);
        if (!child)
            continue;

        // Get panel(s) in this child
        QList<DockPanel*> panels;
        if (child->isLeaf()) {
            DockPanel* p = static_cast<DockLeafNode*>(child)->panel();
            if (p)
                panels.append(p);
        } else if (child->isSplit()) {
            QList<DockLeafNode*> leaves = static_cast<DockSplitNode*>(child)->allLeaves();
            for (DockLeafNode* leaf : leaves) {
                if (leaf && leaf->panel()) {
                    panels.append(leaf->panel());
                }
            }
        }

        // Save the size in the split direction for each panel
        // Use direction-specific methods to keep horizontal and vertical sizes separate
        int size = (i < sizes.size()) ? sizes[i] : 0;
        if (size > 0) {
            if (child->anchor() != Anchor::None) {
                child->captureAnchoredSize(direction);
            }
            for (DockPanel* panel : panels) {
                if (direction == SplitDirection::Horizontal) {
                    // Save width - this was a horizontal split (panels side by side)
                    panel->setUserHorizontalDockedWidth(size);
                } else {
                    // Save height - this was a vertical split (panels stacked)
                    panel->setUserVerticalDockedHeight(size);
                }
            }
        }
    }
}

SplitDirection DockLayoutRoot::positionToDirection(DockPosition position)
{
    switch (position) {
    case DockPosition::Left:
    case DockPosition::Right:
        return SplitDirection::Horizontal;
    case DockPosition::Top:
    case DockPosition::Bottom:
        return SplitDirection::Vertical;
    default:
        return SplitDirection::Horizontal;
    }
}

bool DockLayoutRoot::isFirstPosition(DockPosition position)
{
    return position == DockPosition::Left || position == DockPosition::Top;
}

DockLeafNode* DockLayoutRoot::addPanelToEmpty(DockPanel* panel)
{
    auto leaf = std::make_unique<DockLeafNode>(panel);
    DockLeafNode* result = leaf.get();
    m_root = std::move(leaf);
    return result;
}

DockLeafNode* DockLayoutRoot::addPanelAtRoot(DockPanel* panel, DockPosition position)
{
    SplitDirection newDirection = positionToDirection(position);
    bool insertFirst = isFirstPosition(position);

    // Check if we can reuse existing root split
    if (m_root->isSplit()) {
        auto* rootSplit = static_cast<DockSplitNode*>(m_root.get());
        if (rootSplit->direction() == newDirection) {
            // Same direction - just add to existing split
            auto newLeaf = std::make_unique<DockLeafNode>(panel);
            DockLeafNode* result = newLeaf.get();

            if (insertFirst) {
                rootSplit->insertChild(0, std::move(newLeaf));
            } else {
                rootSplit->addChild(std::move(newLeaf));
            }

            return result;
        }
    }

    // Need to create new split containing old root
    auto newSplit = std::make_unique<DockSplitNode>(newDirection);
    auto newLeaf = std::make_unique<DockLeafNode>(panel);
    DockLeafNode* result = newLeaf.get();

    if (insertFirst) {
        newSplit->addChild(std::move(newLeaf));
        newSplit->addChild(std::move(m_root));
    } else {
        newSplit->addChild(std::move(m_root));
        newSplit->addChild(std::move(newLeaf));
    }

    m_root = std::move(newSplit);
    return result;
}

DockLeafNode* DockLayoutRoot::addPanelByLeafSplit(
    DockPanel* panel, DockLeafNode* leaf, DockPosition position)
{
    SplitDirection newDirection = positionToDirection(position);
    bool insertFirst = isFirstPosition(position);
    const Anchor inheritedAnchor = leaf ? leaf->anchor() : Anchor::None;
    const int inheritedAnchoredSize = leaf ? leaf->anchoredSize() : 0;

    // Find parent of this leaf
    DockLayoutNode* parent = leaf->parent();

    if (parent && parent->isSplit()) {
        auto* parentSplit = static_cast<DockSplitNode*>(parent);

        if (parentSplit->direction() == newDirection) {
            // Same direction as parent - insert into parent split
            int leafIndex = parentSplit->indexOf(leaf);
            auto newLeaf = std::make_unique<DockLeafNode>(panel);
            DockLeafNode* result = newLeaf.get();

            int insertIndex = insertFirst ? leafIndex : leafIndex + 1;
            parentSplit->insertChild(insertIndex, std::move(newLeaf));

            return result;
        }
    }

    // Need to create new split replacing the leaf
    // IMPORTANT: Use replaceChild() instead of remove+insert to preserve the cell size!
    // This prevents the new panel's preferredWidth from expanding the cell when
    // docking to Top/Bottom of another panel (inner drop / "splitting" a panel)
    auto newSplit = std::make_unique<DockSplitNode>(newDirection);
    // If a leaf had anchoring in the parent split (e.g. right sidebar width),
    // keep it on the replacement split so the whole column remains fixed.
    newSplit->setAnchor(inheritedAnchor);
    newSplit->setAnchoredSize(inheritedAnchoredSize);

    auto newLeaf = std::make_unique<DockLeafNode>(panel);
    DockLeafNode* result = newLeaf.get();

    // Find leaf in parent and replace
    if (parent && parent->isSplit()) {
        auto* parentSplit = static_cast<DockSplitNode*>(parent);
        int leafIndex = parentSplit->indexOf(leaf);

        // Replace the old leaf with the new split (preserves the cell size!)
        auto oldLeaf = parentSplit->replaceChild(leafIndex, std::move(newSplit));

        // Get the new split back from parent to add children
        auto* insertedSplit = static_cast<DockSplitNode*>(parentSplit->childAt(leafIndex));

        if (insertFirst) {
            insertedSplit->addChild(std::move(newLeaf));
            insertedSplit->addChild(std::move(oldLeaf));
        } else {
            insertedSplit->addChild(std::move(oldLeaf));
            insertedSplit->addChild(std::move(newLeaf));
        }
    } else {
        // Leaf is root
        if (insertFirst) {
            newSplit->addChild(std::move(newLeaf));
            newSplit->addChild(std::move(m_root));
        } else {
            newSplit->addChild(std::move(m_root));
            newSplit->addChild(std::move(newLeaf));
        }

        m_root = std::move(newSplit);
    }

    return result;
}

void DockLayoutRoot::createHandlesForSplit(DockSplitNode* split)
{
    if (!split || m_handles.contains(split)) {
        return;
    }

    int handleCount = split->childCount() - 1;
    QList<DockSplitHandle*> handles;

    for (int i = 0; i < handleCount; ++i) {
        auto* handle = new DockSplitHandle(split, i, split->direction(), m_containerWidget);
        handle->applyTheme(m_colors);

        connect(handle, &DockSplitHandle::dragStarted, this, &DockLayoutRoot::onHandleDragStarted);
        connect(handle, &DockSplitHandle::dragMoved, this, &DockLayoutRoot::onHandleDragMoved);
        connect(
            handle, &DockSplitHandle::dragFinished, this, &DockLayoutRoot::onHandleDragFinished);

        handles.append(handle);
    }

    m_handles[split] = handles;
    setupHandleCallback(split);
}

void DockLayoutRoot::removeHandlesForSplit(DockSplitNode* split)
{
    if (!m_handles.contains(split)) {
        return;
    }

    qDeleteAll(m_handles[split]);
    m_handles.remove(split);
}

void DockLayoutRoot::cleanupEmptyNodes()
{
    if (!m_root) {
        return;
    }

    // Recursive cleanup function
    std::function<bool(DockLayoutNode*)> shouldRemove = [&](DockLayoutNode* node) -> bool {
        if (!node)
            return true;

        if (node->isLeaf()) {
            return !static_cast<DockLeafNode*>(node)->hasPanel();
        }

        auto* split = static_cast<DockSplitNode*>(node);

        // Remove empty children
        for (int i = split->childCount() - 1; i >= 0; --i) {
            if (shouldRemove(split->childAt(i))) {
                split->removeChildAt(i);
            }
        }

        // Split with no children should be removed
        if (split->isEmpty()) {
            return true;
        }

        // Split with one child should collapse
        if (split->childCount() == 1) {
            // This will be handled by collapseSingleChildSplits
        }

        return false;
    };

    // First pass: remove empty nodes
    if (m_root->isSplit()) {
        auto* rootSplit = static_cast<DockSplitNode*>(m_root.get());

        for (int i = rootSplit->childCount() - 1; i >= 0; --i) {
            if (shouldRemove(rootSplit->childAt(i))) {
                rootSplit->removeChildAt(i);
            }
        }

        // Check if root itself should be removed or collapsed
        if (rootSplit->isEmpty()) {
            m_root.reset();
            return;
        }
    } else if (m_root->isLeaf()) {
        if (shouldRemove(m_root.get())) {
            m_root.reset();
            return;
        }
    }

    // Second pass: collapse single-child splits
    if (m_root) {
        // Use a raw pointer for the recursive function, then handle root specially
        DockLayoutNode* rootPtr = m_root.get();
        collapseSingleChildSplits(rootPtr);

        // If root was collapsed to a single child, m_root is updated inside
        // collapseSingleChildSplits when it detects node == m_root.get()
    }
}

void DockLayoutRoot::collapseSingleChildSplits(DockLayoutNode*& node)
{
    // Note: This function is complex because we're working with unique_ptr ownership

    if (!node || node->isLeaf()) {
        return;
    }

    auto* split = static_cast<DockSplitNode*>(node);

    // First, recursively collapse children
    for (int i = 0; i < split->childCount(); ++i) {
        // Get the child, collapse it if needed
        DockLayoutNode* child = split->childAt(i);
        if (child && child->isSplit()) {
            auto* childSplit = static_cast<DockSplitNode*>(child);

            // Recursively process grandchildren first
            for (int j = 0; j < childSplit->childCount(); ++j) {
                DockLayoutNode* grandchild = childSplit->childAt(j);
                collapseSingleChildSplits(grandchild);
            }

            // Check if child split should collapse
            if (childSplit->childCount() == 1) {
                DockLayoutNodePtr grandchild = childSplit->removeChildAt(0);
                split->replaceChild(i, std::move(grandchild));
            }
        }
    }

    // Now check if this split should collapse
    if (split->childCount() == 1) {
        // If this is the root, we need to handle it specially
        if (node == m_root.get()) {
            DockLayoutNodePtr child = split->removeChildAt(0);
            child->setParent(nullptr);
            m_root = std::move(child);
        }
    }
}

void DockLayoutRoot::syncHandlesRecursive(DockLayoutNode* node)
{
    if (!node || node->isLeaf()) {
        return;
    }

    auto* split = static_cast<DockSplitNode*>(node);
    createHandlesForSplit(split);

    for (int i = 0; i < split->childCount(); ++i) {
        syncHandlesRecursive(split->childAt(i));
    }
}

void DockLayoutRoot::setupHandleCallback(DockSplitNode* split)
{
    if (!m_handles.contains(split)) {
        return;
    }

    // Capture split pointer to look up handles at callback time
    split->setHandleGeometryCallback([this, split](int handleIndex, const QRect& geometry) {
        if (!m_handles.contains(split)) {
            return;
        }
        const QList<DockSplitHandle*>& handles = m_handles[split];
        if (handleIndex >= 0 && handleIndex < handles.size()) {
            DockSplitHandle* handle = handles[handleIndex];

            // If preparing animation and this is a new handle (not in captured),
            // defer showing it until animation starts
            if (m_preparingAnimation && !m_capturedHandleGeometries.contains(handle)) {
                // Store the target geometry but don't show yet
                handle->setGeometry(geometry);
                m_deferredHandles.insert(handle);
                // Don't call show() - will be shown in animateLayoutChange()
            } else {
                handle->setGeometry(geometry);
                handle->show();
            }
            // Note: Do NOT call raise() here - handles are created after panels
            // so they are already above them, and floating containers manage
            // their own z-order by calling raise() when shown/dragged
        }
    });
}

// ============================================================================
// Layout Animation
// ============================================================================

DockPanel* DockLayoutRoot::getFirstPanelInNode(DockLayoutNode* node) const
{
    if (!node)
        return nullptr;

    if (node->isLeaf()) {
        return static_cast<DockLeafNode*>(node)->panel();
    }

    auto* split = static_cast<DockSplitNode*>(node);
    QList<DockLeafNode*> leaves = split->allLeaves();
    return leaves.isEmpty() ? nullptr : leaves.first()->panel();
}

QWidget* DockLayoutRoot::edgeCellWidget(
    DockLayoutNode* node, bool rightEdge, SplitDirection splitDir) const
{
    if (!node) {
        return nullptr;
    }
    if (node->isLeaf()) {
        return cellWidget(static_cast<DockLeafNode*>(node));
    }

    auto* split = static_cast<DockSplitNode*>(node);
    QList<DockLeafNode*> leaves = split->allLeaves();
    if (leaves.isEmpty()) {
        return nullptr;
    }
    // A nested split running the same way has a genuine first/last cell; one
    // running across it shares the whole edge, so any cell identifies it.
    if (split->direction() == splitDir) {
        return cellWidget(rightEdge ? leaves.last() : leaves.first());
    }
    return cellWidget(leaves.first());
}

void DockLayoutRoot::captureLayoutState()
{
    m_capturedGeometries.clear();
    m_capturedHandleGeometries.clear();
    m_capturedHandlePositions.clear();
    m_deferredHandles.clear();

    // Set flag to defer showing new handles until animation starts
    m_preparingAnimation = true;

    // Capture cell geometries
    for (QWidget* cell : allCellWidgets()) {
        if (cell && cell->isVisible()) {
            m_capturedGeometries[cell] = cell->geometry();
        }
    }

    // Capture handle geometries by POSITION (adjacent cells), not by pointer
    // This survives handle recreation in syncHandleWidgets()
    for (auto it = m_handles.begin(); it != m_handles.end(); ++it) {
        DockSplitNode* splitNode = it.key();
        const QList<DockSplitHandle*>& handles = it.value();
        SplitDirection splitDir = splitNode->direction();

        for (int i = 0; i < handles.size(); ++i) {
            DockSplitHandle* handle = handles[i];
            if (!handle || !handle->isVisible())
                continue;

            // Get adjacent cells to identify this handle position
            DockLayoutNode* leftChild = splitNode->childAt(i);
            DockLayoutNode* rightChild = splitNode->childAt(i + 1);

            // For leftChild we want its RIGHT edge cell (adjacent to handle)
            // For rightChild we want its LEFT edge cell (adjacent to handle)
            QWidget* leftCell = edgeCellWidget(leftChild, true, splitDir);
            QWidget* rightCell = edgeCellWidget(rightChild, false, splitDir);

            if (leftCell && rightCell) {
                m_capturedHandlePositions[QPair<QWidget*, QWidget*>(leftCell, rightCell)]
                    = handle->geometry();
            }

            // Also store by pointer for backward compatibility
            m_capturedHandleGeometries[handle] = handle->geometry();
        }
    }
}

void DockLayoutRoot::animateLayoutChange(DockPanel* excludePanel)
{
    // Clear the preparing flag regardless of whether we animate
    m_preparingAnimation = false;

    if (!m_layoutAnimationEnabled || m_destroying) {
        // Show any deferred handles immediately
        for (DockSplitHandle* handle : m_deferredHandles) {
            if (handle) {
                handle->show();
            }
        }
        m_deferredHandles.clear();
        m_capturedGeometries.clear();
        m_capturedHandleGeometries.clear();
        m_capturedHandlePositions.clear();
        return;
    }

    // Stop any running animation
    if (m_layoutAnimation && m_layoutAnimation->state() == QAbstractAnimation::Running) {
        m_layoutAnimation->stop();
    }

    // A grouped panel is excluded through its frame: the frame is what moves in
    // container space.
    m_excludedCell = excludePanel;
    if (excludePanel) {
        if (DockLeafNode* leaf = findLeafForPanel(excludePanel)) {
            if (QWidget* host = leaf->groupHost()) {
                m_excludedCell = host;
            }
        }
    }

    m_targetGeometries.clear();
    m_targetHandleGeometries.clear();

    // Capture new cell geometries
    for (QWidget* cell : allCellWidgets()) {
        if (cell && cell->isVisible() && cell != m_excludedCell) {
            m_targetGeometries[cell] = cell->geometry();
        }
    }

    // Capture new handle geometries (including deferred handles that are not yet visible)
    for (auto it = m_handles.begin(); it != m_handles.end(); ++it) {
        for (DockSplitHandle* handle : it.value()) {
            if (handle && (handle->isVisible() || m_deferredHandles.contains(handle))) {
                m_targetHandleGeometries[handle] = handle->geometry();
            }
        }
    }

    // Check if we have anything to animate
    bool needsAnimation = false;
    for (auto it = m_targetGeometries.begin(); it != m_targetGeometries.end(); ++it) {
        QWidget* cell = it.key();
        if (m_capturedGeometries.contains(cell)) {
            QRect oldGeom = m_capturedGeometries[cell];
            QRect newGeom = it.value();
            if (oldGeom != newGeom) {
                needsAnimation = true;
                break;
            }
        }
    }

    // Also check handles for animation need (including new handles)
    // Use m_capturedHandlePositions to check by adjacent cells, not by pointer
    if (!needsAnimation) {
        for (auto it = m_targetHandleGeometries.begin(); it != m_targetHandleGeometries.end();
            ++it) {
            DockSplitHandle* handle = it.key();
            QRect targetGeom = it.value();

            // Get adjacent cells to identify this handle position
            DockSplitNode* splitNode = handle->splitNode();
            int handleIdx = handle->handleIndex();

            QWidget* leftCell = nullptr;
            QWidget* rightCell = nullptr;

            if (splitNode && handleIdx >= 0 && handleIdx < splitNode->childCount() - 1) {
                DockLayoutNode* leftChild = splitNode->childAt(handleIdx);
                DockLayoutNode* rightChild = splitNode->childAt(handleIdx + 1);
                SplitDirection splitDir = splitNode->direction();

                leftCell = edgeCellWidget(leftChild, true, splitDir);
                rightCell = edgeCellWidget(rightChild, false, splitDir);
            }

            // Check if handle existed at this position (by cell pair)
            QPair<QWidget*, QWidget*> posKey(leftCell, rightCell);
            bool existedAtPosition
                = leftCell && rightCell && m_capturedHandlePositions.contains(posKey);

            if (existedAtPosition) {
                // Handle existed - check if geometry changed
                if (m_capturedHandlePositions[posKey] != targetGeom) {
                    needsAnimation = true;
                    break;
                }
            } else if (m_capturedHandleGeometries.contains(handle)) {
                // Fallback: check by pointer (for handles that weren't recreated)
                if (m_capturedHandleGeometries[handle] != targetGeom) {
                    needsAnimation = true;
                    break;
                }
            } else {
                // New handle appeared - needs animation
                needsAnimation = true;
                break;
            }
        }
    }

    if (!needsAnimation) {
        // Show any deferred handles immediately
        for (DockSplitHandle* handle : m_deferredHandles) {
            if (handle) {
                handle->show();
            }
        }
        m_deferredHandles.clear();
        m_capturedGeometries.clear();
        m_targetGeometries.clear();
        m_capturedHandleGeometries.clear();
        m_targetHandleGeometries.clear();
        m_capturedHandlePositions.clear();
        return;
    }

    // Restore old geometries before starting animation
    for (auto it = m_targetGeometries.begin(); it != m_targetGeometries.end(); ++it) {
        QWidget* cell = it.key();
        if (m_capturedGeometries.contains(cell)) {
            cell->setGeometry(m_capturedGeometries[cell]);
        }
    }

    // Restore old handle geometries OR set initial geometry for new handles
    for (auto it = m_targetHandleGeometries.begin(); it != m_targetHandleGeometries.end(); ++it) {
        DockSplitHandle* handle = it.key();
        QRect targetGeom = it.value();
        QRect startGeom = targetGeom; // Default: start at target

        // Get adjacent cells for this handle to check m_capturedHandlePositions
        DockSplitNode* splitNode = handle->splitNode();
        int handleIdx = handle->handleIndex();

        QWidget* leftCell = nullptr;
        QWidget* rightCell = nullptr;

        if (splitNode && handleIdx >= 0 && handleIdx < splitNode->childCount() - 1) {
            DockLayoutNode* leftChild = splitNode->childAt(handleIdx);
            DockLayoutNode* rightChild = splitNode->childAt(handleIdx + 1);
            SplitDirection splitDir = splitNode->direction();

            leftCell = edgeCellWidget(leftChild, true, splitDir);
            rightCell = edgeCellWidget(rightChild, false, splitDir);
        }

        // Check if this handle position existed before (by adjacent panels)
        // This works even after handles are recreated because we identify by panel pair
        QPair<QWidget*, QWidget*> positionKey(leftCell, rightCell);
        bool handleExistedAtPosition
            = leftCell && rightCell && m_capturedHandlePositions.contains(positionKey);

        if (handleExistedAtPosition) {
            // Existing handle position - restore old geometry from position map
            startGeom = m_capturedHandlePositions[positionKey];
            handle->setGeometry(startGeom);
            m_capturedHandleGeometries[handle] = startGeom;
        } else if (m_capturedHandleGeometries.contains(handle)) {
            // Handle pointer still exists in captured (unlikely after recreation, but handle it)
            handle->setGeometry(m_capturedHandleGeometries[handle]);
        } else {
            // New handle - find the correct starting position
            // The handle should start at the edge of the existing cell

            if (splitNode && handleIdx >= 0 && handleIdx < splitNode->childCount() - 1) {
                const bool leftExisted = leftCell && m_capturedGeometries.contains(leftCell);
                const bool rightExisted = rightCell && m_capturedGeometries.contains(rightCell);

                if (handle->direction() == SplitDirection::Horizontal) {
                    // Vertical handle between left and right cells
                    if (leftExisted && !rightExisted) {
                        // New cell on RIGHT - handle starts at right edge of existing cell
                        QRect oldLeftGeom = m_capturedGeometries[leftCell];
                        startGeom
                            = QRect(oldLeftGeom.right(), targetGeom.y(), 0, targetGeom.height());
                    } else if (rightExisted && !leftExisted) {
                        // New cell on LEFT - handle starts at left edge of existing cell
                        QRect oldRightGeom = m_capturedGeometries[rightCell];
                        startGeom
                            = QRect(oldRightGeom.left(), targetGeom.y(), 0, targetGeom.height());
                    } else if (!leftExisted && !rightExisted) {
                        // Both cells are new - use target position
                        startGeom = targetGeom;
                    } else {
                        // Both existed but no handle was between them before
                        // This shouldn't happen normally, but start from midpoint
                        QRect oldLeftGeom = m_capturedGeometries[leftCell];
                        QRect oldRightGeom = m_capturedGeometries[rightCell];
                        int midX = (oldLeftGeom.right() + oldRightGeom.left()) / 2;
                        startGeom = QRect(midX, targetGeom.y(), 0, targetGeom.height());
                    }
                } else {
                    // Horizontal handle between top and bottom cells
                    if (leftExisted && !rightExisted) {
                        // New cell on BOTTOM - handle starts at bottom edge of existing cell
                        QRect oldTopGeom = m_capturedGeometries[leftCell];
                        startGeom
                            = QRect(targetGeom.x(), oldTopGeom.bottom(), targetGeom.width(), 0);
                    } else if (rightExisted && !leftExisted) {
                        // New cell on TOP - handle starts at top edge of existing cell
                        QRect oldBottomGeom = m_capturedGeometries[rightCell];
                        startGeom
                            = QRect(targetGeom.x(), oldBottomGeom.top(), targetGeom.width(), 0);
                    } else if (!leftExisted && !rightExisted) {
                        // Both cells are new - use target position
                        startGeom = targetGeom;
                    } else {
                        // Both existed but no handle was between them before
                        QRect oldTopGeom = m_capturedGeometries[leftCell];
                        QRect oldBottomGeom = m_capturedGeometries[rightCell];
                        int midY = (oldTopGeom.bottom() + oldBottomGeom.top()) / 2;
                        startGeom = QRect(targetGeom.x(), midY, targetGeom.width(), 0);
                    }
                }
            }

            m_capturedHandleGeometries[handle] = startGeom;
            handle->setGeometry(startGeom);
        }

        // Show deferred handles now with their initial geometry
        if (m_deferredHandles.contains(handle)) {
            handle->show();
        }
    }

    // Clear deferred handles set
    m_deferredHandles.clear();

    // Start animation
    m_animatingLayout = true;
    m_layoutAnimation->setDuration(m_layoutAnimationDuration);
    m_layoutAnimation->setCurrentTime(0);
    m_layoutAnimation->start();
}

void DockLayoutRoot::onLayoutAnimationValueChanged(const QVariant& value)
{
    if (!m_animatingLayout)
        return;

    double progress = value.toDouble();

    // Interpolate all cell geometries
    for (auto it = m_targetGeometries.begin(); it != m_targetGeometries.end(); ++it) {
        QWidget* cell = it.key();

        // Skip if the cell was removed or is being animated separately
        if (!cell || cell == m_excludedCell) {
            continue;
        }
        if (auto* panel = qobject_cast<DockPanel*>(cell)) {
            if (panel->isAnimatingDocking()) {
                continue;
            }
        }

        if (!m_capturedGeometries.contains(cell)) {
            continue;
        }

        QRect oldGeom = m_capturedGeometries[cell];
        QRect newGeom = it.value();

        // Interpolate
        int x = oldGeom.x() + qRound((newGeom.x() - oldGeom.x()) * progress);
        int y = oldGeom.y() + qRound((newGeom.y() - oldGeom.y()) * progress);
        int w = oldGeom.width() + qRound((newGeom.width() - oldGeom.width()) * progress);
        int h = oldGeom.height() + qRound((newGeom.height() - oldGeom.height()) * progress);

        cell->setGeometry(x, y, w, h);
    }

    // Interpolate all handle geometries
    for (auto it = m_targetHandleGeometries.begin(); it != m_targetHandleGeometries.end(); ++it) {
        DockSplitHandle* handle = it.key();

        if (!handle || !m_capturedHandleGeometries.contains(handle)) {
            continue;
        }

        QRect oldGeom = m_capturedHandleGeometries[handle];
        QRect newGeom = it.value();

        // Interpolate
        int x = oldGeom.x() + qRound((newGeom.x() - oldGeom.x()) * progress);
        int y = oldGeom.y() + qRound((newGeom.y() - oldGeom.y()) * progress);
        int w = oldGeom.width() + qRound((newGeom.width() - oldGeom.width()) * progress);
        int h = oldGeom.height() + qRound((newGeom.height() - oldGeom.height()) * progress);

        handle->setGeometry(x, y, w, h);
    }
}

void DockLayoutRoot::onLayoutAnimationFinished()
{
    m_animatingLayout = false;

    // Apply final cell geometries
    for (auto it = m_targetGeometries.begin(); it != m_targetGeometries.end(); ++it) {
        QWidget* cell = it.key();

        if (!cell || cell == m_excludedCell) {
            continue;
        }
        if (auto* panel = qobject_cast<DockPanel*>(cell)) {
            if (panel->isAnimatingDocking()) {
                continue;
            }
        }

        cell->setGeometry(it.value());
    }

    // Apply final handle geometries
    for (auto it = m_targetHandleGeometries.begin(); it != m_targetHandleGeometries.end(); ++it) {
        DockSplitHandle* handle = it.key();

        if (handle) {
            handle->setGeometry(it.value());
        }
    }

    // Cleanup
    m_capturedGeometries.clear();
    m_targetGeometries.clear();
    m_capturedHandleGeometries.clear();
    m_targetHandleGeometries.clear();
    m_capturedHandlePositions.clear();
    m_deferredHandles.clear();
    m_excludedCell = nullptr;
}

} // namespace ruwa::ui::docking
