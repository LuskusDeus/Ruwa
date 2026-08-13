// SPDX-License-Identifier: MPL-2.0

#include "RadialMenuWidget.h"

#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/rendering/CanvasBackdropSource.h"
#include "shared/style/PaintingUtils.h"
#include "shared/widgets/ShortcutKeycapRenderer.h"

#include <QAbstractAnimation>
#include <QApplication>
#include <QEasingCurve>
#include <QEvent>
#include <QFontMetricsF>
#include <QHideEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QShowEvent>
#include <QTabletEvent>
#include <QVariantAnimation>
#include <QtMath>

#include <cmath>

namespace ruwa::ui::widgets {

using ruwa::ui::core::ThemeColors;
using ruwa::ui::core::ThemeManager;

namespace {

constexpr int kShowDurationMs = 230;
constexpr int kHideDurationMs = 170;
constexpr int kHoverDurationMs = 280;
constexpr int kPageTransitionMs = 300;

/// How far the outgoing pieces are thrown, as a fraction of the ring they sat
/// on, before the falloff below is applied.
constexpr qreal kGhostPushFactor = 0.75;
/// Distance falloff of that throw, as a fraction of the ring radius: pieces
/// further from the pressed seat are pushed less.
constexpr qreal kGhostPushFalloff = 1.0;
/// Fraction of the transition the outgoing pieces have to be gone by.
constexpr qreal kGhostFadeEnd = 0.7;
/// When the incoming page starts coming up. It overlaps the exit rather than
/// waiting for it, or the menu reads as empty for a moment.
constexpr qreal kEntranceStart = 0.3;
/// The morphing piece drops its old icon over this much of the transition.
constexpr qreal kMorphIconFade = 0.45;

/// How much the hovered seat grows.
constexpr qreal kHoverScale = 0.18;
/// How far the seat next to the hovered one gives way, as a fraction of its own
/// width. Seats further round the ring get a fraction of that again.
constexpr qreal kNeighbourPush = 1.0 / 3.0;
/// Per-step decay of the push: the second seat out moves this much of what the
/// first one did.
constexpr qreal kNeighbourPushDecay = 0.38;

/// Shortest signed way from @p from to @p to on a circle, in radians. The
/// highlight always rotates the short way round the ring.
qreal shortestAngleDelta(qreal from, qreal to)
{
    qreal delta = to - from;
    while (delta <= -M_PI) {
        delta += 2.0 * M_PI;
    }
    while (delta > M_PI) {
        delta -= 2.0 * M_PI;
    }
    return delta;
}

/// Linear colour blend; the hover crossfades every colour it touches.
QColor lerpColor(const QColor& from, const QColor& to, qreal t)
{
    const qreal k = qBound<qreal>(0.0, t, 1.0);
    return QColor(qRound(from.red() + (to.red() - from.red()) * k),
        qRound(from.green() + (to.green() - from.green()) * k),
        qRound(from.blue() + (to.blue() - from.blue()) * k),
        qRound(from.alpha() + (to.alpha() - from.alpha()) * k));
}

/// How far the opening animation has to have run before a release can pick a
/// seat. A right-click that is over almost as soon as it began is a click, not
/// a press-and-hold gesture, and the seat under the cursor at that instant is
/// not something the user has had time to aim at.
constexpr qreal kMinSelectProgress = 0.6;

/// Ring radius factor at progress 0 — the menu grows out of the cursor.
constexpr qreal kRingScaleStart = 0.82;

QPainterPath wedgePath(
    const QPointF& center, qreal innerRadius, qreal outerRadius, qreal startDeg, qreal spanDeg)
{
    const QRectF outerRect(
        center.x() - outerRadius, center.y() - outerRadius, outerRadius * 2.0, outerRadius * 2.0);
    const QRectF innerRect(
        center.x() - innerRadius, center.y() - innerRadius, innerRadius * 2.0, innerRadius * 2.0);

    QPainterPath path;
    path.arcMoveTo(outerRect, startDeg);
    path.arcTo(outerRect, startDeg, spanDeg);
    path.arcTo(innerRect, startDeg + spanDeg, -spanDeg);
    path.closeSubpath();
    return path;
}

/// Wash pulled over the frost on the hovered piece. The seat cannot go opaque
/// primary the way an ordinary button does — that would throw the glass away
/// exactly where the eye is.
constexpr int kAccentWashAlpha = 150;

} // namespace

RadialMenuWidget::RadialMenuWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    // The app-wide ContextMenuSystem answers every right-press with its generic
    // "no actions" menu; the radial menu is its own answer to right-click.
    setProperty("ruwaContextMenuSystemBypass", true);
    // Mouse propagation stays ON: presses and wheels that the menu declines
    // (middle-drag pan, zoom) have to reach the canvas underneath, which is
    // reachable only by walking up the parent chain.
    setMouseTracking(true);
    setFocusPolicy(Qt::NoFocus);
    hide();

    m_progressAnim = new QVariantAnimation(this);
    m_progressAnim->setStartValue(0.0);
    m_progressAnim->setEndValue(1.0);
    m_progressAnim->setEasingCurve(QEasingCurve::OutCubic);
    connect(m_progressAnim, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& value) { setShowProgress(value.toReal()); });
    connect(m_progressAnim, &QVariantAnimation::finished, this, [this]() {
        if (m_isHiding) {
            hideImmediate();
        }
    });

    // One animation drives every hover weight at once: each seat eases from the
    // value it had when the target last changed towards its new target, so a
    // hover moving along the ring crossfades instead of snapping between seats.
    m_hoverAnim = new QVariantAnimation(this);
    m_hoverAnim->setStartValue(0.0);
    m_hoverAnim->setEndValue(1.0);
    m_hoverAnim->setDuration(kHoverDurationMs);
    // Left linear: the two things it drives want different curves, applied
    // below. Fades ease out, so they commit early and settle; the wedge's
    // rotation eases in and out, which is what makes the swing read as one
    // object turning rather than a value being pushed.
    m_hoverAnim->setEasingCurve(QEasingCurve::Linear);
    connect(m_hoverAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
        static const QEasingCurve fade(QEasingCurve::OutCubic);
        static const QEasingCurve swingFromRest(QEasingCurve::InOutCubic);
        static const QEasingCurve swingInFlight(QEasingCurve::OutCubic);

        const qreal raw = value.toReal();
        const qreal t = fade.valueForProgress(raw);
        const qreal rotation
            = (m_wedgeSwingFromRest ? swingFromRest : swingInFlight).valueForProgress(raw);

        for (int i = 0; i < m_slotHover.size(); ++i) {
            const qreal target = (i == m_hoveredSlot) ? 1.0 : 0.0;
            m_slotHover[i] = m_slotHoverFrom.value(i) + (target - m_slotHoverFrom.value(i)) * t;
        }
        m_hubHover = m_hubHoverFrom + ((m_hubHovered ? 1.0 : 0.0) - m_hubHoverFrom) * t;
        m_wedgeAngle = m_wedgeAngleFrom + (m_wedgeAngleTo - m_wedgeAngleFrom) * rotation;
        m_wedgeOpacity = m_wedgeOpacityFrom + (m_wedgeOpacityTo - m_wedgeOpacityFrom) * t;
        // Seats move and grow, so the frosted regions under them move too.
        if (m_backdropSource) {
            m_backdropSource->requestBackdropUpdate();
        }
        update();
    });

    m_transitionAnim = new QVariantAnimation(this);
    m_transitionAnim->setStartValue(0.0);
    m_transitionAnim->setEndValue(1.0);
    m_transitionAnim->setDuration(kPageTransitionMs);
    m_transitionAnim->setEasingCurve(QEasingCurve::Linear);
    connect(
        m_transitionAnim, &QVariantAnimation::valueChanged, this, [this](const QVariant& value) {
            m_transitionRaw = value.toReal();
            if (m_backdropSource) {
                m_backdropSource->requestBackdropUpdate();
            }
            update();
        });
    connect(m_transitionAnim, &QVariantAnimation::finished, this, [this]() {
        // The widget was grown to hold the outgoing page's flight path; with the
        // ghosts gone it can shrink back to what the current page needs, and the
        // mask with it.
        const QPoint previousCenter = geometry().center();
        clearTransition();
        rebuildMetrics();
        if (isVisible()) {
            move(previousCenter - QPoint(width() / 2, height() / 2));
        }
        update();
    });

    rebuildMetrics();
}

// ======================================================================================
//   C O N T E N T
// ======================================================================================

void RadialMenuWidget::setPage(const Page& page, PageTransition transition, int pivotIndex)
{
    // Only animate a swap that is actually visible, and never stack two: a
    // second swap mid-flight (a live configuration edit) lands on the new page.
    const bool animate = transition != PageTransition::None && isVisible() && !m_isHiding
        && !m_page.items.isEmpty();
    clearTransition();
    if (animate) {
        captureTransition(transition, pivotIndex, page);
    }

    m_page = page;
    m_hoveredSlot = -1;
    m_hubHovered = false;
    // A page swap is not a hover transition: land on the new page at rest.
    m_hoverAnim->stop();
    m_slotHover.fill(0.0, m_page.items.size());
    m_slotHoverFrom = m_slotHover;
    m_hubHover = 0.0;
    m_hubHoverFrom = 0.0;
    m_wedgeOpacity = 0.0;
    m_wedgeOpacityFrom = 0.0;
    m_wedgeOpacityTo = 0.0;

    const QPoint previousCenter = geometry().center();
    rebuildMetrics();
    if (isVisible()) {
        move(previousCenter - QPoint(width() / 2, height() / 2));
    }

    if (animate) {
        finishTransitionSetup();
        m_transitionRaw = 0.0;
        m_transitionAnim->start();
    }
    update();
}

// ======================================================================================
//   P A G E   T R A N S I T I O N
// ======================================================================================

void RadialMenuWidget::captureTransition(
    PageTransition transition, int pivotIndex, const Page& incoming)
{
    // Read the outgoing geometry before the flag goes up: currentRingScale()
    // starts reporting the *incoming* page's entrance the moment it does.
    const QPointF origin = center();
    const qreal ringScale = currentRingScale();

    m_transitionActive = true;
    m_transitionForward = transition == PageTransition::IntoGroup;
    m_transitionPivot = m_transitionForward ? -1 : pivotIndex;
    m_ghosts.clear();

    // Everything the outgoing page is currently showing, stored centre-relative
    // so the resize that comes with the new page cannot shift it.
    const auto relative = [origin](const QRectF& rect) { return rect.translated(-origin); };

    const int pivot = m_transitionForward ? pivotIndex : -1;

    QRectF hubRect(origin.x() - m_metrics.hubRadius, origin.y() - m_metrics.hubRadius,
        m_metrics.hubRadius * 2.0, m_metrics.hubRadius * 2.0);

    if (m_transitionForward) {
        // The picked seat does not leave: it grows into the new hub.
        const Slot picked = m_page.items.value(pivot);
        m_morphFrom = relative(slotRect(pivot, ringScale));
        m_morphFromRadius = m_metrics.slotCorner;
        m_morphIcon = picked.icon;

        Ghost hub;
        hub.rect = relative(hubRect);
        hub.radius = m_metrics.hubRadius;
        m_ghosts.append(hub);
    } else {
        // Going back: the hub is what morphs, into the seat it came from.
        m_morphFrom = relative(hubRect);
        m_morphFromRadius = m_metrics.hubRadius;
        m_morphIcon = incoming.items.value(pivotIndex).icon;
    }

    for (int index = 0; index < m_page.items.size(); ++index) {
        const Slot& slot = m_page.items.at(index);
        if (slot.empty) {
            continue;
        }

        // The picked seat itself is the morphing piece, but its caption still
        // leaves with everything else.
        if (index != pivot) {
            Ghost seat;
            seat.rect = relative(slotRect(index, ringScale));
            seat.radius = m_metrics.slotCorner;
            seat.icon = slot.icon;
            seat.title = slot.title;
            seat.enabled = slot.enabled;
            m_ghosts.append(seat);
        }

        const QRectF pill = labelRect(index, ringScale);
        if (pill.isEmpty()) {
            continue;
        }
        Ghost label;
        label.rect = relative(pill);
        label.radius = m_metrics.pillRadius;
        label.title = slot.title;
        label.shortcut = slot.shortcut;
        label.enabled = slot.enabled;
        label.label = true;
        m_ghosts.append(label);
    }

    const QRectF title = titlePillRect();
    if (!title.isEmpty()) {
        Ghost pill;
        pill.rect = relative(title);
        pill.radius = m_metrics.titlePillHeight / 2.0;
        pill.title = m_page.title;
        pill.label = true;
        m_ghosts.append(pill);
    }

    // Everything is thrown away from the piece the transition turns on: the
    // pressed seat going in, the hub coming back out. Closer pieces are thrown
    // harder, which is what makes the pivot read as the source of the motion.
    const QPointF pushOrigin = m_morphFrom.center();
    const qreal reach = qMax<qreal>(1.0, m_metrics.ringRadius);
    for (Ghost& ghost : m_ghosts) {
        QPointF away = ghost.rect.center() - pushOrigin;
        qreal distance = std::hypot(away.x(), away.y());
        if (distance < 1e-3) {
            // Dead centre on the pivot: send it straight out from the menu.
            away = QPointF(0.0, -1.0);
            distance = 1.0;
        }
        const qreal falloff = qExp(-distance / (reach * kGhostPushFalloff));
        ghost.push = (away / distance) * (reach * kGhostPushFactor * falloff);
    }
}

void RadialMenuWidget::finishTransitionSetup()
{
    const QPointF origin = center();
    if (m_transitionForward) {
        m_morphTo = QRectF(-m_metrics.hubRadius, -m_metrics.hubRadius, m_metrics.hubRadius * 2.0,
            m_metrics.hubRadius * 2.0);
        m_morphToRadius = m_metrics.hubRadius;
    } else if (m_transitionPivot >= 0 && m_transitionPivot < m_page.items.size()) {
        m_morphTo = slotRect(m_transitionPivot, 1.0).translated(-origin);
        m_morphToRadius = m_metrics.slotCorner;
    } else {
        // No seat to fold into (the layout changed under us): fall back to a
        // plain fade by morphing in place.
        m_morphTo = m_morphFrom;
        m_morphToRadius = m_morphFromRadius;
    }
}

void RadialMenuWidget::clearTransition()
{
    m_transitionAnim->stop();
    m_transitionActive = false;
    m_transitionPivot = -1;
    m_transitionRaw = 0.0;
    m_ghosts.clear();
    m_morphIcon = QIcon();
}

qreal RadialMenuWidget::transitionProgress() const
{
    if (!m_transitionActive) {
        return 1.0;
    }
    static const QEasingCurve curve(QEasingCurve::InOutCubic);
    return curve.valueForProgress(qBound<qreal>(0.0, m_transitionRaw, 1.0));
}

qreal RadialMenuWidget::transitionEntrance() const
{
    if (!m_transitionActive) {
        return 1.0;
    }
    static const QEasingCurve curve(QEasingCurve::OutCubic);
    const qreal raw
        = qBound<qreal>(0.0, (m_transitionRaw - kEntranceStart) / (1.0 - kEntranceStart), 1.0);
    return curve.valueForProgress(raw);
}

qreal RadialMenuWidget::entranceAmount() const
{
    return m_showProgress * transitionEntrance();
}

void RadialMenuWidget::rebuildMetrics()
{
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    Metrics metrics;
    metrics.hubRadius = theme.scaled(28);
    // A seat is a 34px square, near enough to a ToolsPanel button (36px, corner
    // radius 6) that the two read as the same control.
    metrics.slotRadius = theme.scaled(17);
    metrics.slotCorner = theme.scaled(6);
    metrics.labelGap = theme.scaled(10);
    metrics.pillHeight = theme.scaled(28);
    metrics.pillRadius = metrics.pillHeight / 2.0;
    metrics.titlePillHeight = theme.scaled(26);

    // Seat spacing is deliberately not tied to the button size: the buttons are
    // small, but the ring keeps the wider reach a radial menu needs to stay easy
    // to flick at. The chord between two seats is 2*R*sin(pi/N).
    const qreal seatPitch = theme.scaled(21);
    const int slotCount = qMax(1, m_page.items.size());
    const qreal minRing = (slotCount > 1) ? (seatPitch + theme.scaled(9)) / qSin(M_PI / slotCount)
                                          : metrics.hubRadius + seatPitch;
    metrics.ringRadius = qMax<qreal>(theme.scaled(77), minRing);

    const QFont labelFont = colors.fonts.getUIFont(theme.scaledFontSize(10));
    const QFontMetricsF labelMetrics(labelFont);
    const qreal pillPadding = theme.scaled(12);

    m_labelSizes.clear();
    m_labelSizes.reserve(m_page.items.size());
    qreal widestLabel = 0.0;
    for (const Slot& slot : m_page.items) {
        if (slot.empty || slot.title.isEmpty()) {
            m_labelSizes.append(QSizeF());
            continue;
        }

        qreal width = pillPadding * 2.0 + labelMetrics.horizontalAdvance(slot.title);
        if (ShortcutKeycapRenderer::isRenderable(slot.shortcut)) {
            width += theme.scaled(6)
                + ShortcutKeycapRenderer::contentSize(
                    slot.shortcut, ShortcutKeycapRenderer::SizeVariant::Compact)
                      .width();
        }
        const QSizeF size(width, metrics.pillHeight);
        m_labelSizes.append(size);
        widestLabel = qMax(widestLabel, size.width());
    }

    metrics.sideGutter = metrics.labelGap + widestLabel + theme.scaled(6);
    metrics.verticalGutter
        = metrics.labelGap * 2.0 + metrics.pillHeight + metrics.titlePillHeight + theme.scaled(14);
    m_metrics = metrics;

    const int half = qRound(metrics.ringRadius + metrics.slotRadius);
    int halfWidth = half + qRound(metrics.sideGutter);
    int halfHeight = half + qRound(metrics.verticalGutter);

    // The outgoing page is thrown outward and is usually laid out for a
    // different number of seats than the incoming one, so while a transition
    // runs the widget has to be big enough for both at their furthest.
    for (const Ghost& ghost : m_ghosts) {
        const QRectF flown = ghost.rect.translated(ghost.push);
        halfWidth = qMax(halfWidth, qCeil(qMax(qAbs(flown.left()), qAbs(flown.right()))));
        halfHeight = qMax(halfHeight, qCeil(qMax(qAbs(flown.top()), qAbs(flown.bottom()))));
    }

    setFixedSize(QSize(2 * halfWidth, 2 * halfHeight));
    updateMask();
}

// ======================================================================================
//   G E O M E T R Y
// ======================================================================================

QPointF RadialMenuWidget::center() const
{
    return QRectF(rect()).center();
}

qreal RadialMenuWidget::seatAngle(int index) const
{
    const int count = m_page.items.size();
    return (count > 0) ? (2.0 * M_PI * index) / count : 0.0;
}

qreal RadialMenuWidget::hoverAmount(int index) const
{
    return (index >= 0 && index < m_slotHover.size()) ? m_slotHover.at(index) : 0.0;
}

qreal RadialMenuWidget::hoverAngleOffset(int index) const
{
    const int count = m_page.items.size();
    if (count <= 2 || m_metrics.ringRadius <= 0.0) {
        return 0.0;
    }

    const qreal step = (2.0 * M_PI) / count;
    // The push is a distance along the ring — a third of a button's width —
    // turned into the angle that covers it at this radius.
    const qreal maxPush = (m_metrics.slotRadius * 2.0 * kNeighbourPush) / m_metrics.ringRadius;

    qreal offset = 0.0;
    for (int source = 0; source < count; ++source) {
        const qreal weight = hoverAmount(source);
        if (source == index || weight <= 0.001) {
            continue;
        }

        // Signed shortest way round from the hovered seat to this one; its sign
        // is the side this seat has to give way towards.
        const qreal delta = shortestAngleDelta(0.0, (index - source) * step);
        // Directly opposite there is no "away" to move to, and the falloff has
        // long since died anyway.
        if (qAbs(qAbs(delta) - M_PI) < 1e-6) {
            continue;
        }

        const qreal steps = qAbs(delta) / step;
        const qreal falloff = qPow(kNeighbourPushDecay, steps - 1.0);
        offset += weight * (delta > 0.0 ? 1.0 : -1.0) * maxPush * falloff;
    }
    return offset;
}

QPointF RadialMenuWidget::slotDirection(int index) const
{
    const int count = m_page.items.size();
    if (count <= 0) {
        return QPointF(0.0, -1.0);
    }
    // Seat 0 sits at the top and the ring runs clockwise, matching the order
    // slots are listed in the configuration.
    const qreal angle = seatAngle(index) + hoverAngleOffset(index);
    return QPointF(qSin(angle), -qCos(angle));
}

QPointF RadialMenuWidget::slotCenter(int index, qreal ringScale) const
{
    return center() + slotDirection(index) * (m_metrics.ringRadius * ringScale);
}

QSizeF RadialMenuWidget::labelSize(int index) const
{
    return (index >= 0 && index < m_labelSizes.size()) ? m_labelSizes.at(index) : QSizeF();
}

QRectF RadialMenuWidget::titlePillRect() const
{
    if (m_page.title.isEmpty()) {
        return QRectF();
    }

    const auto& theme = ThemeManager::instance();
    QFont font = theme.colors().fonts.getUIFont(theme.scaledFontSize(9));
    font.setWeight(QFont::DemiBold);

    const qreal padding = theme.scaled(14);
    const qreal width = QFontMetricsF(font).horizontalAdvance(m_page.title) + padding * 2.0;
    return QRectF(center().x() - width / 2.0,
        center().y() + m_metrics.ringRadius + m_metrics.slotRadius + m_metrics.labelGap
            + m_metrics.pillHeight + m_metrics.labelGap,
        width, m_metrics.titlePillHeight);
}

QRectF RadialMenuWidget::slotRect(int index, qreal ringScale) const
{
    const QPointF pos = slotCenter(index, ringScale);
    const qreal r = m_metrics.slotRadius * (1.0 + kHoverScale * hoverAmount(index));
    return QRectF(pos.x() - r, pos.y() - r, r * 2.0, r * 2.0);
}

QRectF RadialMenuWidget::labelRect(int index, qreal ringScale) const
{
    const QSizeF size = labelSize(index);
    if (size.isEmpty()) {
        return QRectF();
    }

    const QPointF direction = slotDirection(index);
    // Distance from the seat centre to the square's edge along this direction:
    // a diagonal seat's corner reaches further than an axis-aligned one's side,
    // and the label has to start beyond it either way.
    const qreal halfSize = m_metrics.slotRadius * (1.0 + kHoverScale * hoverAmount(index));
    const qreal reach = halfSize / qMax(qAbs(direction.x()), qAbs(direction.y()));
    const QPointF anchor = slotCenter(index, ringScale) + direction * (reach + m_metrics.labelGap);

    // Which side of its seat a label sits on is decided on the seat's resting
    // angle, not its pushed one: a seat sitting near the threshold would
    // otherwise flip its label across the button mid-hover.
    const qreal baseAngle = (2.0 * M_PI * index) / qMax(1, m_page.items.size());
    const QPointF baseDirection(qSin(baseAngle), -qCos(baseAngle));

    // Seats near the top and bottom of the ring have no room to the side, so
    // their label is centred above/below instead of pushed outward. Those pills
    // must clear the disc edge-on: straddling the anchor would sink half the
    // pill height back into the slot.
    qreal left = anchor.x() - size.width() / 2.0;
    qreal top = anchor.y() - size.height() / 2.0;
    if (baseDirection.x() > 0.25) {
        left = anchor.x();
    } else if (baseDirection.x() < -0.25) {
        left = anchor.x() - size.width();
    } else if (baseDirection.y() < 0.0) {
        top = anchor.y() - size.height();
    } else {
        top = anchor.y();
    }

    return QRectF(QPointF(left, top), size);
}

qreal RadialMenuWidget::currentRingScale() const
{
    return kRingScaleStart + (1.0 - kRingScaleStart) * entranceAmount();
}

qreal RadialMenuWidget::ringHitRadius(qreal ringScale) const
{
    // Square seats reach further than a disc did: a corner sits slotRadius*sqrt2
    // from the seat centre, and the band (and the mask built from it) has to
    // clear that.
    return m_metrics.ringRadius * ringScale + m_metrics.slotRadius * M_SQRT2
        + ThemeManager::instance().scaled(4);
}

QRegion RadialMenuWidget::menuRegion(qreal padding) const
{
    const QPointF c = center();
    const qreal outer = ringHitRadius(1.0) + padding;
    QRegion region(QRectF(c.x() - outer, c.y() - outer, outer * 2.0, outer * 2.0).toAlignedRect(),
        QRegion::Ellipse);

    const int pad = qRound(padding);
    for (int index = 0; index < m_page.items.size(); ++index) {
        const QRectF pill = labelRect(index, 1.0);
        if (pill.isEmpty()) {
            continue;
        }
        region += pill.toAlignedRect().adjusted(-pad, -pad, pad, pad);
    }

    if (!m_page.title.isEmpty()) {
        region += titlePillRect().toAlignedRect().adjusted(-pad, -pad, pad, pad);
    }

    // A ghost has to stay unmasked over its whole flight, so its start and its
    // landing both go in.
    const QPointF origin = center();
    for (const Ghost& ghost : m_ghosts) {
        const QRectF from = ghost.rect.translated(origin);
        const QRectF to = from.translated(ghost.push);
        region += from.united(to).toAlignedRect().adjusted(-pad, -pad, pad, pad);
    }
    return region;
}

bool RadialMenuWidget::containsMenuPoint(const QPointF& pos) const
{
    const QPointF offset = pos - center();
    if (std::hypot(offset.x(), offset.y()) <= ringHitRadius(currentRingScale())) {
        return true;
    }
    for (int index = 0; index < m_page.items.size(); ++index) {
        if (labelRect(index, currentRingScale()).contains(pos)) {
            return true;
        }
    }
    return false;
}

void RadialMenuWidget::updateMask()
{
    // The mask is built at rest, but a hover grows the seat under the pointer
    // and pushes its neighbours sideways, so the padding has to cover both;
    // the extra ring is dead space that slotAtPosition() rejects anyway.
    setMask(menuRegion(ThemeManager::instance().scaled(18)));
}

int RadialMenuWidget::slotAtPosition(const QPointF& pos) const
{
    const int count = m_page.items.size();
    // Nothing is pickable while a page is swapping: the seats are still moving
    // and the pointer has not been given a chance to aim at the new ones.
    if (count <= 0 || m_transitionActive) {
        return -1;
    }

    const qreal ringScale = currentRingScale();

    // A label sits outside the band and belongs to its own seat, so it is
    // matched by rect before the angular test runs.
    for (int index = 0; index < count; ++index) {
        if (!labelRect(index, ringScale).contains(pos)) {
            continue;
        }
        const Slot& slot = m_page.items.at(index);
        return (slot.empty || !slot.enabled) ? -1 : index;
    }

    const QPointF offset = pos - center();
    const qreal distance = std::hypot(offset.x(), offset.y());
    if (distance <= m_metrics.hubRadius || distance > ringHitRadius(ringScale)) {
        return -1;
    }

    // Angle measured from straight up, clockwise, so it maps directly onto the
    // seat order; half a step of bias puts the boundary between two seats.
    qreal angle = qAtan2(offset.x(), -offset.y());
    if (angle < 0.0) {
        angle += 2.0 * M_PI;
    }
    const qreal step = (2.0 * M_PI) / count;
    const int index = static_cast<int>(std::floor((angle + step / 2.0) / step)) % count;

    const Slot& slot = m_page.items.at(index);
    if (slot.empty || !slot.enabled) {
        return -1;
    }
    return index;
}

bool RadialMenuWidget::isInHub(const QPointF& pos) const
{
    if (m_transitionActive) {
        return false;
    }
    const QPointF offset = pos - center();
    return std::hypot(offset.x(), offset.y()) <= m_metrics.hubRadius;
}

// ======================================================================================
//   P R E S E N T A T I O N
// ======================================================================================

void RadialMenuWidget::showAt(const QPoint& centerInParent, bool armReleaseSelect)
{
    QPoint topLeft = centerInParent - QPoint(width() / 2, height() / 2);
    if (QWidget* parent = parentWidget()) {
        const int margin = ThemeManager::instance().scaled(8);
        topLeft.setX(qBound(margin, topLeft.x(), qMax(margin, parent->width() - width() - margin)));
        topLeft.setY(
            qBound(margin, topLeft.y(), qMax(margin, parent->height() - height() - margin)));
    }
    move(topLeft);

    m_isHiding = false;
    m_activated = false;
    m_hoveredSlot = -1;
    m_hubHovered = false;
    // Open at rest, never mid-crossfade from the last time the menu was up.
    m_hoverAnim->stop();
    m_slotHover.fill(0.0, m_page.items.size());
    m_slotHoverFrom = m_slotHover;
    m_hubHover = 0.0;
    m_hubHoverFrom = 0.0;
    m_wedgeOpacity = 0.0;
    m_wedgeOpacityFrom = 0.0;
    m_wedgeOpacityTo = 0.0;
    m_releaseSelectArmed = armReleaseSelect;
    setAttribute(Qt::WA_TransparentForMouseEvents, false);

    m_progressAnim->stop();
    setShowProgress(0.0);
    show();
    raise();

    m_progressAnim->setDuration(kShowDurationMs);
    m_progressAnim->setStartValue(showProgress());
    m_progressAnim->setEndValue(1.0);
    m_progressAnim->start();
}

void RadialMenuWidget::hideMenu(bool animate)
{
    if (!isVisible() || m_isHiding) {
        return;
    }
    if (!animate) {
        hideImmediate();
        return;
    }

    m_isHiding = true;
    m_releaseSelectArmed = false;
    // The menu is on its way out: stop taking input so the fade-out never eats
    // a wheel notch or a press meant for the canvas below it.
    setAttribute(Qt::WA_TransparentForMouseEvents, true);

    m_progressAnim->stop();
    m_progressAnim->setDuration(kHideDurationMs);
    m_progressAnim->setStartValue(showProgress());
    m_progressAnim->setEndValue(0.0);
    m_progressAnim->start();
}

void RadialMenuWidget::hideImmediate()
{
    m_progressAnim->stop();
    m_hoverAnim->stop();
    m_isHiding = false;
    m_hoveredSlot = -1;
    m_hubHovered = false;
    m_slotHover.fill(0.0, m_slotHover.size());
    m_slotHoverFrom = m_slotHover;
    m_hubHover = 0.0;
    m_hubHoverFrom = 0.0;
    m_wedgeOpacity = 0.0;
    m_wedgeOpacityFrom = 0.0;
    m_wedgeOpacityTo = 0.0;
    m_releaseSelectArmed = false;
    setShowProgress(0.0);
    hide();
}

void RadialMenuWidget::setShowProgress(qreal progress)
{
    m_showProgress = qBound(0.0, progress, 1.0);
    // Every piece rides the ring scale, so the frosted regions move with it and
    // the canvas has to redraw them on the same tick as this repaint.
    if (m_backdropSource) {
        m_backdropSource->requestBackdropUpdate();
    }
    update();
}

void RadialMenuWidget::dismiss()
{
    if (m_isHiding || !isVisible()) {
        return;
    }
    hideMenu();
    if (!m_activated) {
        emit dismissed();
    }
}

// ======================================================================================
//   I N P U T
// ======================================================================================

void RadialMenuWidget::applyHoverState(int slotIndex, bool hubHovered)
{
    if (slotIndex == m_hoveredSlot && hubHovered == m_hubHovered) {
        return;
    }
    m_hoveredSlot = slotIndex;
    m_hubHovered = hubHovered;
    startHoverAnimation();

    // Anything that reacts to a click gets the pointing hand; the canvas leaves
    // our rect alone because the menu is a cursor-exclusion widget.
    if (m_hoveredSlot >= 0 || m_hubHovered) {
        setCursor(Qt::PointingHandCursor);
    } else {
        unsetCursor();
    }
    update();
}

void RadialMenuWidget::startHoverAnimation()
{
    // Restarting from wherever the weights currently stand, rather than from
    // zero, is what keeps a hover swept round the ring continuous: an
    // interrupted crossfade carries its part-way values into the next one.
    // Read before stopping: a swing still in flight has to be continued at
    // speed, not restarted through another lead-in.
    const bool interrupted = m_hoverAnim->state() == QAbstractAnimation::Running
        && qAbs(m_wedgeAngleTo - m_wedgeAngle) > 1e-4;

    m_hoverAnim->stop();
    m_slotHover.resize(m_page.items.size());
    m_slotHoverFrom = m_slotHover;
    m_hubHoverFrom = m_hubHover;
    m_wedgeOpacityFrom = m_wedgeOpacity;
    m_wedgeSwingFromRest = !interrupted;

    if (m_hoveredSlot >= 0) {
        const qreal target = seatAngle(m_hoveredSlot);
        if (m_wedgeOpacity <= 0.001) {
            // Nothing on screen to turn: the wedge is appearing, so it fades in
            // already pointing at its seat instead of sweeping in from whatever
            // seat was last hovered.
            m_wedgeAngle = target;
        }
        m_wedgeAngleFrom = m_wedgeAngle;
        m_wedgeAngleTo = m_wedgeAngleFrom + shortestAngleDelta(m_wedgeAngleFrom, target);
        m_wedgeOpacityTo = 1.0;
    } else {
        // Fading out: hold the angle, or it would spin while disappearing.
        m_wedgeAngleFrom = m_wedgeAngle;
        m_wedgeAngleTo = m_wedgeAngle;
        m_wedgeOpacityTo = 0.0;
    }

    m_hoverAnim->start();
}

void RadialMenuWidget::updateHoverFromGlobal(const QPoint& globalPos)
{
    const QPointF local = mapFromGlobal(globalPos);
    applyHoverState(slotAtPosition(local), m_page.canGoBack && isInHub(local));
}

void RadialMenuWidget::activateSlot(int index)
{
    if (index < 0 || index >= m_page.items.size()) {
        return;
    }
    const Slot& slot = m_page.items.at(index);
    if (slot.empty || !slot.enabled) {
        return;
    }

    // A seat that opens a page keeps the menu up: the controller swaps the page
    // under us and the widget stays where it is.
    if (!slot.opensPage) {
        m_activated = true;
        hideMenu();
    }
    emit slotTriggered(index);
}

bool RadialMenuWidget::handleShortcutKey(int key)
{
    // Same rule as the pointer: nothing is pickable mid-swap.
    if (m_transitionActive || key < Qt::Key_1 || key > Qt::Key_9) {
        return false;
    }
    const int index = key - Qt::Key_1;
    if (index >= m_page.items.size()) {
        return false;
    }
    activateSlot(index);
    return true;
}

void RadialMenuWidget::mouseMoveEvent(QMouseEvent* event)
{
    // Closed or closing mid-gesture (a middle-drag pan dismissed us): Qt still
    // delivers the drag through the implicit grab, so hand it to the canvas.
    if (!isVisible() || m_isHiding) {
        event->ignore();
        return;
    }

    const QPointF pos = event->position();
    applyHoverState(slotAtPosition(pos), m_page.canGoBack && isInHub(pos));
    event->accept();
}

void RadialMenuWidget::mousePressEvent(QMouseEvent* event)
{
    if (m_isHiding) {
        event->ignore();
        return;
    }

    // Mid-swap the press has nothing to land on, and letting it fall through to
    // the "clicked outside" branch below would dismiss the menu the user is
    // waiting on.
    if (m_transitionActive) {
        event->accept();
        return;
    }

    const QPointF pos = event->position();

    // Navigation gestures win over the menu: dismiss it and let the press fall
    // through to the canvas so the pan starts on this very press instead of
    // costing the user a second click.
    if (event->button() == Qt::MiddleButton) {
        dismiss();
        event->ignore();
        return;
    }

    if (isInHub(pos)) {
        if (m_page.canGoBack) {
            emit backRequested();
        } else {
            dismiss();
        }
        event->accept();
        return;
    }

    const int index = slotAtPosition(pos);
    if (index >= 0) {
        activateSlot(index);
    } else {
        dismiss();
    }
    event->accept();
}

void RadialMenuWidget::wheelEvent(QWheelEvent* event)
{
    // Zooming is navigation, not menu input: fade the menu out and hand the
    // wheel to the canvas by leaving the event unaccepted (it walks up the
    // parent chain to CanvasPanel::wheelEvent).
    dismiss();
    event->ignore();
}

void RadialMenuWidget::leaveEvent(QEvent* event)
{
    applyHoverState(-1, false);
    QWidget::leaveEvent(event);
}

void RadialMenuWidget::showEvent(QShowEvent* event)
{
    QWidget::showEvent(event);
    if (qApp) {
        qApp->installEventFilter(this);
    }
}

void RadialMenuWidget::hideEvent(QHideEvent* event)
{
    QWidget::hideEvent(event);
    if (qApp) {
        qApp->removeEventFilter(this);
    }
    // The regions vanish with the widget; without a frame the canvas would keep
    // the last frosted patches painted where the menu used to be.
    if (m_backdropSource) {
        m_backdropSource->requestBackdropUpdate();
    }
}

bool RadialMenuWidget::eventFilter(QObject* watched, QEvent* event)
{
    if (!isVisible() || m_isHiding || !event) {
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::TabletPress: {
        // Presses are also delivered to the QWindow before its widget. Acting
        // on that copy would dismiss the menu and then leave the widget-level
        // copy to fall through unfiltered, which is the wrong way round.
        QWidget* target = qobject_cast<QWidget*>(watched);
        if (!target) {
            break;
        }

        QPoint globalPos;
        if (event->type() == QEvent::TabletPress) {
            globalPos = static_cast<QTabletEvent*>(event)->globalPosition().toPoint();
        } else {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() == Qt::MiddleButton) {
                // Pan gesture. Over the menu our own handler runs; anywhere
                // else the canvas is already panning, so only close up shop —
                // without consuming, or the pan would never start.
                if (!containsMenuPoint(mapFromGlobal(mouseEvent->globalPosition().toPoint()))) {
                    dismiss();
                }
                break;
            }
            globalPos = mouseEvent->globalPosition().toPoint();
        }
        // Same window as well as same place: a floating panel parked over the
        // menu owns the presses that land on it.
        if (target->window() == window() && containsMenuPoint(mapFromGlobal(globalPos))) {
            break; // Our own press handling deals with it.
        }

        // Closing costs no click: the press keeps going to whatever it landed
        // on — a panel button, or the canvas, where it starts the stroke right
        // away — while the menu fades out behind it. hideMenu() has already
        // made the widget mouse-transparent, so the drag that follows never
        // catches on the fading menu.
        dismiss();
        break;
    }
    case QEvent::MouseMove:
    case QEvent::TabletMove: {
        // Hover is tracked here rather than in mouseMoveEvent alone: while the
        // opening button is held the canvas owns the implicit grab, and after
        // the release Qt may still route a move or two to it.
        const QPoint globalPos = (event->type() == QEvent::TabletMove)
            ? static_cast<QTabletEvent*>(event)->globalPosition().toPoint()
            : static_cast<QMouseEvent*>(event)->globalPosition().toPoint();
        updateHoverFromGlobal(globalPos);
        break;
    }
    case QEvent::MouseButtonRelease:
    case QEvent::TabletRelease: {
        if (!m_releaseSelectArmed) {
            break;
        }
        QPoint globalPos;
        if (event->type() == QEvent::TabletRelease) {
            globalPos = static_cast<QTabletEvent*>(event)->globalPosition().toPoint();
        } else {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (mouseEvent->button() != Qt::RightButton) {
                break;
            }
            globalPos = mouseEvent->globalPosition().toPoint();
        }

        // The gesture ends here either way. Releasing on a seat picks it;
        // releasing without having left the hub leaves the menu open so it can
        // also be used as an ordinary click menu.
        //
        // The event is deliberately NOT consumed: swallowing a release skips
        // Qt's own end-of-press bookkeeping, which left the implicit grab on
        // the canvas and killed plain hover afterwards. The canvas itself does
        // nothing with a right-button release.
        m_releaseSelectArmed = false;
        updateHoverFromGlobal(globalPos);
        // Released while the menu was still coming up: the gesture becomes a
        // plain open, and the menu stays put as an ordinary click menu. The
        // test is on elapsed time rather than on m_showProgress, which is
        // already eased and passes 0.6 a quarter of the way in.
        const bool stillOpening = !m_isHiding
            && m_progressAnim->state() == QAbstractAnimation::Running
            && m_progressAnim->currentTime() < kMinSelectProgress * m_progressAnim->duration();
        if (!stillOpening && m_hoveredSlot >= 0) {
            activateSlot(m_hoveredSlot);
        }
        break;
    }
    case QEvent::Wheel:
        // Reaches us even when the pointer is over the canvas: zooming fades
        // the menu out, and the wheel itself is left for the canvas to handle.
        dismiss();
        return false;
    case QEvent::KeyPress: {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            dismiss();
            return true;
        }
        if (keyEvent->key() == Qt::Key_Space) {
            // Space starts the canvas pan-hold: get out of the way, and let the
            // key itself through so the drag that follows actually pans.
            dismiss();
            break;
        }
        if (handleShortcutKey(keyEvent->key())) {
            return true;
        }
        break;
    }
    case QEvent::ApplicationDeactivate:
        hideImmediate();
        break;
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

// ======================================================================================
//   P A I N T I N G
// ======================================================================================

void RadialMenuWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    if (m_page.items.isEmpty()) {
        return;
    }

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Three layers, each on its own clock: the page on its way out, the page
    // coming in, and the single piece that travels between them.
    painter.setOpacity(m_showProgress);
    paintGhosts(painter);

    const qreal ringScale = currentRingScale();
    painter.setOpacity(entranceAmount());
    paintHoverWedge(painter, ringScale);
    paintHub(painter, ringScale);
    paintSlots(painter, ringScale);
    paintLabels(painter, ringScale);
    paintTitle(painter);

    painter.setOpacity(m_showProgress);
    paintMorph(painter);
}

void RadialMenuWidget::paintGlassShape(
    QPainter& painter, const QRectF& rect, qreal radius, qreal accent) const
{
    namespace painting = ruwa::ui::painting;

    const auto& colors = ThemeManager::instance().colors();
    const qreal hover = qBound<qreal>(0.0, accent, 1.0);
    const qreal glassRadius = painting::glassSilhouetteRadius(radius);
    QPainterPath glass;
    glass.addRoundedRect(painting::glassSilhouetteRect(rect), glassRadius, glassRadius);

    painter.save();
    painter.setPen(Qt::NoPen);

    QColor tint = colors.surface;
    tint.setAlpha(painting::kBackdropTintAlpha);
    // The GPU pass has already frosted and refracted the canvas under this
    // rect; without it there is nothing behind the widget, so fall back to a
    // near-opaque surface.
    if (!painting::drawBackdropBlurTint(
            painter, const_cast<RadialMenuWidget*>(this), m_backdropSource, glass, tint)) {
        QColor fallback = colors.surface;
        fallback.setAlpha(colors.isDark ? 232 : 242);
        painter.setBrush(fallback);
        painter.drawPath(glass);
    }

    if (hover > 0.001) {
        QColor wash = colors.primary;
        wash.setAlpha(qRound(kAccentWashAlpha * hover));
        painter.setBrush(wash);
        painter.drawPath(glass);
    }
    painter.restore();

    QColor restBorder = colors.border;
    restBorder.setAlphaF(restBorder.alphaF() * 0.5);
    QColor hoverBorder = colors.primary;
    hoverBorder.setAlphaF(hoverBorder.alphaF() * 0.9);
    const QColor border = lerpColor(restBorder, hoverBorder, hover);
    painting::drawGradientBorder(painter, rect, radius, border, border);

    // Specular sweep only. No inner shadow and no drop shadow: the refracting
    // bevel is what separates these pieces from the canvas.
    const QRectF rimRect = rect.adjusted(0.5, 0.5, -0.5, -0.5);
    const qreal rimRadius = qMax<qreal>(0.0, radius - 0.5);
    QPainterPath rim;
    rim.addRoundedRect(rimRect, rimRadius, rimRadius);
    painting::drawLiquidGlassRim(painter, rim, rimRect, colors.primary);
}

QVector<RadialMenuWidget::BackdropShape> RadialMenuWidget::backdropShapes() const
{
    QVector<BackdropShape> shapes;
    if (!isVisible() || m_page.items.isEmpty() || m_showProgress <= 0.001) {
        return shapes;
    }

    const qreal ringScale = currentRingScale();
    const QPointF origin = center();
    shapes.reserve(m_page.items.size() * 2 + m_ghosts.size() + 3);

    // Pieces of the page on its way out are still glass while they fly. The
    // frost has to fade with the chrome painted over it, or a half-faded button
    // sits on a full-strength patch of blur.
    if (m_transitionActive) {
        const qreal travel = transitionProgress();
        const qreal ghostFade = 1.0 - qBound<qreal>(0.0, m_transitionRaw / kGhostFadeEnd, 1.0);
        if (ghostFade > 0.001) {
            for (const Ghost& ghost : m_ghosts) {
                shapes.append({ ghost.rect.translated(origin + ghost.push * travel), ghost.radius,
                    ghostFade });
            }
        }
        const QRectF morph(
            origin + (m_morphFrom.topLeft() * (1.0 - travel) + m_morphTo.topLeft() * travel),
            QSizeF(m_morphFrom.width() * (1.0 - travel) + m_morphTo.width() * travel,
                m_morphFrom.height() * (1.0 - travel) + m_morphTo.height() * travel));
        shapes.append(
            { morph, m_morphFromRadius * (1.0 - travel) + m_morphToRadius * travel, 1.0 });
    }

    const qreal entering = transitionEntrance();
    if (entering > 0.001) {
        if (!m_transitionActive || !m_transitionForward) {
            const QRectF hubRect(origin.x() - m_metrics.hubRadius, origin.y() - m_metrics.hubRadius,
                m_metrics.hubRadius * 2.0, m_metrics.hubRadius * 2.0);
            shapes.append({ hubRect, m_metrics.hubRadius, entering });
        }

        for (int index = 0; index < m_page.items.size(); ++index) {
            if (m_page.items.at(index).empty || index == m_transitionPivot) {
                continue;
            }
            shapes.append({ slotRect(index, ringScale), m_metrics.slotCorner, entering });
            const QRectF pill = labelRect(index, ringScale);
            if (!pill.isEmpty()) {
                shapes.append({ pill, m_metrics.pillRadius, entering });
            }
        }

        const QRectF title = titlePillRect();
        if (!title.isEmpty()) {
            shapes.append({ title, m_metrics.titlePillHeight / 2.0, entering });
        }
    }
    return shapes;
}

void RadialMenuWidget::setBackdropSource(ruwa::shared::rendering::ICanvasBackdropSource* source)
{
    if (m_backdropSource == source) {
        return;
    }
    m_backdropSource = source;
    update();
}

void RadialMenuWidget::paintGhosts(QPainter& painter) const
{
    if (!m_transitionActive || m_ghosts.isEmpty()) {
        return;
    }

    const qreal travel = transitionProgress();
    const qreal fade = 1.0 - qBound<qreal>(0.0, m_transitionRaw / kGhostFadeEnd, 1.0);
    if (fade <= 0.001) {
        return;
    }

    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    const QPointF origin = center();
    const qreal baseOpacity = painter.opacity();

    painter.save();
    painter.setOpacity(baseOpacity * fade);
    painter.setFont(colors.fonts.getUIFont(theme.scaledFontSize(10)));

    for (const Ghost& ghost : m_ghosts) {
        const QRectF rect = ghost.rect.translated(origin + ghost.push * travel);
        paintGlassShape(painter, rect, ghost.radius);

        const QColor textColor = ghost.enabled ? colors.text : colors.textDisabled();
        if (ghost.label) {
            paintLabelContent(painter, rect, ghost.title, ghost.shortcut, textColor);
            continue;
        }
        if (!ghost.icon.isNull()) {
            const int iconSize = qMax(1, qRound(rect.width() * 0.58));
            QPixmap pixmap = ghost.icon.pixmap(QSize(iconSize, iconSize), devicePixelRatioF());
            pixmap = ruwa::ui::painting::tintedPixmap(pixmap, textColor);
            painter.drawPixmap(QRectF(rect.center() - QPointF(iconSize / 2.0, iconSize / 2.0),
                                   QSizeF(iconSize, iconSize))
                                   .toRect(),
                pixmap);
        }
    }
    painter.restore();
}

void RadialMenuWidget::paintMorph(QPainter& painter) const
{
    if (!m_transitionActive) {
        return;
    }

    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    const qreal t = transitionProgress();
    const QPointF origin = center();

    // One piece changing shape, not two pieces cross-fading: the square rolls
    // its corners out to a full circle as it grows into the hub.
    const QRectF rect(origin + (m_morphFrom.topLeft() * (1.0 - t) + m_morphTo.topLeft() * t),
        QSizeF(m_morphFrom.width() * (1.0 - t) + m_morphTo.width() * t,
            m_morphFrom.height() * (1.0 - t) + m_morphTo.height() * t));
    const qreal radius = m_morphFromRadius * (1.0 - t) + m_morphToRadius * t;

    const qreal baseOpacity = painter.opacity();
    paintGlassShape(painter, rect, radius);

    painter.save();
    if (!m_morphIcon.isNull()) {
        // Going in, the seat's own icon dissolves as it becomes the hub; coming
        // back out, the same icon reappears on the seat it returns to.
        const qreal raw = qBound<qreal>(0.0, m_transitionRaw / kMorphIconFade, 1.0);
        const qreal iconAlpha = m_transitionForward ? (1.0 - raw) : raw;
        if (iconAlpha > 0.001) {
            const int iconSize = qMax(1, qRound(rect.width() * 0.42));
            QPixmap pixmap = m_morphIcon.pixmap(QSize(iconSize, iconSize), devicePixelRatioF());
            pixmap = ruwa::ui::painting::tintedPixmap(pixmap, colors.text);
            painter.setOpacity(baseOpacity * iconAlpha);
            painter.drawPixmap(QRectF(rect.center() - QPointF(iconSize / 2.0, iconSize / 2.0),
                                   QSizeF(iconSize, iconSize))
                                   .toRect(),
                pixmap);
        }
    }

    // The hub it is becoming is a back button, so the chevron arrives with it.
    if (m_transitionForward && m_page.canGoBack) {
        const qreal size = m_metrics.hubRadius * 0.28;
        const QPointF c = rect.center();
        QPen chevron(colors.textMuted);
        chevron.setWidthF(theme.scaled(2));
        chevron.setCapStyle(Qt::RoundCap);
        chevron.setJoinStyle(Qt::RoundJoin);
        painter.setOpacity(baseOpacity * t);
        painter.setPen(chevron);
        painter.setBrush(Qt::NoBrush);
        QPainterPath arrow;
        arrow.moveTo(c + QPointF(size * 0.5, -size));
        arrow.lineTo(c + QPointF(-size * 0.5, 0.0));
        arrow.lineTo(c + QPointF(size * 0.5, size));
        painter.drawPath(arrow);
    }
    painter.restore();
}

void RadialMenuWidget::paintHoverWedge(QPainter& painter, qreal ringScale) const
{
    const int count = m_page.items.size();
    if (count <= 0 || m_wedgeOpacity <= 0.001) {
        return;
    }

    const auto& colors = ThemeManager::instance().colors();
    const qreal spanDeg = 360.0 / count;

    // Qt angles run counter-clockwise from 3 o'clock; our seats run clockwise
    // from 12 o'clock, hence the negation and the quarter turn.
    const qreal centerDeg = 90.0 - qRadiansToDegrees(m_wedgeAngle);
    const qreal startDeg = centerDeg - spanDeg / 2.0;

    // The wedge stops at the ring itself, so its outer end is centred on the
    // seat's button: the button straddles the tip of the highlight.
    const QPainterPath path
        = wedgePath(center(), m_metrics.hubRadius + ThemeManager::instance().scaled(2),
            m_metrics.ringRadius * ringScale, startDeg, spanDeg);

    QRadialGradient gradient(center(), m_metrics.ringRadius + m_metrics.slotRadius);
    gradient.setColorAt(0.0,
        ThemeColors::withAlpha(colors.primary, qRound((colors.isDark ? 26 : 34) * m_wedgeOpacity)));
    gradient.setColorAt(1.0,
        ThemeColors::withAlpha(colors.primary, qRound((colors.isDark ? 62 : 78) * m_wedgeOpacity)));

    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(gradient);
    painter.drawPath(path);
    painter.restore();
}

void RadialMenuWidget::paintHub(QPainter& painter, qreal ringScale) const
{
    Q_UNUSED(ringScale);

    // Entering a group, the hub is not drawn at all: the seat that was picked
    // is on its way to becoming it, and paintMorph() has that piece.
    if (m_transitionActive && m_transitionForward) {
        return;
    }

    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    const QRectF hubRect(center().x() - m_metrics.hubRadius, center().y() - m_metrics.hubRadius,
        m_metrics.hubRadius * 2.0, m_metrics.hubRadius * 2.0);

    paintGlassShape(painter, hubRect, m_metrics.hubRadius, m_hubHover);

    painter.save();
    if (m_page.canGoBack) {
        // Back chevron, pointing left, drawn from the hub centre.
        const qreal size = m_metrics.hubRadius * 0.28;
        const QPointF c = center();
        QPen chevron(lerpColor(colors.textMuted, colors.textOnPrimary(), m_hubHover));
        chevron.setWidthF(theme.scaled(2));
        chevron.setCapStyle(Qt::RoundCap);
        chevron.setJoinStyle(Qt::RoundJoin);
        painter.setPen(chevron);
        painter.setBrush(Qt::NoBrush);
        QPainterPath arrow;
        arrow.moveTo(c + QPointF(size * 0.5, -size));
        arrow.lineTo(c + QPointF(-size * 0.5, 0.0));
        arrow.lineTo(c + QPointF(size * 0.5, size));
        painter.drawPath(arrow);
    }
    painter.restore();
}

void RadialMenuWidget::paintSlots(QPainter& painter, qreal ringScale) const
{
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    const qreal radius = m_metrics.slotRadius;
    const qreal corner = m_metrics.slotCorner;

    painter.save();
    for (int index = 0; index < m_page.items.size(); ++index) {
        const Slot& slot = m_page.items.at(index);
        // Coming back out of a group, the seat the hub folds into is drawn by
        // paintMorph() until it lands.
        if (m_transitionActive && index == m_transitionPivot) {
            continue;
        }

        const QPointF slotPos = slotCenter(index, ringScale);
        const QRectF discRect = slotRect(index, ringScale);
        const qreal hover = hoverAmount(index);

        if (slot.empty) {
            // An empty seat still shows where it is, so the ring reads as a
            // grid the user can drop something into.
            QPen pen(ThemeColors::withAlpha(colors.border, 140));
            pen.setWidthF(theme.scaled(1));
            pen.setStyle(Qt::DashLine);
            painter.setPen(pen);
            painter.setBrush(Qt::NoBrush);
            painter.drawRoundedRect(discRect, corner, corner);
            continue;
        }

        paintGlassShape(painter, discRect, corner, hover);

        const QColor contentColor = !slot.enabled
            ? colors.textDisabled()
            : lerpColor(colors.text, colors.textOnPrimary(), hover);

        if (!slot.icon.isNull()) {
            // Same icon-to-button ratio as the tools panel (20px in 36px), and
            // the icon grows with the button it sits in.
            const int iconSize = qMax(1, qRound(radius * 1.16 * (1.0 + kHoverScale * hover)));
            QPixmap pixmap = slot.icon.pixmap(QSize(iconSize, iconSize), devicePixelRatioF());
            const QColor tint = contentColor;
            pixmap = ruwa::ui::painting::tintedPixmap(pixmap, tint);
            const QRectF target(
                slotPos.x() - iconSize / 2.0, slotPos.y() - iconSize / 2.0, iconSize, iconSize);
            painter.drawPixmap(target.toRect(), pixmap);
            continue;
        }

        // No icon: fall back to the first letter, which still tells the seats apart.
        QFont font = colors.fonts.getUIFont(theme.scaledFontSize(11));
        font.setWeight(QFont::DemiBold);
        painter.setFont(font);
        painter.setPen(contentColor);
        painter.drawText(discRect, Qt::AlignCenter, slot.title.left(1).toUpper());
    }
    painter.restore();
}

void RadialMenuWidget::paintLabels(QPainter& painter, qreal ringScale) const
{
    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();
    const QFont labelFont = colors.fonts.getUIFont(theme.scaledFontSize(10));

    painter.save();
    painter.setFont(labelFont);
    for (int index = 0; index < m_page.items.size(); ++index) {
        const Slot& slot = m_page.items.at(index);
        const QRectF pill = labelRect(index, ringScale);
        if (slot.empty || pill.isEmpty()) {
            continue;
        }

        // Labels deliberately do not light up with their seat: highlighting a
        // caption as well as the button reads as noise. They only move with it.
        const QColor textColor = slot.enabled ? colors.text : colors.textDisabled();

        paintGlassShape(painter, pill, m_metrics.pillRadius);
        paintLabelContent(painter, pill, slot.title, slot.shortcut, textColor);
    }
    painter.restore();
}

void RadialMenuWidget::paintLabelContent(QPainter& painter, const QRectF& pill,
    const QString& title, const QKeySequence& shortcut, const QColor& textColor) const
{
    const auto& theme = ThemeManager::instance();
    const qreal padding = theme.scaled(12);
    QRectF textRect = pill.adjusted(padding, 0.0, -padding, 0.0);

    if (ShortcutKeycapRenderer::isRenderable(shortcut)) {
        const QSizeF keycapSize = ShortcutKeycapRenderer::contentSize(
            shortcut, ShortcutKeycapRenderer::SizeVariant::Compact);
        const QRectF keycapRect(
            textRect.right() - keycapSize.width(), pill.top(), keycapSize.width(), pill.height());
        ShortcutKeycapRenderer::paint(painter, keycapRect, shortcut,
            Qt::AlignRight | Qt::AlignVCenter, ShortcutKeycapRenderer::SizeVariant::Compact,
            textColor, false);
        textRect.setRight(keycapRect.left() - theme.scaled(6));
    }

    painter.setPen(textColor);
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, title);
}

void RadialMenuWidget::paintTitle(QPainter& painter) const
{
    const QRectF pill = titlePillRect();
    if (pill.isEmpty()) {
        return;
    }

    const auto& theme = ThemeManager::instance();
    const auto& colors = theme.colors();

    QFont font = colors.fonts.getUIFont(theme.scaledFontSize(9));
    font.setWeight(QFont::DemiBold);

    paintGlassShape(painter, pill, m_metrics.titlePillHeight / 2.0);

    painter.save();
    painter.setFont(font);
    painter.setPen(colors.textMuted);
    painter.drawText(pill, Qt::AlignCenter, m_page.title);
    painter.restore();
}

} // namespace ruwa::ui::widgets
