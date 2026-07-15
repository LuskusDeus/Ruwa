// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   O V E R L A Y   L A Y O U T   ( E N G I N E )
// ==========================================================================

#include "CanvasOverlayLayout.h"

#include <QPropertyAnimation>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <cmath>

namespace ruwa::ui::workspace {

namespace {

/// Smoothing time constant (ms) of the drag follow. Larger = softer/slower trailing lag.
constexpr qreal kDragFollowTauMs = 55.0;
/// Follow tick interval (ms). dt-based smoothing makes the exact value non-critical.
constexpr int kDragFollowTickMs = 8;

} // namespace

CanvasOverlayLayout::CanvasOverlayLayout(QWidget* content, QObject* parent)
    : QObject(parent)
    , m_content(content)
{
}

CanvasOverlayLayout::~CanvasOverlayLayout() = default;

// ---------------------------------------------------------------------------
// Registration
// ---------------------------------------------------------------------------

void CanvasOverlayLayout::registerItem(QWidget* w, Anchor anchor, Caps caps, int priority)
{
    if (!w) {
        return;
    }
    if (Item* existing = find(w)) {
        existing->anchor = anchor;
        existing->caps = caps;
        existing->priority = priority;
        return;
    }
    Item it;
    it.widget = w;
    it.anchor = anchor;
    it.caps = caps;
    it.priority = priority;
    m_items.append(it);
}

void CanvasOverlayLayout::unregisterItem(QWidget* w)
{
    if (m_dragFollowWidget == w) {
        stopDragFollow();
    }
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].widget == w) {
            // anim is parented to the widget; only delete it ourselves while the
            // widget is still alive (otherwise Qt already destroyed it).
            if (m_items[i].widget && m_items[i].anim) {
                m_items[i].anim->stop();
                delete m_items[i].anim;
            }
            m_items.removeAt(i);
            return;
        }
    }
}

bool CanvasOverlayLayout::isRegistered(QWidget* w) const
{
    return find(w) != nullptr;
}

void CanvasOverlayLayout::setContentWidget(QWidget* content)
{
    m_content = content;
}

CanvasOverlayLayout::Item* CanvasOverlayLayout::find(QWidget* w)
{
    for (Item& it : m_items) {
        if (it.widget == w) {
            return &it;
        }
    }
    return nullptr;
}

const CanvasOverlayLayout::Item* CanvasOverlayLayout::find(QWidget* w) const
{
    for (const Item& it : m_items) {
        if (it.widget == w) {
            return &it;
        }
    }
    return nullptr;
}

// ---------------------------------------------------------------------------
// Tunables
// ---------------------------------------------------------------------------

void CanvasOverlayLayout::setMargin(int px)
{
    m_margin = qMax(0, px);
}

void CanvasOverlayLayout::setSnapThreshold(int px)
{
    m_snapThreshold = qMax(0, px);
}

void CanvasOverlayLayout::setAnimationDurationMs(int ms)
{
    m_animMs = qMax(0, ms);
}

// ---------------------------------------------------------------------------
// Geometry
// ---------------------------------------------------------------------------

bool CanvasOverlayLayout::hasValidContent() const
{
    return m_content && m_content->width() > 0 && m_content->height() > 0;
}

QRect CanvasOverlayLayout::contentBounds() const
{
    if (!m_content) {
        return QRect();
    }
    const int w = m_content->width() - 2 * m_margin;
    const int h = m_content->height() - 2 * m_margin;
    return QRect(m_margin, m_margin, qMax(0, w), qMax(0, h));
}

QPoint CanvasOverlayLayout::clampToBounds(QWidget* w, const QPoint& pos) const
{
    if (!w || !hasValidContent()) {
        return pos;
    }
    const QRect b = contentBounds();
    const QSize s = w->size();
    const int minX = b.left();
    const int minY = b.top();
    const int maxX = b.left() + qMax(0, b.width() - s.width());
    const int maxY = b.top() + qMax(0, b.height() - s.height());
    return QPoint(qBound(minX, pos.x(), qMax(minX, maxX)), qBound(minY, pos.y(), qMax(minY, maxY)));
}

QList<QRect> CanvasOverlayLayout::solidObstacles(const Item& exclude) const
{
    QList<QRect> obs;
    for (const Item& it : m_items) {
        if (&it == &exclude) {
            continue;
        }
        if (!(it.caps & Solid) || !it.widget || !it.widget->isVisible()) {
            continue;
        }
        // An item yields only to obstacles at least as important as itself, so a
        // higher-priority anchored item (e.g. the tool HUD) stays put while the
        // lower-priority draggables flow around it. Equal priority blocks mutually.
        if (it.priority < exclude.priority) {
            continue;
        }
        obs.append(it.widget->geometry());
    }
    return obs;
}

QPoint CanvasOverlayLayout::resolveSolid(
    const Item& it, const QPoint& cur, const QPoint& desired) const
{
    if (!it.widget) {
        return desired;
    }
    const QList<QRect> obs = solidObstacles(it);
    if (obs.isEmpty()) {
        return desired;
    }

    const QSize sz = it.widget->size();

    // Axis-separated "wall": block penetration along the dominant motion and let
    // the item slide along the obstacle edge. X resolved against the current Y
    // span, then Y resolved against the already-resolved X span.
    int x = desired.x();
    {
        const int top = cur.y();
        const int bot = cur.y() + sz.height();
        const int curL = cur.x();
        const int curR = cur.x() + sz.width();
        for (const QRect& o : obs) {
            const int ol = o.x();
            const int orr = o.x() + o.width();
            const int ot = o.y();
            const int ob = o.y() + o.height();
            const bool vOverlap = top < ob && bot > ot;
            if (!vOverlap) {
                continue;
            }
            if (x > curL && curR <= ol && x + sz.width() > ol) { // moving right into obstacle
                x = qMin(x, ol - sz.width());
            } else if (x < curL && curL >= orr && x < orr) { // moving left into obstacle
                x = qMax(x, orr);
            }
        }
    }
    int y = desired.y();
    {
        const int newL = x;
        const int newR = x + sz.width();
        const int curT = cur.y();
        const int curB = cur.y() + sz.height();
        for (const QRect& o : obs) {
            const int ol = o.x();
            const int orr = o.x() + o.width();
            const int ot = o.y();
            const int ob = o.y() + o.height();
            const bool hOverlap = newL < orr && newR > ol;
            if (!hOverlap) {
                continue;
            }
            if (y > curT && curB <= ot && y + sz.height() > ot) { // moving down into obstacle
                y = qMin(y, ot - sz.height());
            } else if (y < curT && curT >= ob && y < ob) { // moving up into obstacle
                y = qMax(y, ob);
            }
        }
    }
    return QPoint(x, y);
}

QPoint CanvasOverlayLayout::applySnap(const Item& it, const QPoint& pos) const
{
    if (!(it.caps & SnapEdges) || !it.widget || !hasValidContent()) {
        return pos;
    }
    const QRect b = contentBounds();
    const QSize s = it.widget->size();
    const int minX = b.left();
    const int minY = b.top();
    const int maxX = b.left() + qMax(0, b.width() - s.width());
    const int maxY = b.top() + qMax(0, b.height() - s.height());

    int x = pos.x();
    int y = pos.y();
    if (qAbs(x - minX) <= m_snapThreshold) {
        x = minX;
    } else if (qAbs(x - maxX) <= m_snapThreshold) {
        x = maxX;
    }
    if (qAbs(y - minY) <= m_snapThreshold) {
        y = minY;
    } else if (qAbs(y - maxY) <= m_snapThreshold) {
        y = maxY;
    }
    return QPoint(x, y);
}

QPoint CanvasOverlayLayout::anchorPosition(const Item& it) const
{
    if (!it.widget) {
        return QPoint();
    }
    const QRect b = contentBounds();
    const QSize s = it.widget->size();
    const int left = b.left();
    const int top = b.top();
    const int right = b.left() + qMax(0, b.width() - s.width());
    const int bottom = b.top() + qMax(0, b.height() - s.height());
    const int cx = b.left() + (b.width() - s.width()) / 2;
    const int cy = b.top() + (b.height() - s.height()) / 2;

    switch (it.anchor) {
    case Anchor::TopLeft:
        return QPoint(left, top);
    case Anchor::TopCenter:
        return QPoint(qBound(left, cx, right), top);
    case Anchor::TopRight:
        return QPoint(right, top);
    case Anchor::CenterLeft:
        return QPoint(left, qBound(top, cy, bottom));
    case Anchor::Center:
        return QPoint(qBound(left, cx, right), qBound(top, cy, bottom));
    case Anchor::CenterRight:
        return QPoint(right, qBound(top, cy, bottom));
    case Anchor::BottomLeft:
        return QPoint(left, bottom);
    case Anchor::BottomCenter:
        return QPoint(qBound(left, cx, right), bottom);
    case Anchor::BottomRight:
        return QPoint(right, bottom);
    }
    return QPoint(left, top);
}

QPointF CanvasOverlayLayout::toNormalized(const Item& it, const QPoint& pos) const
{
    // Track-normalized: fraction along the free travel range (0 = min edge, 1 = max
    // edge), so an item docked to an edge/corner stays docked across content resizes.
    if (!it.widget || !hasValidContent()) {
        return QPointF(-1, -1);
    }
    const QRect b = contentBounds();
    const QSize s = it.widget->size();
    const int trackW = b.width() - s.width();
    const int trackH = b.height() - s.height();
    const qreal nx = trackW > 0 ? qreal(pos.x() - b.left()) / trackW : 0.0;
    const qreal ny = trackH > 0 ? qreal(pos.y() - b.top()) / trackH : 0.0;
    return QPointF(qBound(0.0, nx, 1.0), qBound(0.0, ny, 1.0));
}

QPoint CanvasOverlayLayout::fromNormalized(const Item& it, const QPointF& norm) const
{
    if (!it.widget || !hasValidContent()) {
        return it.widget ? it.widget->pos() : QPoint();
    }
    const QRect b = contentBounds();
    const QSize s = it.widget->size();
    const int trackW = b.width() - s.width();
    const int trackH = b.height() - s.height();
    const int x = b.left() + qRound(qBound(0.0, norm.x(), 1.0) * qMax(0, trackW));
    const int y = b.top() + qRound(qBound(0.0, norm.y(), 1.0) * qMax(0, trackH));
    return QPoint(x, y);
}

void CanvasOverlayLayout::moveItem(Item& it, const QPoint& pos, bool animate, int durationMs)
{
    if (!it.widget) {
        return;
    }
    QPoint target = pos;
    if (it.caps & BoundsClamped) {
        target = clampToBounds(it.widget, pos);
    }

    const int duration = durationMs > 0 ? durationMs : m_animMs;
    if (animate && duration > 0 && it.widget->isVisible() && it.widget->pos() != target) {
        if (!it.anim) {
            it.anim = new QPropertyAnimation(it.widget, "pos", it.widget);
            it.anim->setEasingCurve(QEasingCurve::OutCubic);
        }
        it.anim->stop();
        it.anim->setDuration(duration);
        it.anim->setStartValue(it.widget->pos());
        it.anim->setEndValue(target);
        it.anim->start();
    } else {
        if (it.anim) {
            it.anim->stop();
        }
        if (it.widget->pos() != target) {
            it.widget->move(target);
        }
    }
    emit itemMoved(it.widget, target);
}

// ---------------------------------------------------------------------------
// Drag
// ---------------------------------------------------------------------------

QPoint CanvasOverlayLayout::applyDrag(QWidget* w, const QPoint& desiredTopLeft)
{
    Item* it = find(w);
    if (!it || !it->widget || !hasValidContent()) {
        if (w) {
            w->move(desiredTopLeft);
        }
        return desiredTopLeft;
    }
    it->userMoved = true;

    // First frame of a drag: seed the logical position from where the widget is.
    if (m_activeDrag != w) {
        m_activeDrag = w;
        m_dragLogicalPos = it->widget->pos();
    }

    // Resolve the wall in logical space (from the previous logical target, not the
    // lagging visual position) so collisions track the cursor precisely.
    const QPoint desiredClamped = clampToBounds(w, desiredTopLeft);
    const QPoint resolved = clampToBounds(w, resolveSolid(*it, m_dragLogicalPos, desiredClamped));
    m_dragLogicalPos = resolved;
    it->pendingNorm = toNormalized(*it, resolved);

    if (it->caps & NoDragGlide) {
        // Strict 1:1 (the joystick runs its own swap logic on each move).
        moveItem(*it, resolved, /*animate*/ false);
    } else {
        // Constant trailing lag: the timer eases the widget toward the live target
        // for the whole drag. Persistence tracks the true target, not the visual.
        startDragFollow(w, resolved);
        emit itemMoved(w, resolved);
    }
    return resolved;
}

void CanvasOverlayLayout::endDrag(QWidget* w)
{
    Item* it = find(w);
    if (w == m_activeDrag) {
        m_activeDrag = nullptr;
    }
    if (!it || !it->widget || !hasValidContent()) {
        stopDragFollow();
        return;
    }
    // Snap from the true target (the visible widget may still be trailing it).
    const bool wasFollowing = (m_dragFollowWidget == w);
    const QPoint base = wasFollowing ? m_dragFollowTarget : it->widget->pos();
    stopDragFollow();

    QPoint snapped = applySnap(*it, base);
    snapped = resolveSolid(*it, base, snapped);
    snapped = clampToBounds(it->widget, snapped);

    it->userMoved = true;
    it->pendingNorm = toNormalized(*it, snapped);
    moveItem(*it, snapped, /*animate*/ true);
}

void CanvasOverlayLayout::startDragFollow(QWidget* w, const QPoint& target)
{
    m_dragFollowWidget = w;
    m_dragFollowTarget = target;
    if (!m_dragFollowTimer) {
        m_dragFollowTimer = new QTimer(this);
        m_dragFollowTimer->setTimerType(Qt::PreciseTimer);
        m_dragFollowTimer->setInterval(kDragFollowTickMs);
        connect(m_dragFollowTimer, &QTimer::timeout, this, &CanvasOverlayLayout::updateDragFollow);
    }
    if (!m_dragFollowTimer->isActive()) {
        m_dragFollowClock.restart();
        m_dragFollowTimer->start();
    }
}

void CanvasOverlayLayout::stopDragFollow()
{
    m_dragFollowWidget = nullptr;
    if (m_dragFollowTimer) {
        m_dragFollowTimer->stop();
    }
}

void CanvasOverlayLayout::updateDragFollow()
{
    if (!m_dragFollowWidget) {
        stopDragFollow();
        return;
    }
    QWidget* w = m_dragFollowWidget;
    const qint64 dtMs = qMax<qint64>(1, m_dragFollowClock.restart());

    // Exponential approach: fraction of the remaining gap to close this tick,
    // derived from real elapsed time so timer jitter never changes the lag.
    const qreal alpha = 1.0 - std::exp(-static_cast<qreal>(dtMs) / kDragFollowTauMs);
    const QPointF cur(w->pos());
    const QPointF tgt(m_dragFollowTarget);
    const QPointF next = cur + (tgt - cur) * alpha;
    const QPoint nextPx(qRound(next.x()), qRound(next.y()));
    if (w->pos() != nextPx) {
        w->move(nextPx);
    }
}

QPoint CanvasOverlayLayout::setItemPosition(QWidget* w, const QPoint& topLeft, bool animate)
{
    Item* it = find(w);
    if (!it || !it->widget || !hasValidContent()) {
        if (w) {
            w->move(topLeft);
        }
        return topLeft;
    }
    it->userMoved = true;
    QPoint p = clampToBounds(w, topLeft);
    p = resolveSolid(*it, it->widget->pos(), p);
    p = clampToBounds(w, p);
    it->pendingNorm = toNormalized(*it, p);
    moveItem(*it, p, animate);
    return p;
}

// ---------------------------------------------------------------------------
// Programmatic placement
// ---------------------------------------------------------------------------

void CanvasOverlayLayout::placeDefault(QWidget* w, bool animate)
{
    Item* it = find(w);
    if (!it || !it->widget || !hasValidContent()) {
        return;
    }
    it->userMoved = false;
    it->pendingNorm.reset();
    QPoint p = anchorPosition(*it);
    p = resolveSolid(*it, it->widget->pos(), p);
    p = clampToBounds(it->widget, p);
    moveItem(*it, p, animate);
}

void CanvasOverlayLayout::placeItem(Item& it, bool animate)
{
    if (!it.widget || (it.caps & Transient) || it.widget == m_activeDrag) {
        return;
    }
    QPoint target;
    if (it.pendingNorm.has_value()) {
        target = fromNormalized(it, *it.pendingNorm);
    } else {
        target = anchorPosition(it);
    }
    target = resolveSolid(it, it.widget->pos(), target);
    target = clampToBounds(it.widget, target);
    moveItem(it, target, animate && it.widget->isVisible());
}

void CanvasOverlayLayout::relayout(bool animate)
{
    if (!hasValidContent()) {
        return;
    }
    // Settle higher-priority items first so lower-priority ones avoid them.
    QList<Item*> order;
    order.reserve(m_items.size());
    for (Item& it : m_items) {
        order.append(&it);
    }
    std::stable_sort(order.begin(), order.end(),
        [](const Item* a, const Item* b) { return a->priority > b->priority; });

    for (Item* it : order) {
        placeItem(*it, animate);
    }
}

void CanvasOverlayLayout::relayoutItem(QWidget* w, bool animate)
{
    if (!hasValidContent()) {
        return;
    }
    if (Item* it = find(w)) {
        placeItem(*it, animate);
    }
}

// ---------------------------------------------------------------------------
// Persisted state
// ---------------------------------------------------------------------------

void CanvasOverlayLayout::markUserMoved(QWidget* w, bool moved)
{
    if (Item* it = find(w)) {
        it->userMoved = moved;
        if (!moved) {
            it->pendingNorm.reset();
        } else if (!it->pendingNorm.has_value() && it->widget && hasValidContent()) {
            it->pendingNorm = toNormalized(*it, it->widget->pos());
        }
    }
}

bool CanvasOverlayLayout::isUserMoved(QWidget* w) const
{
    const Item* it = find(w);
    return it && (it->userMoved || it->pendingNorm.has_value());
}

QPointF CanvasOverlayLayout::normalizedPosition(QWidget* w) const
{
    const Item* it = find(w);
    if (!it || !it->widget) {
        return QPointF(-1, -1);
    }
    if (it->pendingNorm.has_value()) {
        return *it->pendingNorm;
    }
    return toNormalized(*it, it->widget->pos());
}

void CanvasOverlayLayout::setNormalizedPosition(QWidget* w, const QPointF& norm)
{
    Item* it = find(w);
    if (!it) {
        return;
    }
    if (norm.x() < 0 || norm.y() < 0 || norm.x() > 1 || norm.y() > 1) {
        return;
    }
    // Keep tracking the fraction across resizes until the user actually drags;
    // the first apply may happen while the content is still at a transient size.
    it->pendingNorm = norm;
    if (it->widget && hasValidContent()) {
        QPoint p = fromNormalized(*it, norm);
        p = resolveSolid(*it, it->widget->pos(), p);
        p = clampToBounds(it->widget, p);
        moveItem(*it, p, /*animate*/ false);
    }
}

} // namespace ruwa::ui::workspace
