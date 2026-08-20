// SPDX-License-Identifier: MPL-2.0

// DockGroupHost.cpp
#include "DockGroupHost.h"
#include "DockGroupHeader.h"
#include "DockPanel.h"
#include "shell/docking/layout/DockLeafNode.h"
#include "shared/style/AnimationPolicy.h"

#include <QEasingCurve>
#include <QKeySequence>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QShortcut>
#include <QTimer>
#include <QVariantAnimation>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::docking {

namespace {

/// Switching the visible member is the same gesture as switching a document
/// tab, so it borrows that transition wholesale from AnimatedTabWidget: the
/// docking system's own layout timing (250 ms, OutCubic) reads as a flick next
/// to it. The layout duration still decides WHETHER a slide runs at all.
constexpr int kMemberSlideMs = 350;
constexpr QEasingCurve::Type kMemberSlideEasing = QEasingCurve::InOutCubic;

/**
 * @brief The member area's outline: square at the top, rounded at the bottom
 *
 * Square where it meets the header strip (the two are one surface), rounded
 * where the cell ends — the members square off their TOP corners while grouped
 * and keep their bottom ones, so the backdrop has to follow the same shape or
 * it would show through as blunt corners under a panel.
 */
QPainterPath buildMemberBackdrop(const QRectF& rect, qreal radius)
{
    const qreal r = qBound<qreal>(0.0, radius, qMin(rect.width() / 2.0, rect.height()));

    QPainterPath path;
    path.moveTo(rect.left(), rect.top());
    path.lineTo(rect.right(), rect.top());
    path.lineTo(rect.right(), rect.bottom() - r);
    if (r > 0.0) {
        path.quadTo(rect.right(), rect.bottom(), rect.right() - r, rect.bottom());
    } else {
        path.lineTo(rect.right(), rect.bottom());
    }
    path.lineTo(rect.left() + r, rect.bottom());
    if (r > 0.0) {
        path.quadTo(rect.left(), rect.bottom(), rect.left(), rect.bottom() - r);
    } else {
        path.lineTo(rect.left(), rect.bottom());
    }
    path.closeSubpath();
    return path;
}

} // namespace

DockGroupHost::DockGroupHost(DockLeafNode* leaf, QWidget* parent)
    : QWidget(parent)
    , m_leaf(leaf)
{
    setAttribute(Qt::WA_NoSystemBackground, true);

    m_header = new DockGroupHeader(this);
    m_viewport = new QWidget(this);
    // The viewport is purely a clip rect for the sliding members.
    //
    // NOT WA_TransparentForMouseEvents: that attribute takes the widget AND ITS
    // WHOLE SUBTREE out of hit testing, so every grouped panel would be dead to
    // the mouse. The viewport has nothing to swallow anyway — the current
    // member covers it exactly.
    m_viewport->setAttribute(Qt::WA_NoSystemBackground, true);

    connect(m_header, &DockGroupHeader::panelActivationRequested, this,
        &DockGroupHost::panelActivationRequested);
    connect(
        m_header, &DockGroupHeader::panelCloseStarted, this, &DockGroupHost::onPanelCloseStarted);
    connect(m_header, &DockGroupHeader::panelFarewellFinished, this,
        &DockGroupHost::onPanelFarewellFinished);
    connect(m_header, &DockGroupHeader::panelReorderRequested, this,
        &DockGroupHost::panelReorderRequested);

    setupShortcuts();
    layoutChildren();
}

void DockGroupHost::setupShortcuts()
{
    // WidgetWithChildren, so the cycle only fires while focus is inside this
    // group — several groups can be open at once, and each answers for itself.
    //
    // Ctrl+PageDown/Up rather than Ctrl+Tab: that one already cycles document
    // tabs (nav.nextTab), and a panel group must not steal it.
    auto* next = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageDown), this);
    next->setContext(Qt::WidgetWithChildrenShortcut);
    connect(next, &QShortcut::activated, this, [this]() { activateRelativeMember(1, true); });

    auto* previous = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_PageUp), this);
    previous->setContext(Qt::WidgetWithChildrenShortcut);
    connect(previous, &QShortcut::activated, this, [this]() { activateRelativeMember(-1, true); });
}

DockGroupHost::~DockGroupHost()
{
    stopSlide();

    // Members are owned by the docking system, not by this frame: hand every
    // one of them back to the container so destroying a host never destroys a
    // panel. DockLayoutRoot normally releases them first; this is the net.
    //
    // Only panels this frame STILL holds. A member listed here may long since
    // have been adopted by another host — that is what a whole-tree rebuild
    // (restoring a layout over a live one) does: the new frames take the panels
    // first, and only then is this one swept as stale. Reparenting on the
    // membership list alone would rip those panels out of the frame that now
    // owns them and leave the group looking empty until a tab is clicked.
    QWidget* container = parentWidget();
    for (const auto& p : m_hosted) {
        if (!p.isNull() && p->parentWidget() == m_viewport) {
            p->setParent(container);
        }
    }
    m_hosted.clear();
}

// ============================================================================
// Model
// ============================================================================

QList<DockPanel*> DockGroupHost::hostedPanels() const
{
    QList<DockPanel*> result;
    result.reserve(m_hosted.size());
    for (const auto& p : m_hosted) {
        if (!p.isNull()) {
            result.append(p.data());
        }
    }
    return result;
}

void DockGroupHost::syncFromLeaf()
{
    if (!m_leaf) {
        return;
    }

    // A cell that is a group again (a panel was dropped into it while the last
    // collapse was still playing) is not on its way out any more — otherwise
    // the flag would keep the frame alive forever the next time it drops to one.
    if (m_collapsing && m_leaf->isGroup()) {
        m_collapsing = false;
    }

    const QList<DockPanel*> members = m_leaf->panels();

    // Captured before anything moves: releasePanel() clears m_currentPanel when
    // the member it takes away was the visible one, and the old tab order is
    // what says which side the replacement should come in from.
    DockPanel* previousCurrent = m_currentPanel.data();
    const QList<DockPanel*> previousOrder = hostedPanels();

    // Release panels that are no longer members.
    for (int i = m_hosted.size() - 1; i >= 0; --i) {
        DockPanel* hosted = m_hosted[i].data();
        if (!hosted || !members.contains(hosted)) {
            m_hosted.removeAt(i);
            releasePanel(hosted);
        }
    }

    // Adopt new members, in tab order.
    m_hosted.clear();
    for (DockPanel* member : members) {
        if (!member) {
            continue;
        }
        adoptPanel(member);
    }

    m_header->setPanels(members);

    DockPanel* current = m_leaf->panel();

    // The members square off their top corners while grouped; the strip carries
    // that rounding for the cell, so it has to use the same radius they gave up.
    if (current) {
        m_header->setCornerRadius(current->baseCornerRadius());
    }

    stopSlide();

    // The visible member just left the group (closed, dragged out, ungrouped)
    // and another one takes the stage. Slide it in instead of blinking it into
    // place — there is no outgoing panel to slide out, the departed one is
    // already back with the container.
    //
    // Only while the group SURVIVES: dropping to a single member destroys this
    // frame, and destroyGroupHost() calls us one last time to promote the
    // survivor before its own farewell — a slide there would be cut short by
    // the release that follows.
    const bool lostCurrent = previousCurrent && current && current != previousCurrent
        && !members.contains(previousCurrent) && m_leaf->isGroup();

    if (lostCurrent) {
        const int gone = static_cast<int>(previousOrder.indexOf(previousCurrent));
        const int taking = static_cast<int>(previousOrder.indexOf(current));
        const SlideDirection direction
            = (taking >= gone) ? SlideDirection::FromRight : SlideDirection::FromLeft;

        m_currentPanel = nullptr;
        setCurrentPanel(current, /*animated=*/true, direction);
        return;
    }

    m_currentPanel = current;
    m_header->setCurrentPanel(current);
    applyRestingGeometry();
}

void DockGroupHost::adoptPanel(DockPanel* panel)
{
    if (!panel) {
        return;
    }

    if (!m_hosted.contains(QPointer<DockPanel>(panel))) {
        m_hosted.append(panel);
    }

    if (panel->parentWidget() != m_viewport) {
        panel->setParent(m_viewport);
    }
    panel->setGeometry(m_viewport->rect());
    panel->setVisible(panel == m_currentPanel.data());
}

void DockGroupHost::activateRelativeMember(int delta, bool wrap)
{
    const QList<DockPanel*> order = hostedPanels();
    if (order.size() < 2 || delta == 0) {
        return;
    }

    const int current = static_cast<int>(order.indexOf(m_currentPanel.data()));
    const int count = static_cast<int>(order.size());
    int target = (current < 0 ? 0 : current) + delta;

    if (wrap) {
        target = ((target % count) + count) % count;
    } else {
        target = qBound(0, target, count - 1);
    }

    if (target == current) {
        return;
    }

    emit panelActivationRequested(order[target]);
}

void DockGroupHost::releasePanel(DockPanel* panel)
{
    if (!panel) {
        return;
    }

    m_hosted.removeAll(QPointer<DockPanel>(panel));

    QWidget* container = parentWidget();
    if (!container || panel->parentWidget() != m_viewport) {
        return;
    }

    // Keep the panel where it visually is, so a caller can animate from here.
    const QPoint topLeftInContainer = panel->mapTo(container, QPoint(0, 0));
    const QSize size = panel->size();
    // setParent() hides a widget; restore whatever state it had. Forcing show()
    // here would resurrect a member that was just CLOSED, and would reveal the
    // hidden non-current members of a group that is being taken apart.
    const bool wasVisible = panel->isVisible();
    panel->setParent(container);
    panel->setGeometry(QRect(topLeftInContainer, size));
    panel->setVisible(wasVisible);

    if (m_currentPanel.data() == panel) {
        m_currentPanel = nullptr;
    }
}

// ============================================================================
// Selection / sliding
// ============================================================================

void DockGroupHost::setCurrentPanel(DockPanel* panel, bool animated, SlideDirection direction)
{
    if (!panel || panel == m_currentPanel.data()) {
        return;
    }

    adoptPanel(panel);

    DockPanel* outgoing = m_currentPanel.data();
    m_currentPanel = panel;
    m_header->setCurrentPanel(panel);

    // No outgoing member is fine: when the visible one was closed or dragged
    // out, the replacement slides in over an empty viewport.
    const bool canAnimate = animated && m_animationsEnabled && anim::enabled()
        && m_animationDuration > 0 && isVisible() && m_viewport->width() > 0;

    if (!canAnimate) {
        stopSlide();
        applyRestingGeometry();
        return;
    }

    if (direction == SlideDirection::Auto) {
        const QList<DockPanel*> order = hostedPanels();
        const int from = static_cast<int>(order.indexOf(outgoing));
        const int to = static_cast<int>(order.indexOf(panel));
        direction = (to >= from) ? SlideDirection::FromRight : SlideDirection::FromLeft;
    }

    stopSlide();

    const int w = m_viewport->width();
    m_slideOutgoing = outgoing;
    m_slideIncoming = panel;
    m_slideIncomingStartX = (direction == SlideDirection::FromRight) ? w : -w;
    m_slideOutgoingTargetX = -m_slideIncomingStartX;

    const QRect rest = m_viewport->rect();
    if (outgoing) {
        outgoing->setGeometry(rest);
        outgoing->show();
    }
    panel->setGeometry(rest.translated(m_slideIncomingStartX, 0));
    panel->show();
    panel->raise();

    m_slideAnimation = new QVariantAnimation(this);
    m_slideAnimation->setStartValue(0.0);
    m_slideAnimation->setEndValue(1.0);
    m_slideAnimation->setDuration(anim::duration(kMemberSlideMs));
    m_slideAnimation->setEasingCurve(kMemberSlideEasing);
    connect(m_slideAnimation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) { onSlideValue(value.toReal()); });
    connect(m_slideAnimation, &QVariantAnimation::finished, this, &DockGroupHost::onSlideFinished);
    m_slideAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

void DockGroupHost::runInsertionSlide(GroupInsertSide side, int duration)
{
    DockPanel* outgoing = m_currentPanel.data();
    if (!outgoing || !m_animationsEnabled || !anim::enabled() || duration <= 0
        || m_viewport->width() <= 0) {
        return;
    }

    stopSlide();

    const int w = m_viewport->width();
    // Inserting AFTER the incumbent pushes it left (the newcomer arrives from
    // the right); inserting BEFORE it pushes it right.
    m_slideOutgoing = outgoing;
    m_slideIncoming = nullptr;
    m_slideOutgoingTargetX = (side == GroupInsertSide::After) ? -w : w;
    m_slideIncomingStartX = 0;

    outgoing->setGeometry(m_viewport->rect());
    outgoing->show();

    m_slideAnimation = new QVariantAnimation(this);
    m_slideAnimation->setStartValue(0.0);
    m_slideAnimation->setEndValue(1.0);
    m_slideAnimation->setDuration(anim::duration(duration));
    m_slideAnimation->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_slideAnimation, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) { onSlideValue(value.toReal()); });
    connect(m_slideAnimation, &QVariantAnimation::finished, this, &DockGroupHost::onSlideFinished);
    m_slideAnimation->start(QAbstractAnimation::DeleteWhenStopped);
}

void DockGroupHost::previewCurrentTab(DockPanel* panel)
{
    if (m_header) {
        m_header->setCurrentPanel(panel);
    }
}

void DockGroupHost::finishInsertion(DockPanel* panel)
{
    if (!panel) {
        return;
    }

    stopSlide();
    adoptPanel(panel);
    m_currentPanel = panel;
    m_header->setCurrentPanel(panel);
    m_header->setDropInsertIndex(-1);
    applyRestingGeometry();
}

void DockGroupHost::onSlideValue(qreal progress)
{
    const QRect rest = m_viewport->rect();

    if (DockPanel* outgoing = m_slideOutgoing.data()) {
        const int x = qRound(m_slideOutgoingTargetX * progress);
        outgoing->setGeometry(rest.translated(x, 0));
    }

    if (DockPanel* incoming = m_slideIncoming.data()) {
        const int x = qRound(m_slideIncomingStartX * (1.0 - progress));
        incoming->setGeometry(rest.translated(x, 0));
    }
}

void DockGroupHost::onSlideFinished()
{
    m_slideAnimation = nullptr;
    applyRestingGeometry();
    markPendingCloseSlidesDone();
    finishCollapse();
}

void DockGroupHost::stopSlide()
{
    if (!m_slideAnimation.isNull()) {
        // Started with DeleteWhenStopped: stopping is enough, and the finished
        // handler must not run for a slide that was superseded.
        m_slideAnimation->disconnect(this);
        m_slideAnimation->stop();
        m_slideAnimation = nullptr;

        // The suppressed finished handler is also what a close waiting on this
        // slide was listening for; without this the panel would never close.
        markPendingCloseSlidesDone();
    }
    m_slideOutgoing = nullptr;
    m_slideIncoming = nullptr;
}

void DockGroupHost::applyRestingGeometry()
{
    const QRect rest = m_viewport->rect();
    DockPanel* current = m_currentPanel.data();

    for (const auto& p : m_hosted) {
        DockPanel* member = p.data();
        if (!member) {
            continue;
        }
        member->setGeometry(rest);
        member->setVisible(member == current);
    }

    if (current) {
        current->raise();
    }
}

// ============================================================================
// Closing a member
// ============================================================================

void DockGroupHost::onPanelCloseStarted(DockPanel* closing, DockPanel* successor)
{
    if (!closing) {
        return;
    }

    PendingClose pending;
    pending.panel = closing;

    if (successor && successor != closing) {
        // Routed through the layout root rather than straight into
        // setCurrentPanel(): the leaf's own selection has to move with the
        // stage, or the close landing later would find a stale current member
        // and play the whole transition a second time.
        emit panelActivationRequested(successor);
        // Only a slide that actually started is worth waiting for.
        pending.slideDone = m_slideAnimation.isNull();
    } else {
        // Nothing moves: the closing member was not the visible one, or it has
        // no living neighbour to hand the stage to.
        pending.slideDone = true;
    }

    m_pendingCloses.append(pending);
    reportSettledCloses();
}

void DockGroupHost::onPanelFarewellFinished(DockPanel* panel)
{
    if (!panel) {
        return;
    }

    bool known = false;
    for (PendingClose& pending : m_pendingCloses) {
        if (pending.panel.data() == panel) {
            pending.farewellDone = true;
            known = true;
        }
    }

    if (!known) {
        // A farewell nobody registered (a close started before this frame was
        // listening) still has to reach the manager.
        emit panelCloseRequested(panel);
        return;
    }

    reportSettledCloses();
}

void DockGroupHost::markPendingCloseSlidesDone()
{
    bool changed = false;
    for (PendingClose& pending : m_pendingCloses) {
        if (!pending.slideDone) {
            pending.slideDone = true;
            changed = true;
        }
    }

    if (!changed) {
        return;
    }

    // Queued: this is reached from stopSlide(), i.e. from the middle of a
    // setCurrentPanel() that is still setting up the next slide. Reporting a
    // close from there would run tree surgery straight back into this frame.
    QTimer::singleShot(0, this, &DockGroupHost::reportSettledCloses);
}

void DockGroupHost::reportSettledCloses()
{
    // Reporting re-enters this frame (the panel leaves the tree, the cell may
    // collapse), so an entry leaves the list BEFORE it is emitted, and the scan
    // restarts afterwards because the list can have changed underneath.
    for (int i = 0; i < m_pendingCloses.size();) {
        const PendingClose pending = m_pendingCloses[i];

        if (pending.panel.isNull()) {
            m_pendingCloses.removeAt(i);
            continue;
        }
        if (!pending.slideDone || !pending.farewellDone) {
            ++i;
            continue;
        }

        m_pendingCloses.removeAt(i);
        emit panelCloseRequested(pending.panel.data());
        i = 0;
    }
}

// ============================================================================
// Teardown
// ============================================================================

bool DockGroupHost::beginCollapse()
{
    if (m_collapsing || !m_leaf) {
        return false;
    }

    DockPanel* survivor = m_leaf->panel();
    DockPanel* departed = m_currentPanel.data();

    // The member that left was not the visible one (or there is nothing left to
    // show): the frame can go straight to its farewell.
    if (!survivor || !departed || survivor == departed) {
        return false;
    }

    if (!m_animationsEnabled || m_animationDuration <= 0 || !isVisible()
        || m_viewport->width() <= 0) {
        return false;
    }

    // Direction from the order the strip still has: the survivor comes in from
    // the side it was sitting on.
    const QList<DockPanel*> order = hostedPanels();
    const int gone = static_cast<int>(order.indexOf(departed));
    const int taking = static_cast<int>(order.indexOf(survivor));
    const SlideDirection direction
        = (taking >= gone) ? SlideDirection::FromRight : SlideDirection::FromLeft;

    m_collapsing = true;

    // Hand the departed member back before the slide: it is closed (hidden) or
    // already floating, and it must not travel with the frame.
    releasePanel(departed);
    m_currentPanel = nullptr;

    m_header->setPanels({ survivor });
    setCurrentPanel(survivor, /*animated=*/true, direction);

    // A slide that could not start (geometry not ready) must not strand the
    // frame: nothing would ever ask for its destruction. Queued, so the caller
    // is not re-entered mid-sweep.
    if (m_slideAnimation.isNull()) {
        QTimer::singleShot(0, this, [this]() { finishCollapse(); });
    }

    return true;
}

void DockGroupHost::finishCollapse()
{
    if (!m_collapsing) {
        return;
    }
    m_collapsing = false;
    emit collapseFinished();
}

void DockGroupHost::detachFromLayout()
{
    stopSlide();

    // Hand back only what this frame still physically holds: releasePanel()
    // ignores a panel that already lives somewhere else, which is exactly the
    // case after a layout rebuild handed the members to their new frames.
    const QList<QPointer<DockPanel>> hosted = m_hosted;
    for (const auto& p : hosted) {
        releasePanel(p.data());
    }
    m_hosted.clear();
    m_currentPanel = nullptr;

    // The leaf that owned this frame is gone (or has disowned it); nothing may
    // route header or layout events back into it from here on.
    m_leaf = nullptr;
    if (m_header) {
        m_header->setPanels({});
        m_header->setDropInsertIndex(-1);
    }
}

void DockGroupHost::runFarewell(int duration)
{
    stopSlide();

    // Detach from the model first: from here on this frame is a pure visual,
    // and nothing must route layout or header events back into a dead leaf.
    m_leaf = nullptr;
    m_hosted.clear();
    m_currentPanel = nullptr;
    if (m_header) {
        m_header->setPanels({});
        m_header->setDropInsertIndex(-1);
    }
    if (m_viewport) {
        m_viewport->hide();
    }

    const int header = headerHeight();
    if (duration <= 0 || header <= 0 || !isVisible() || !anim::enabled()) {
        hide();
        deleteLater();
        return;
    }

    // Shrink to the header strip, then collapse it upward. The survivor is
    // growing into this exact band over the same duration, so the two read as
    // one motion: the strip is not removed, it is absorbed.
    const QRect start(pos(), QSize(width(), header));
    setGeometry(start);
    raise();

    auto* collapseAnim = new QVariantAnimation(this);
    collapseAnim->setStartValue(1.0);
    collapseAnim->setEndValue(0.0);
    collapseAnim->setDuration(anim::duration(duration));
    collapseAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(
        collapseAnim, &QVariantAnimation::valueChanged, this, [this, start](const QVariant& value) {
            const int h = qMax(0, qRound(start.height() * value.toReal()));
            setGeometry(QRect(start.x(), start.y(), start.width(), h));
        });
    connect(collapseAnim, &QVariantAnimation::finished, this, [this]() {
        hide();
        deleteLater();
    });
    collapseAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ============================================================================
// Geometry
// ============================================================================

int DockGroupHost::headerHeight() const
{
    return m_header ? m_header->barHeight() : 0;
}

QRect DockGroupHost::memberAreaRect() const
{
    const int header = headerHeight();
    return QRect(0, header, width(), qMax(0, height() - header));
}

QRect DockGroupHost::memberAreaInParent() const
{
    return memberAreaRect().translated(pos());
}

void DockGroupHost::layoutChildren()
{
    const int header = headerHeight();
    m_header->setGeometry(0, 0, width(), header);
    m_viewport->setGeometry(memberAreaRect());
}

void DockGroupHost::paintEvent(QPaintEvent* /*event*/)
{
    // The stage a member stands on. It is uncovered whenever a member is in
    // flight — the gap behind a sliding tab, the slot a dropped panel is about
    // to land in, the whole area while the group collapses — and with
    // WA_NoSystemBackground that gap would otherwise be the container's own
    // background showing through, a hole punched in the cell.
    //
    // Same surface as the header strip above it, so the frame reads as one
    // continuous piece of chrome with the members sitting on it.
    const QRect member = memberAreaRect();
    if (member.isEmpty()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_colors.surfaceAlt);
    painter.drawPath(buildMemberBackdrop(QRectF(member), m_header ? m_header->cornerRadius() : 0));
}

void DockGroupHost::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    layoutChildren();

    // A resize mid-slide would leave members at stale offsets; land immediately
    // instead of interpolating against a rect that no longer exists.
    if (!m_slideAnimation.isNull()) {
        stopSlide();
    }
    applyRestingGeometry();

    // stopSlide() suppresses the finished handler, so a collapse cut short here
    // would never ask to be destroyed and the strip would stay forever.
    finishCollapse();
}

// ============================================================================
// Theme
// ============================================================================

void DockGroupHost::applyTheme(const ruwa::ui::core::ThemeColors& colors)
{
    m_colors = colors;
    if (m_header) {
        m_header->applyTheme(colors);
    }
    layoutChildren();
    applyRestingGeometry();
    update();
}

} // namespace ruwa::ui::docking
