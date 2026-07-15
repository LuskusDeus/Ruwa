// SPDX-License-Identifier: MPL-2.0

// DockOverlay.cpp
#include "DockOverlay.h"
#include "DockCompassWidget.h"
#include "DropZoneIndicator.h"
#include "DockDimOverlay.h"
#include "shell/docking/core/DockContainerWidget.h"
#include "shell/docking/core/DockFloatingContainer.h"
#include "shell/docking/widgets/DockPanel.h"
#include "shell/docking/layout/DockLayoutRoot.h"

#include <QPainter>
#include <QCursor>
#include <algorithm>
#include <cmath>

namespace ruwa::ui::docking {

// ============================================================================
// Constructor / Destructor
// ============================================================================

DockOverlay::DockOverlay(DockContainerWidget* container)
    : QWidget(container)
    , m_container(container)
{
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setAttribute(Qt::WA_TranslucentBackground);
    setMouseTracking(true);

    // Z-order (bottom to top):
    // 1. Fading dim/indicator (previous state)        — child of overlay (under floating)
    // 2. Current dim/indicator (new state)            — child of overlay (under floating)
    // 3. Floating container                            — sibling of overlay in container
    // 4. Compass widgets                               — sibling of overlay, raised above floating
    //
    // The dim/indicator layers must sit under the dragged panel (otherwise
    // they'd cover it), but the compass arrows must sit on top of the dragged
    // panel so the user can target them. To achieve this, compasses are
    // parented to the DockContainerWidget directly (siblings of the floating
    // container) and explicitly raised in raiseFloatingContainer().

    // Fading layers (for smooth transitions)
    m_dimOverlayFading = new DockDimOverlay(this);
    m_dropIndicatorFading = new DropZoneIndicator(this);
    m_compassFading = new DockCompassWidget(m_container);

    // Current layers
    m_dimOverlay = new DockDimOverlay(this);
    m_dropIndicator = new DropZoneIndicator(this);

    m_compass = new DockCompassWidget(m_container);

    // Timer for updating geometry during target animations
    m_geometryUpdateTimer = new QTimer(this);
    m_geometryUpdateTimer->setInterval(16); // ~60fps
    connect(m_geometryUpdateTimer, &QTimer::timeout, this, &DockOverlay::updateIndicatorGeometry);
}

DockOverlay::~DockOverlay()
{
    // Children deleted by Qt
}

// ============================================================================
// Drag State
// ============================================================================

void DockOverlay::showForContainer()
{
    // Container mode - no specific target area, use whole container
    m_containerMode = true;
    m_targetPanel = nullptr;
    m_currentZone = DropZone::None;

    // Reset previous state tracking
    m_prevZone = DropZone::None;
    m_prevDropRect = QRect();

    show();

    // Start geometry update timer
    armGeometryTimer();

    // Compass will be positioned when updateDropZone is called and we find a target panel
    if (m_compass) {
        m_compass->hideImmediate();
    }

    // Keep floating container on top
    raiseFloatingContainer();

    update();
}

void DockOverlay::updateDropZone(const QPoint& globalPos)
{
    // Prevent re-entrant updates
    if (m_inUpdate) {
        return;
    }
    m_inUpdate = true;

    // Cleanup guard
    struct UpdateGuard {
        bool& flag;
        ~UpdateGuard() { flag = false; }
    } guard { m_inUpdate };

    if (!isVisible()) {
        return;
    }

    QPoint localPos = mapFromGlobal(globalPos);
    DropZone newZone = DropZone::None;

    // Update target panel under cursor
    updateTargetPanel(globalPos);

    // Cursor speed throttle: if the panel is being dragged faster than the
    // configured threshold, suppress drop-zone indicators entirely.
    // Mirrors the auto-snap speed gate in TransformController.
    bool cursorTooFast = false;
    if (m_cursorSpeedSampleValid) {
        const qint64 elapsedMs = m_cursorSpeedTimer.elapsed();
        if (elapsedMs > 0) {
            const QPoint delta = globalPos - m_lastCursorPos;
            const float dist
                = std::sqrt(static_cast<float>(delta.x() * delta.x() + delta.y() * delta.y()));
            const float speed = dist * 1000.0f / static_cast<float>(elapsedMs);
            if (speed > kMaxCursorSpeedPxPerSec) {
                cursorTooFast = true;
            }
        }
    }
    m_lastCursorPos = globalPos;
    m_cursorSpeedTimer.restart();
    m_cursorSpeedSampleValid = true;

    if (cursorTooFast) {
        if (m_currentZone != DropZone::None) {
            m_currentZone = DropZone::None;
            m_compass->setHighlightedZone(DropZone::None);
            updateDropIndicator();
            emit dropZoneChanged(DropZone::None);
        }
        return;
    }

    // First check container edges for outer zones (higher priority)
    newZone = zoneAtContainerEdge(localPos);

    if (newZone == DropZone::None) {
        // Check compass buttons
        QPoint compassLocal = m_compass->mapFromGlobal(globalPos);
        if (m_compass->isVisible() && m_compass->containsPoint(compassLocal)) {
            newZone = m_compass->zoneAt(compassLocal);
        }

        // If not on compass, check regions for implicit zones
        if (newZone == DropZone::None) {
            // Check if we're over a panel for inner zones
            if (!m_targetPanel.isNull()) {
                newZone = zoneAtPanelPosition(globalPos);
            } else {
                // No panel under cursor - use container edges
                newZone = zoneAtContainerPosition(localPos);
            }
        }
    }

    if (newZone != m_currentZone) {
        m_currentZone = newZone;
        m_compass->setHighlightedZone(newZone);
        updateDropIndicator();
        emit dropZoneChanged(newZone);
        // Zone change → likely layout reflow / target animation; re-arm timer.
        armGeometryTimer();
    } else if (m_currentZone != DropZone::None) {
        // Zone unchanged: follow the cursor with a single explicit update. Do NOT
        // re-arm the periodic geometry poll here — re-arming on every mouse move
        // reset the stable-tick counter so the 16ms timer never auto-stopped,
        // and each tick re-raised the floating container over the GL canvas
        // (~60Hz recomposite). The poll is only needed to track a layout-reflow
        // animation of the target, which is (re)armed on zone changes and on
        // showForContainer.
        updateIndicatorGeometry();
    }
}

void DockOverlay::setFloatingContainer(DockFloatingContainer* container)
{
    m_floatingContainer = container;
}

void DockOverlay::hideOverlay()
{
    // Stop geometry update timer
    if (m_geometryUpdateTimer) {
        m_geometryUpdateTimer->stop();
    }
    m_lastTargetGeometry = QRect();
    m_geometryStableTicks = 0;

    m_targetPanel = nullptr;
    m_currentZone = DropZone::None;
    m_floatingContainer = nullptr;
    m_containerMode = false;

    // Reset previous state
    m_prevZone = DropZone::None;
    m_prevDropRect = QRect();

    // Reset cooldown
    m_indicatorCooldownActive = false;

    // Reset cursor speed throttle
    m_cursorSpeedSampleValid = false;

    if (m_compass) {
        m_compass->hideImmediate();
    }
    if (m_compassFading) {
        m_compassFading->hideImmediate();
    }

    // Hide all indicators immediately
    if (m_dropIndicator) {
        m_dropIndicator->hideImmediate();
    }
    if (m_dropIndicatorFading) {
        m_dropIndicatorFading->hideImmediate();
    }

    if (m_dimOverlay) {
        m_dimOverlay->hideImmediate();
    }
    if (m_dimOverlayFading) {
        m_dimOverlayFading->hideImmediate();
    }

    hide();
}

// ============================================================================
// Appearance
// ============================================================================

void DockOverlay::applyTheme(const ruwa::ui::core::ThemeColors& colors)
{
    m_colors = colors;

    if (m_compass) {
        m_compass->applyTheme(colors);
    }
    if (m_compassFading) {
        m_compassFading->applyTheme(colors);
    }

    if (m_dropIndicator) {
        m_dropIndicator->applyTheme(colors);
    }
    if (m_dropIndicatorFading) {
        m_dropIndicatorFading->applyTheme(colors);
    }

    if (m_dimOverlay) {
        m_dimOverlay->applyTheme(colors);
    }
    if (m_dimOverlayFading) {
        m_dimOverlayFading->applyTheme(colors);
    }
}

// ============================================================================
// Events
// ============================================================================

void DockOverlay::paintEvent(QPaintEvent* /*event*/)
{
    // Transparent - we only show children widgets
}

// ============================================================================
// Private
// ============================================================================

void DockOverlay::updateDropIndicator()
{
    if (!m_dropIndicator)
        return;

    // Zone became None - fade out and start cooldown
    if (m_currentZone == DropZone::None) {
        m_dropIndicator->hideIndicator();

        if (m_dimOverlay) {
            m_dimOverlay->hideAnimated();
        }

        // Start cooldown only if we had a valid zone before
        if (m_prevZone != DropZone::None) {
            m_indicatorCooldownTimer.start();
            m_indicatorCooldownActive = true;
        }

        m_prevZone = DropZone::None;
        m_prevDropRect = QRect();

        raiseFloatingContainer();
        return;
    }

    // Check cooldown - don't show new indicator until cooldown expires
    if (m_indicatorCooldownActive) {
        if (m_indicatorCooldownTimer.elapsed() < m_indicatorCooldownMs) {
            // Still in cooldown - but update state so we don't re-trigger targetChanged
            m_prevZone = m_currentZone;
            return;
        }
        // Cooldown expired
        m_indicatorCooldownActive = false;
    }

    QRect dropRect = calculateDropRect(m_currentZone);
    if (dropRect.isEmpty()) {
        m_dropIndicator->hideIndicator();
        if (m_dimOverlay) {
            m_dimOverlay->hideAnimated();
        }
        raiseFloatingContainer();
        return;
    }

    // Check if target changed significantly (far position)
    bool targetChanged = false;
    if (m_prevDropRect.isValid()) {
        int dx = qAbs(dropRect.center().x() - m_prevDropRect.center().x());
        int dy = qAbs(dropRect.center().y() - m_prevDropRect.center().y());
        if (dx > 100 || dy > 100) {
            targetChanged = true;
        }
    }

    if (targetChanged && m_prevZone != DropZone::None) {
        // Target changed - swap indicators for smooth crossfade
        swapIndicators();

        // Fade out old indicator (now in fading slot)
        m_dropIndicatorFading->hideIndicator();
        if (m_dimOverlayFading) {
            m_dimOverlayFading->hideAnimated();
        }
    }

    // Show new indicator
    m_dropIndicator->showForZone(dropRect, m_currentZone);

    // Show dim overlay for target (inner zones only)
    if (m_dimOverlay && isInnerZone(m_currentZone)) {
        if (!m_targetPanel.isNull()) {
            // Dim the target panel
            m_dimOverlay->showForTarget(m_targetPanel.data());
        } else {
            m_dimOverlay->hideAnimated();
        }
    } else if (m_dimOverlay) {
        m_dimOverlay->hideAnimated();
    }

    // Save state for next comparison
    m_prevZone = m_currentZone;
    m_prevDropRect = dropRect;

    raiseFloatingContainer();
}

void DockOverlay::updateIndicatorGeometry()
{
    // Auto-stop the periodic geometry poll once the target widget's geometry
    // has been stable for kGeometryStableFrames ticks. This keeps the timer
    // alive only while a layout animation is actively reflowing the target.
    if (m_geometryUpdateTimer && m_geometryUpdateTimer->isActive()) {
        QRect probe;
        if (!m_targetPanel.isNull()) {
            probe = m_targetPanel->geometry();
        }
        if (probe == m_lastTargetGeometry) {
            if (++m_geometryStableTicks >= kGeometryStableFrames) {
                m_geometryUpdateTimer->stop();
            }
        } else {
            m_lastTargetGeometry = probe;
            m_geometryStableTicks = 0;
        }
    }

    // Always update compass position (target might be animating)
    updateCompassForTargetPanel();

    // Update indicators only if there's an active zone
    if (m_currentZone == DropZone::None) {
        return;
    }

    // Check cooldown
    if (m_indicatorCooldownActive) {
        if (m_indicatorCooldownTimer.elapsed() < m_indicatorCooldownMs) {
            // Still in cooldown - don't show indicator yet
            return;
        }
        // Cooldown expired
        m_indicatorCooldownActive = false;
    }

    QRect dropRect = calculateDropRect(m_currentZone);
    if (!dropRect.isEmpty() && m_dropIndicator) {
        m_dropIndicator->showForZone(dropRect, m_currentZone);
    }

    // Update dim overlay geometry
    if (m_dimOverlay && isInnerZone(m_currentZone) && !m_targetPanel.isNull()) {
        m_dimOverlay->showForTarget(m_targetPanel.data());
    }

    // Per-frame poll: z-order is already correct (see raiseFloatingContainer).
    raiseFloatingContainer(/*force=*/false);
}

QRect DockOverlay::calculateDropRect(DropZone zone) const
{
    QRect baseRect;

    if (isOuterZone(zone)) {
        baseRect = rect();
    } else if (!m_targetPanel.isNull()) {
        // Use target panel rect for inner zones if available
        QRect panelRect = m_targetPanel->rect();
        QPoint topLeft = m_targetPanel->mapToGlobal(panelRect.topLeft());
        baseRect = QRect(mapFromGlobal(topLeft), panelRect.size());
    } else {
        // No target panel - use full container rect
        baseRect = rect();
    }

    // Inset baseRect so the indicator doesn't hug the container/target edges.
    // This gives a small visual gap around the drop preview.
    constexpr int kEdgePadding = 8;
    if (baseRect.width() > kEdgePadding * 4 && baseRect.height() > kEdgePadding * 4) {
        baseRect.adjust(kEdgePadding, kEdgePadding, -kEdgePadding, -kEdgePadding);
    }

    int zoneWidth = 0;
    int zoneHeight = 0;

    if (isInnerZone(zone)) {
        // Inner zones: use preferred size from the panel being dragged
        DockPanel* draggedPanel = nullptr;
        if (!m_floatingContainer.isNull()) {
            draggedPanel = m_floatingContainer->panel();
        }

        if (draggedPanel) {
            // Use panel's preferred docked size based on zone direction
            if (zone == DropZone::InnerLeft || zone == DropZone::InnerRight) {
                zoneWidth = draggedPanel->effectiveHorizontalDockedWidth();
            } else {
                zoneHeight = draggedPanel->effectiveVerticalDockedHeight();
            }
        }

        // Fallback to 50% if no preferred size or no dragged panel
        if (zoneWidth <= 0) {
            zoneWidth = qRound(baseRect.width() * 0.5);
        }
        if (zoneHeight <= 0) {
            zoneHeight = qRound(baseRect.height() * 0.5);
        }

        // Clamp to base rect size (max 80% to leave space for the target)
        zoneWidth = qMin(zoneWidth, qRound(baseRect.width() * 0.8));
        zoneHeight = qMin(zoneHeight, qRound(baseRect.height() * 0.8));
    } else {
        // Outer zones: 20% (preview only)
        zoneWidth = qRound(baseRect.width() * 0.2);
        zoneHeight = qRound(baseRect.height() * 0.2);
    }

    // Ensure minimum size
    zoneWidth = qMax(zoneWidth, 40);
    zoneHeight = qMax(zoneHeight, 40);

    switch (zone) {
    case DropZone::OuterLeft:
    case DropZone::InnerLeft:
        return QRect(baseRect.left(), baseRect.top(), zoneWidth, baseRect.height());

    case DropZone::OuterRight:
    case DropZone::InnerRight:
        return QRect(baseRect.right() - zoneWidth, baseRect.top(), zoneWidth, baseRect.height());

    case DropZone::OuterTop:
    case DropZone::InnerTop:
        return QRect(baseRect.left(), baseRect.top(), baseRect.width(), zoneHeight);

    case DropZone::OuterBottom:
    case DropZone::InnerBottom:
        return QRect(baseRect.left(), baseRect.bottom() - zoneHeight, baseRect.width(), zoneHeight);

    default:
        return QRect();
    }
}

DropZone DockOverlay::zoneAtContainerEdge(const QPoint& localPos) const
{
    constexpr int edgeMargin = 4; // Narrow - only when pressed against container border
    QRect r = rect();

    if (localPos.x() < r.left() + edgeMargin) {
        return DropZone::OuterLeft;
    }
    if (localPos.x() > r.right() - edgeMargin) {
        return DropZone::OuterRight;
    }
    if (localPos.y() < r.top() + edgeMargin) {
        return DropZone::OuterTop;
    }
    if (localPos.y() > r.bottom() - edgeMargin) {
        return DropZone::OuterBottom;
    }

    return DropZone::None;
}

DropZone DockOverlay::zoneAtContainerPosition(const QPoint& localPos) const
{
    // For container mode - detect zones within container
    QRect containerRect = rect();

    if (!containerRect.contains(localPos)) {
        return DropZone::None;
    }

    int w = containerRect.width();
    int h = containerRect.height();
    int x = localPos.x();
    int y = localPos.y();

    // Avoid division by zero
    if (w <= 0 || h <= 0) {
        return DropZone::None;
    }

    // Use larger margin for container mode since we want inner zones to work
    constexpr int edgeMargin = 60;

    // Check edges with priority to corners
    bool nearLeft = x < edgeMargin;
    bool nearRight = x > w - edgeMargin;
    bool nearTop = y < edgeMargin;
    bool nearBottom = y > h - edgeMargin;

    // If in corner, determine which edge is closer
    if (nearLeft && nearTop) {
        return (x < y) ? DropZone::InnerLeft : DropZone::InnerTop;
    }
    if (nearRight && nearTop) {
        return (w - x < y) ? DropZone::InnerRight : DropZone::InnerTop;
    }
    if (nearLeft && nearBottom) {
        return (x < h - y) ? DropZone::InnerLeft : DropZone::InnerBottom;
    }
    if (nearRight && nearBottom) {
        return (w - x < h - y) ? DropZone::InnerRight : DropZone::InnerBottom;
    }

    // Single edge
    if (nearLeft)
        return DropZone::InnerLeft;
    if (nearRight)
        return DropZone::InnerRight;
    if (nearTop)
        return DropZone::InnerTop;
    if (nearBottom)
        return DropZone::InnerBottom;

    // Not near any edge - no zone (allows floating)
    return DropZone::None;
}

DropZone DockOverlay::zoneAtPosition(const QPoint& localPos) const
{
    return zoneAtContainerEdge(localPos);
}

void DockOverlay::swapIndicators()
{
    // Swap drop indicators
    std::swap(m_dropIndicator, m_dropIndicatorFading);

    // Swap dim overlays
    std::swap(m_dimOverlay, m_dimOverlayFading);
}

void DockOverlay::swapCompasses()
{
    std::swap(m_compass, m_compassFading);
}

void DockOverlay::armGeometryTimer()
{
    if (!m_geometryUpdateTimer) {
        return;
    }
    m_geometryStableTicks = 0;
    m_lastTargetGeometry = QRect();
    if (!m_geometryUpdateTimer->isActive()) {
        m_geometryUpdateTimer->start();
    }
}

void DockOverlay::raiseFloatingContainer(bool force)
{
    // The per-frame poll passes force=false. Nothing re-stacks during steady
    // drag movement (moveTo() does not raise, the compass self-raises in
    // updateCompassForTargetPanel, and the dim/indicator
    // are children of this overlay so they stay under the floating container),
    // so skip the churn. Each raise()/stackUnder() over the GL canvas forces a
    // backing-store recomposite, which was dropping drag to ~15fps.
    if (!force) {
        return;
    }

    if (!m_floatingContainer.isNull()) {
        m_floatingContainer->raise();
        // Ensure overlay (dim + drop-zone indicator) stays under the floating
        // container so the dragged panel paints on top of them.
        if (parentWidget() == m_floatingContainer->parentWidget()) {
            stackUnder(m_floatingContainer.data());
        }
    }

    // Compass arrows must sit ABOVE the floating container so the user can
    // target them while dragging. Raise the fading one first so the current
    // compass ends up on top.
    if (m_compassFading) {
        m_compassFading->raise();
    }
    if (m_compass) {
        m_compass->raise();
    }
}

void DockOverlay::updateTargetPanel(const QPoint& globalPos)
{
    if (!m_containerMode || !m_container) {
        m_targetPanel = nullptr;
        return;
    }

    DockLayoutRoot* layoutRoot = m_container->layoutRoot();
    if (!layoutRoot) {
        m_targetPanel = nullptr;
        return;
    }

    DockPanel* newTarget = layoutRoot->findPanelAt(globalPos);

    // Check if target panel changed
    if (newTarget != m_targetPanel) {
        // If we had a target and compass was showing, swap for crossfade
        if (!m_targetPanel.isNull() && m_compass->isActiveOrShowing()) {
            swapCompasses();
            m_compassFading->hideAnimated();
        }

        m_targetPanel = newTarget;

        // Update compass position for new target
        updateCompassForTargetPanel();
    }
}

void DockOverlay::updateCompassForTargetPanel()
{
    if (!m_compass) {
        return;
    }

    if (m_targetPanel.isNull() || m_container.isNull()) {
        // No target panel - hide compass
        m_compass->hideAnimated();
        return;
    }

    // Center compass on target panel. Compass is parented to m_container, so
    // map the target center into container coordinates.
    QRect panelRect = m_targetPanel->rect();
    QPoint panelCenter = m_targetPanel->mapToGlobal(panelRect.center());
    QPoint localCenter = m_container->mapFromGlobal(panelCenter);

    int compassW = m_compass->width();
    int compassH = m_compass->height();

    m_compass->move(localCenter.x() - compassW / 2, localCenter.y() - compassH / 2);
    m_compass->showAnimated();
    m_compass->raise();
}

DropZone DockOverlay::zoneAtPanelPosition(const QPoint& globalPos) const
{
    if (m_targetPanel.isNull()) {
        return DropZone::None;
    }

    QRect panelRect = m_targetPanel->rect();
    QPoint panelTopLeft = m_targetPanel->mapToGlobal(panelRect.topLeft());
    QPoint localPos = globalPos - panelTopLeft;

    if (!panelRect.contains(localPos)) {
        return DropZone::None;
    }

    int w = panelRect.width();
    int h = panelRect.height();
    int x = localPos.x();
    int y = localPos.y();

    // Avoid division by zero
    if (w <= 0 || h <= 0) {
        return DropZone::None;
    }

    // Use fixed pixel margin for edge detection
    constexpr int edgeMargin = 50;

    // Check edges with priority to corners
    bool nearLeft = x < edgeMargin;
    bool nearRight = x > w - edgeMargin;
    bool nearTop = y < edgeMargin;
    bool nearBottom = y > h - edgeMargin;

    // If in corner, determine which edge is closer
    if (nearLeft && nearTop) {
        return (x < y) ? DropZone::InnerLeft : DropZone::InnerTop;
    }
    if (nearRight && nearTop) {
        return (w - x < y) ? DropZone::InnerRight : DropZone::InnerTop;
    }
    if (nearLeft && nearBottom) {
        return (x < h - y) ? DropZone::InnerLeft : DropZone::InnerBottom;
    }
    if (nearRight && nearBottom) {
        return (w - x < h - y) ? DropZone::InnerRight : DropZone::InnerBottom;
    }

    // Single edge
    if (nearLeft)
        return DropZone::InnerLeft;
    if (nearRight)
        return DropZone::InnerRight;
    if (nearTop)
        return DropZone::InnerTop;
    if (nearBottom)
        return DropZone::InnerBottom;

    // Not near any edge - no zone (allows floating)
    return DropZone::None;
}

} // namespace ruwa::ui::docking
