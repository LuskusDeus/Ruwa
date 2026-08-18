// SPDX-License-Identifier: MPL-2.0

#include "shared/widgets/inputs/AnchorGridSelector.h"

#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/style/WidgetStyleManager.h"

#include <QMouseEvent>
#include <QPainter>
#include <QResizeEvent>
#include <QVariantAnimation>

namespace ruwa::ui::widgets {

using ruwa::ui::core::ThemeColors;
using ruwa::ui::core::ThemeManager;
using ruwa::ui::core::WidgetStyleManager;

namespace {
// No plate behind the grid, so the cells run to the widget edge and the gap is
// the only thing separating them.
constexpr int kBasePadding = 0;
constexpr int kBaseGap = 4;
constexpr int kBaseCornerRadius = 8;
// The selection highlight is a separate shape inset from the cell edges —
// "inscribed" in the cell rather than filling it — so it never touches its
// neighbours and needs no corner-matching trick.
constexpr int kSelectionInsetBase = 4;
constexpr int kSelectionCornerRadiusBase = 6;
// Grow/shrink range for the highlight's appear/disappear animation.
constexpr qreal kSelectionMinScale = 0.8;
constexpr int kSelectionAnimationMs = 190;
constexpr int kHoverAnimationMs = 150;
} // namespace

AnchorGridSelector::AnchorGridSelector(QWidget* parent)
    : QWidget(parent)
{
    setCursor(Qt::PointingHandCursor);
    setFocusPolicy(Qt::NoFocus);
    setMouseTracking(true);
    setAttribute(Qt::WA_TranslucentBackground);
    updateScaledSize();

    for (int index = 0; index < 9; ++index) {
        auto* hoverAnimation = new QVariantAnimation(this);
        hoverAnimation->setDuration(kHoverAnimationMs);
        hoverAnimation->setEasingCurve(QEasingCurve::OutCubic);
        connect(hoverAnimation, &QVariantAnimation::valueChanged, this,
            [this, index](const QVariant& value) {
                m_cellHover[index] = value.toReal();
                update();
            });
        m_cellHoverAnimations[index] = hoverAnimation;

        auto* selectionAnimation = new QVariantAnimation(this);
        selectionAnimation->setDuration(kSelectionAnimationMs);
        selectionAnimation->setEasingCurve(QEasingCurve::OutCubic);
        connect(selectionAnimation, &QVariantAnimation::valueChanged, this,
            [this, index](const QVariant& value) {
                m_cellSelection[index] = value.toReal();
                update();
            });
        m_cellSelectionAnimations[index] = selectionAnimation;
    }

    connect(&ThemeManager::instance(), &ThemeManager::themeChanged, this,
        &AnchorGridSelector::onThemeChanged);
}

// ============================================================================
//   S T A T E
// ============================================================================

Qt::Alignment AnchorGridSelector::alignmentForIndex(int index)
{
    const int clamped = qBound(0, index, 8);
    const Qt::Alignment vertical[3] = { Qt::AlignTop, Qt::AlignVCenter, Qt::AlignBottom };
    const Qt::Alignment horizontal[3] = { Qt::AlignLeft, Qt::AlignHCenter, Qt::AlignRight };
    return vertical[clamped / 3] | horizontal[clamped % 3];
}

int AnchorGridSelector::indexForAlignment(Qt::Alignment alignment)
{
    int row = 1;
    if (alignment & Qt::AlignTop) {
        row = 0;
    } else if (alignment & Qt::AlignBottom) {
        row = 2;
    }

    int column = 1;
    if (alignment & Qt::AlignLeft) {
        column = 0;
    } else if (alignment & Qt::AlignRight) {
        column = 2;
    }

    return row * 3 + column;
}

Qt::Alignment AnchorGridSelector::anchor() const
{
    if (m_currentIndex < 0) {
        return {};
    }
    return alignmentForIndex(m_currentIndex);
}

void AnchorGridSelector::setAnchor(Qt::Alignment alignment, bool animated)
{
    setCurrentIndex(indexForAlignment(alignment), animated);
}

void AnchorGridSelector::setCurrentIndex(int index, bool animated)
{
    // Anything off the grid collapses to "no cell", so a caller can clear the
    // highlight without a second method.
    const int normalized = (index >= 0 && index <= 8) ? index : -1;
    if (m_currentIndex == normalized) {
        return;
    }

    m_currentIndex = normalized;
    retargetSelectionAnimations(normalized, animated);

    update();
    emit anchorChanged(m_currentIndex);
}

void AnchorGridSelector::retargetSelectionAnimations(int index, bool animated)
{
    for (int cell = 0; cell < 9; ++cell) {
        const qreal target = (cell == index) ? 1.0 : 0.0;
        auto* animation = m_cellSelectionAnimations[cell];

        if (!animated) {
            animation->stop();
            m_cellSelection[cell] = target;
            continue;
        }

        const bool running = animation->state() == QAbstractAnimation::Running;
        // The +1.0 form keeps the comparison meaningful when target is zero,
        // which qFuzzyCompare alone does not handle.
        if (running && qFuzzyCompare(animation->endValue().toReal() + 1.0, target + 1.0)) {
            continue; // already heading there; restarting would reset the easing
        }
        if (!running && qFuzzyCompare(m_cellSelection[cell] + 1.0, target + 1.0)) {
            continue;
        }

        // Retargeted from wherever it currently sits — never from 0 — so a cell
        // interrupted mid-fade (a rapid second change before the first
        // finished) keeps going from its real state instead of popping.
        animation->stop();
        animation->setStartValue(m_cellSelection[cell]);
        animation->setEndValue(target);
        animation->start();
    }
}

void AnchorGridSelector::setBaseSize(int size)
{
    const int clamped = qMax(24, size);
    if (m_baseSize == clamped) {
        return;
    }
    m_baseSize = clamped;
    updateScaledSize();
}

void AnchorGridSelector::setSideLength(int pixels)
{
    const int clamped = qMax(0, pixels);
    if (m_explicitSide == clamped) {
        return;
    }
    m_explicitSide = clamped;
    updateScaledSize();
}

void AnchorGridSelector::updateScaledSize()
{
    // An explicit side is already in final pixels — it came from measuring
    // scaled widgets, so scaling it again would double-apply the theme factor.
    const int side
        = m_explicitSide > 0 ? m_explicitSide : ThemeManager::instance().scaled(m_baseSize);
    setFixedSize(side, side);
    update();
}

void AnchorGridSelector::onThemeChanged()
{
    updateScaledSize();
}

QSize AnchorGridSelector::sizeHint() const
{
    const int side
        = m_explicitSide > 0 ? m_explicitSide : ThemeManager::instance().scaled(m_baseSize);
    return QSize(side, side);
}

// ============================================================================
//   G E O M E T R Y
// ============================================================================

qreal AnchorGridSelector::cellSpan() const
{
    auto& tm = ThemeManager::instance();
    const qreal padding = tm.scaled(kBasePadding);
    const qreal gap = tm.scaled(kBaseGap);
    return qMax<qreal>(1.0, (width() - 2.0 * padding - 2.0 * gap) / 3.0);
}

QRectF AnchorGridSelector::cellRect(int index) const
{
    if (index < 0 || index > 8) {
        return QRectF();
    }

    auto& tm = ThemeManager::instance();
    const qreal padding = tm.scaled(kBasePadding);
    const qreal gap = tm.scaled(kBaseGap);
    const qreal span = cellSpan();
    const int row = index / 3;
    const int column = index % 3;

    return QRectF(padding + column * (span + gap), padding + row * (span + gap), span, span);
}

QPainterPath AnchorGridSelector::outwardRoundedRect(const QRectF& rect, int index, qreal radius) const
{
    QPainterPath path;
    if (rect.isNull()) {
        return path;
    }

    const int row = index / 3;
    const int column = index % 3;
    const qreal r = qMin(radius, qMin(rect.width(), rect.height()) * 0.5);

    // Only the corner cells keep the one corner that faces the outside of the
    // grid; everything a neighbour touches stays square, so the nine shapes
    // read as one rounded square cut into a grid — and the same rule applies
    // to the inset selection highlight, so it never rounds a corner that has
    // no outer edge behind it to round away from.
    const bool roundTopLeft = (row == 0 && column == 0);
    const bool roundTopRight = (row == 0 && column == 2);
    const bool roundBottomRight = (row == 2 && column == 2);
    const bool roundBottomLeft = (row == 2 && column == 0);

    // Traced clockwise from the top-left corner. Built by hand rather than by
    // combining rects: a union of overlapping subpaths resolves odd-even, which
    // punches the overlap out instead of filling it.
    const qreal left = rect.left();
    const qreal top = rect.top();
    const qreal right = rect.right();
    const qreal bottom = rect.bottom();
    const qreal diameter = r * 2.0;

    path.moveTo(left + (roundTopLeft ? r : 0.0), top);
    path.lineTo(right - (roundTopRight ? r : 0.0), top);
    if (roundTopRight) {
        path.arcTo(QRectF(right - diameter, top, diameter, diameter), 90.0, -90.0);
    }
    path.lineTo(right, bottom - (roundBottomRight ? r : 0.0));
    if (roundBottomRight) {
        path.arcTo(QRectF(right - diameter, bottom - diameter, diameter, diameter), 0.0, -90.0);
    }
    path.lineTo(left + (roundBottomLeft ? r : 0.0), bottom);
    if (roundBottomLeft) {
        path.arcTo(QRectF(left, bottom - diameter, diameter, diameter), 270.0, -90.0);
    }
    path.lineTo(left, top + (roundTopLeft ? r : 0.0));
    if (roundTopLeft) {
        path.arcTo(QRectF(left, top, diameter, diameter), 180.0, -90.0);
    }
    path.closeSubpath();

    return path;
}

QPainterPath AnchorGridSelector::cellPath(int index) const
{
    return outwardRoundedRect(
        cellRect(index), index, ThemeManager::instance().scaled(kBaseCornerRadius));
}

QRectF AnchorGridSelector::selectionOverlayRect(int index, qreal amount) const
{
    const QRectF cell = cellRect(index);
    if (cell.isNull()) {
        return QRectF();
    }

    const qreal inset = ThemeManager::instance().scaled(kSelectionInsetBase);
    const QRectF inscribed = cell.adjusted(inset, inset, -inset, -inset);
    if (inscribed.width() <= 0.0 || inscribed.height() <= 0.0) {
        return QRectF();
    }

    // The inscribed rect is the 100% state; growing from 80% reads as the
    // highlight settling into its inset spot rather than the cell itself
    // resizing, since the cell's own fill never moves.
    const qreal scale = kSelectionMinScale + (1.0 - kSelectionMinScale) * qBound(0.0, amount, 1.0);
    QRectF overlay(QPointF(), inscribed.size() * scale);
    overlay.moveCenter(inscribed.center());
    return overlay;
}

int AnchorGridSelector::indexAtPosition(const QPointF& pos) const
{
    // Grow each cell into half the gap around it so the grid has no dead
    // pixels between the squares — a near-miss still picks the nearest cell.
    const qreal slack = ThemeManager::instance().scaled(kBaseGap) * 0.5;
    for (int index = 0; index < 9; ++index) {
        if (cellRect(index).adjusted(-slack, -slack, slack, slack).contains(pos)) {
            return index;
        }
    }
    return -1;
}

void AnchorGridSelector::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    update();
}

// ============================================================================
//   I N P U T
// ============================================================================

void AnchorGridSelector::setHoveredIndex(int index)
{
    if (m_hoveredIndex == index) {
        return;
    }

    m_hoveredIndex = index;

    // Every cell is retargeted from wherever it currently sits, so a cell that
    // is still lit on the way out keeps falling from that value instead of
    // being snapped, and one re-entered mid-fade resumes from where it was.
    for (int cell = 0; cell < 9; ++cell) {
        const qreal target = (cell == index) ? 1.0 : 0.0;
        auto* animation = m_cellHoverAnimations[cell];
        const bool running = animation->state() == QAbstractAnimation::Running;
        // The +1.0 form keeps the comparison meaningful when target is zero,
        // which qFuzzyCompare alone does not handle.
        if (running && qFuzzyCompare(animation->endValue().toReal() + 1.0, target + 1.0)) {
            continue; // already heading there; restarting would reset the easing
        }
        if (!running && qFuzzyCompare(m_cellHover[cell] + 1.0, target + 1.0)) {
            continue;
        }

        animation->stop();
        animation->setStartValue(m_cellHover[cell]);
        animation->setEndValue(target);
        animation->start();
    }
    update();
}

void AnchorGridSelector::mouseMoveEvent(QMouseEvent* event)
{
    setHoveredIndex(indexAtPosition(event->position()));
    QWidget::mouseMoveEvent(event);
}

void AnchorGridSelector::leaveEvent(QEvent* event)
{
    setHoveredIndex(-1);
    m_pressedIndex = -1;
    QWidget::leaveEvent(event);
}

void AnchorGridSelector::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    // Tracked only so a release that wandered off the pressed cell is ignored;
    // the press itself has no visual, the hover fade carries the feedback.
    m_pressedIndex = indexAtPosition(event->position());
    event->accept();
}

void AnchorGridSelector::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mouseReleaseEvent(event);
        return;
    }

    const int pressed = m_pressedIndex;
    m_pressedIndex = -1;

    const int released = indexAtPosition(event->position());
    if (pressed >= 0 && released == pressed) {
        // Deliberately does not move the highlight: it shows where the content
        // actually is, and only the owner learns that once the click has been
        // acted on.
        emit anchorClicked(pressed);
    }
    event->accept();
}

// ============================================================================
//   P A I N T I N G
// ============================================================================

void AnchorGridSelector::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& colors = WidgetStyleManager::instance().colors();
    const bool enabled = isEnabled();

    // No plate and no outlines: a cell is nothing but its fill, so the grid is
    // read from the gaps between the nine shapes.
    //
    // Resting and hover are the same colour at two alphas rather than two
    // different colours. Interpolating a low-alpha white towards an opaque
    // surface tone travels through a much brighter blend and lands where it
    // started once composited — a flash, not a hover.
    const QColor restingFill = ThemeColors::withAlpha(colors.text, colors.isDark ? 20 : 16);
    const QColor hoverFill = ThemeColors::withAlpha(colors.text, colors.isDark ? 58 : 46);
    const QColor selectedFill
        = ThemeColors::adjustBrightness(colors.primary, colors.isDark ? 0.92 : 1.08);

    painter.setPen(Qt::NoPen);

    // Base fill: hover only. Selection is a separate overlay drawn afterwards,
    // so its own scale/alpha animation is not fighting the cell's fill colour.
    for (int index = 0; index < 9; ++index) {
        const qreal hovered = enabled ? m_cellHover[index] : 0.0;
        painter.setBrush(ThemeColors::interpolate(restingFill, hoverFill, hovered));
        painter.drawPath(cellPath(index));
    }

    // Selection highlight: inset, scale 80%→100%, alpha 0→100% on the way in;
    // the reverse on the way out. Each cell owns its own weight (m_cellSelection),
    // so any number of cells can be mid-fade at once — the outgoing cell keeps
    // shrinking away exactly while the incoming one grows in, and a rapid
    // second change does not cut the first one short.
    const qreal selectionAlphaScale = enabled ? 1.0 : 0.45;
    const qreal cornerRadius = ThemeManager::instance().scaled(kSelectionCornerRadiusBase);

    for (int index = 0; index < 9; ++index) {
        const qreal amount = m_cellSelection[index];
        if (amount <= 0.0) {
            continue;
        }

        const QRectF overlay = selectionOverlayRect(index, amount);
        if (overlay.isNull()) {
            continue;
        }

        QColor fill = selectedFill;
        fill.setAlphaF(fill.alphaF() * amount * selectionAlphaScale);
        painter.setBrush(fill);
        painter.drawPath(outwardRoundedRect(overlay, index, cornerRadius));
    }
}

} // namespace ruwa::ui::widgets
