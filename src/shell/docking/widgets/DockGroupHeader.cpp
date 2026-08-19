// SPDX-License-Identifier: MPL-2.0

// DockGroupHeader.cpp
#include "DockGroupHeader.h"
#include "shared/style/AnimationPolicy.h"
#include "DockPanel.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/resources/IconProvider.h"

#include <QCursor>
#include <QEvent>
#include <QFontMetricsF>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QResizeEvent>
#include <QVariantAnimation>
#include <QWheelEvent>
#include <utility>

namespace anim = ruwa::ui::core::anim;

namespace ruwa::ui::docking {

namespace {

/// Hover pill and × reveal: the app tab strip's timings (CustomTabBar), which
/// this strip mirrors down to the easing — the × fades in slower than it goes.
constexpr int kHoverAnimationMs = 200;
constexpr int kCloseRevealInMs = 170;
constexpr int kCloseRevealOutMs = 150;
/// Softer than raw overlayHover, matching the app tab strip's hover pill.
constexpr qreal kHoverPillAlphaFactor = 0.42;

/// Matches DockPanel's border overlay: the strip and the member below it are
/// one outline around the whole cell, so they must stroke the same colour.
constexpr qreal kStripBorderWidth = 1.0;
constexpr qreal kStripOuterInset = 0.5;
constexpr int kBorderDarkerFactor = 133;

/// One wheel notch, in eighths of a degree (QWheelEvent::angleDelta units).
constexpr int kWheelNotch = 120;

/// Farewell of a closed tab, and the catch-up slide of the survivors — both the
/// timing and the drop distance of the app tab strip (CustomTabBar).
constexpr int kCloseFadeMs = 240;
constexpr int kLayoutSlideMs = 240;
constexpr int kTabReorderSlideMs = 140;
constexpr int kTabPopDistance = 10;

/// A tab whose close hands the stage to another member fades for exactly as
/// long as that content transition takes (DockGroupHost's member slide), so the
/// two land on the same frame and the survivors close the gap immediately after.
constexpr int kCloseFadeWithHandoverMs = 350;

/**
 * @brief The strip's outline: rounded top corners, open at the bottom
 *
 * Open on purpose — the member below draws its own top border, and the two
 * lines would otherwise sit on adjacent pixels as a doubled seam.
 */
QPainterPath buildStripOutline(const QRectF& rect, qreal radius)
{
    const qreal r = qBound<qreal>(0.0, radius, qMin(rect.width() / 2.0, rect.height()));

    QPainterPath path;
    path.moveTo(rect.left(), rect.bottom());
    path.lineTo(rect.left(), rect.top() + r);
    if (r > 0.0) {
        path.quadTo(rect.left(), rect.top(), rect.left() + r, rect.top());
    } else {
        path.lineTo(rect.left(), rect.top());
    }
    path.lineTo(rect.right() - r, rect.top());
    if (r > 0.0) {
        path.quadTo(rect.right(), rect.top(), rect.right(), rect.top() + r);
    } else {
        path.lineTo(rect.right(), rect.top());
    }
    path.lineTo(rect.right(), rect.bottom());
    return path;
}

} // namespace

// ============================================================================
// Construction
// ============================================================================

DockGroupHeader::DockGroupHeader(QWidget* parent)
    : QWidget(parent)
{
    setMouseTracking(true);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
    updateScaledSizes();
}

DockGroupHeader::~DockGroupHeader()
{
    for (TabItem& item : m_items) {
        destroyItemAnimations(item);
    }
}

void DockGroupHeader::destroyItemAnimations(TabItem& item)
{
    // deleteLater, never delete: the tab's farewell reports the close from its
    // own finished handler, and the layout surgery that follows runs all the
    // way back into setPanels() — i.e. into here — while that signal is still
    // being emitted. Deleting the sender outright there is a crash.
    for (QVariantAnimation** slot : { &item.hoverAnim, &item.closeRevealAnim, &item.fadeAnim }) {
        if (*slot) {
            (*slot)->stop();
            (*slot)->disconnect(this);
            (*slot)->deleteLater();
            *slot = nullptr;
        }
    }
}

// ============================================================================
// Model
// ============================================================================

void DockGroupHeader::setPanels(const QList<DockPanel*>& panels)
{
    // Where every tab is drawn right now — the surviving ones slide from here
    // into their new slots if this rebuild dropped one.
    QHash<DockPanel*, qreal> visualLeft;
    for (const TabItem& item : m_items) {
        if (!item.panel.isNull()) {
            visualLeft.insert(
                item.panel.data(), item.rect.x() + item.slideOffsetX + item.dragOffsetX);
        }
    }

    // Items are MOVED across the rebuild rather than rebuilt from scratch: they
    // carry live animations (hover pill, and a farewell that must not be reset
    // by an unrelated re-sync arriving mid-flight).
    QList<TabItem> rebuilt;
    rebuilt.reserve(panels.size());
    QList<bool> reused;
    reused.fill(false, m_items.size());

    for (DockPanel* panel : panels) {
        if (!panel) {
            continue;
        }

        const int oldIndex = indexOfPanel(panel);
        if (oldIndex >= 0) {
            reused[oldIndex] = true;
            rebuilt.append(std::move(m_items[oldIndex]));
            rebuilt.last().title = panel->title();
            continue;
        }

        TabItem item;
        item.panel = panel;
        item.title = panel->title();
        rebuilt.append(item);
    }

    bool removedAny = false;
    for (int i = 0; i < m_items.size(); ++i) {
        if (!reused[i]) {
            destroyItemAnimations(m_items[i]);
            removedAny = true;
        }
    }
    m_items = std::move(rebuilt);

    if (!m_currentPanel.isNull() && indexOfPanel(m_currentPanel.data()) < 0) {
        m_currentPanel = nullptr;
    }
    m_hoveredIndex = -1;
    if (indexOfPanel(m_pressedPanel.data()) < 0) {
        m_pressedPanel = nullptr;
        m_dragStartIndex = -1;
        m_dragging = false;
    }

    updateLayout();

    if (removedAny && !m_items.isEmpty()) {
        runLayoutSlide(visualLeft, m_pressedPanel.data(), kLayoutSlideMs);
    }
}

QList<DockPanel*> DockGroupHeader::panels() const
{
    QList<DockPanel*> result;
    result.reserve(m_items.size());
    for (const TabItem& item : m_items) {
        if (!item.panel.isNull()) {
            result.append(item.panel.data());
        }
    }
    return result;
}

void DockGroupHeader::setCurrentPanel(DockPanel* panel)
{
    if (m_currentPanel.data() == panel) {
        return;
    }
    m_currentPanel = panel;
    update();
}

void DockGroupHeader::refreshContents()
{
    for (TabItem& item : m_items) {
        if (!item.panel.isNull()) {
            item.title = item.panel->title();
        }
    }
    updateLayout();
}

DockPanel* DockGroupHeader::neighbourOfTab(int index) const
{
    if (index < 0 || index >= m_items.size()) {
        return nullptr;
    }

    // To the right first, then to the left — the tab order the app tab strip
    // falls back through. Tabs already on their way out are skipped: they have
    // no content left to show.
    for (int i = index + 1; i < m_items.size(); ++i) {
        if (!m_items[i].isClosing && !m_items[i].panel.isNull()) {
            return m_items[i].panel.data();
        }
    }
    for (int i = index - 1; i >= 0; --i) {
        if (!m_items[i].isClosing && !m_items[i].panel.isNull()) {
            return m_items[i].panel.data();
        }
    }
    return nullptr;
}

int DockGroupHeader::indexOfPanel(DockPanel* panel) const
{
    if (!panel) {
        return -1;
    }
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].panel.data() == panel) {
            return i;
        }
    }
    return -1;
}

// ============================================================================
// Metrics
// ============================================================================

void DockGroupHeader::setBarHeight(int height)
{
    m_height = qMax(1, height);
    setFixedHeight(m_height);
    updateLayout();
}

void DockGroupHeader::setCornerRadius(int radius)
{
    const int clamped = qMax(0, radius);
    if (clamped == m_cornerRadius) {
        return;
    }
    m_cornerRadius = clamped;
    update();
}

void DockGroupHeader::updateScaledSizes()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();

    m_height = theme.scaled(kDefaultGroupHeaderHeight);
    m_iconSize = theme.scaled(12);
    m_iconMargin = theme.scaled(6);
    m_closeSize = theme.scaled(12);
    m_closeMargin = theme.scaled(6);
    m_tabPadding = theme.scaled(6);
    m_tabSpacing = theme.scaled(2);

    m_titleFont = theme.font(ruwa::ui::core::ThemeFontRole::Body);

    setFixedHeight(m_height);
}

void DockGroupHeader::updateLayout()
{
    if (m_items.isEmpty()) {
        update();
        return;
    }

    const QFontMetricsF fm(m_titleFont);

    // Natural widths first: icon + title + ×, everything at full length.
    QList<qreal> widths;
    widths.reserve(m_items.size());
    qreal natural = 0.0;
    for (const TabItem& item : m_items) {
        qreal w = m_iconMargin + m_iconSize + m_iconMargin;
        w += fm.horizontalAdvance(item.title) + m_tabPadding;
        w += m_closeMargin + m_closeSize;
        widths.append(w);
        natural += w;
    }

    // Docks are narrow, so three or four members overflow the strip almost
    // immediately — and a tab that runs off the right edge cannot be clicked at
    // all. Shrink proportionally instead (titles elide in drawTab), down to a
    // floor that still shows the icon and the ×.
    const qreal spacing = m_tabSpacing * (m_items.size() - 1);
    const qreal available = width() - m_tabPadding * 2.0 - spacing;
    const qreal floorWidth
        = m_iconMargin + m_iconSize + m_iconMargin + m_tabPadding + m_closeMargin + m_closeSize;

    if (available > 0.0 && natural > available) {
        const qreal scale = available / natural;
        for (qreal& w : widths) {
            w = qMax(floorWidth, w * scale);
        }
    }

    qreal x = m_tabPadding;
    for (int i = 0; i < m_items.size(); ++i) {
        TabItem& item = m_items[i];
        const qreal w = widths[i];

        item.rect = QRectF(x, 0, w, height());

        const qreal closeX = x + w - m_closeSize - m_closeMargin * 0.5;
        const qreal closeY = (height() - m_closeSize) / 2.0;
        item.closeRect = QRectF(closeX, closeY, m_closeSize, m_closeSize);

        x += w + m_tabSpacing;
    }

    update();
}

void DockGroupHeader::setDropInsertIndex(int index)
{
    const int clamped = (index < 0) ? -1 : qBound(0, index, static_cast<int>(m_items.size()));
    if (clamped == m_dropInsertIndex) {
        return;
    }
    m_dropInsertIndex = clamped;
    update();
}

int DockGroupHeader::insertIndexAt(const QPoint& globalPos, GroupInsertSide* outSide) const
{
    const QPointF pos = mapFromGlobal(globalPos);

    if (outSide) {
        *outSide = GroupInsertSide::After;
    }

    if (m_items.isEmpty()) {
        return 0;
    }

    for (int i = 0; i < m_items.size(); ++i) {
        const QRectF& r = m_items[i].rect;
        if (pos.x() < r.center().x()) {
            // Left half of tab i: the new panel takes its slot and pushes it right.
            if (outSide) {
                *outSide = GroupInsertSide::Before;
            }
            return i;
        }
        if (pos.x() < r.right()) {
            if (outSide) {
                *outSide = GroupInsertSide::After;
            }
            return i + 1;
        }
    }

    return static_cast<int>(m_items.size());
}

// ============================================================================
// Theme
// ============================================================================

void DockGroupHeader::applyTheme(const ruwa::ui::core::ThemeColors& colors)
{
    m_colors = colors;
    updateScaledSizes();
    updateLayout();
}

// ============================================================================
// Context menu
// ============================================================================

ruwa::ui::widgets::ContextMenuType DockGroupHeader::contextMenuType() const
{
    // None lets ContextMenuSystem keep walking up the parent chain, which is
    // the right answer for the empty stretch of strip right of the last tab.
    return (tabIndexAtCursor() >= 0) ? ruwa::ui::widgets::ContextMenuType::DockPanelTitle
                                     : ruwa::ui::widgets::ContextMenuType::None;
}

QVariantMap DockGroupHeader::contextMenuContext() const
{
    QVariantMap ctx;

    // Read from the cursor rather than from a remembered press: the menu system
    // filters the right-click before this widget sees it, so no press is
    // recorded — but the click that opened the menu is where the cursor is.
    const int index = tabIndexAtCursor();
    if (index < 0) {
        return ctx;
    }

    DockPanel* panel = m_items[index].panel.data();
    if (!panel) {
        return ctx;
    }

    ctx["dockPanelPtr"] = static_cast<qulonglong>(reinterpret_cast<quintptr>(panel));
    ctx["dockPanelTitle"] = panel->title();
    return ctx;
}

// ============================================================================
// Painting
// ============================================================================

void DockGroupHeader::paintEvent(QPaintEvent* /*event*/)
{
    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    // Same surface as a panel title bar: the header reads as chrome belonging
    // to the panel below it, not as a separate floating strip. Its top corners
    // are the group cell's top corners — the members square theirs off while
    // grouped (DockPanel::calculateCornerRadii).
    const QRectF stripRect(kStripOuterInset, kStripOuterInset, width() - kStripOuterInset * 2.0,
        height() - kStripOuterInset);
    const QPainterPath outline = buildStripOutline(stripRect, m_cornerRadius);

    QPainterPath filled = outline;
    filled.closeSubpath();
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_colors.surfaceAlt);
    painter.drawPath(filled);

    const DockPanel* draggedPanel = m_dragging ? m_pressedPanel.data() : nullptr;
    for (int i = 0; i < m_items.size(); ++i) {
        if (m_items[i].panel.data() == draggedPanel) {
            continue;
        }
        drawTab(painter, m_items[i], m_items[i].panel.data() == m_currentPanel.data());
    }
    // The tab under the pointer stays above neighbours while crossing them.
    if (draggedPanel) {
        const int draggedIndex = indexOfPanel(m_pressedPanel.data());
        if (draggedIndex >= 0) {
            drawTab(painter, m_items[draggedIndex], draggedPanel == m_currentPanel.data());
        }
    }

    // Insertion caret for an in-flight drop.
    if (m_dropInsertIndex >= 0) {
        qreal caretX = m_tabPadding * 0.5;
        if (m_dropInsertIndex < m_items.size()) {
            caretX = m_items[m_dropInsertIndex].rect.left() - m_tabSpacing * 0.5;
        } else if (!m_items.isEmpty()) {
            caretX = m_items.last().rect.right() + m_tabSpacing * 0.5;
        }

        const qreal inset = height() * 0.2;
        painter.setPen(QPen(m_colors.primary, qMax(1.0, height() * 0.08)));
        painter.drawLine(QPointF(caretX, inset), QPointF(caretX, height() - inset));
    }

    // Outline last, so no tab can paint over it. No bottom edge: the member
    // below draws its own top border and the two would double up.
    QPen borderPen(m_colors.border.darker(kBorderDarkerFactor));
    borderPen.setWidthF(kStripBorderWidth);
    borderPen.setCosmetic(true);
    borderPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(outline);
}

void DockGroupHeader::drawTab(QPainter& painter, const TabItem& item, bool isActive) const
{
    if (item.panel.isNull()) {
        return;
    }

    painter.save();

    // Everything below is drawn in the tab's own frame: the farewell drops it
    // and fades it out, and a survivor of a removal slides in from where the
    // strip used to have it.
    painter.translate(item.slideOffsetX + item.dragOffsetX, item.verticalOffset);
    if (item.opacity < 1.0) {
        painter.setOpacity(qBound(0.0, item.opacity, 1.0));
    }

    const qreal progress = item.hoverProgress;

    // Hover pill behind the whole tab row.
    if (progress > 0.001) {
        const qreal vInset = qMax(1.0, height() * 0.12);
        const qreal radius = qMax(2.0, height() * 0.25);
        QColor bg = m_colors.overlayHover();
        bg.setAlpha(
            static_cast<int>(qBound(0.0, progress, 1.0) * bg.alpha() * kHoverPillAlphaFactor));
        painter.setPen(Qt::NoPen);
        painter.setBrush(bg);
        painter.drawRoundedRect(item.rect.adjusted(0.5, vInset, -0.5, -vInset), radius, radius);
    }

    // Selection is carried by the TEXT and the icon alone — exactly as the app
    // tab strip does it. No plate behind the active tab: the only background a
    // tab ever gets is the hover pill above, so a plate would read as a
    // permanently hovered tab.
    QColor textColor = isActive ? m_colors.text : m_colors.textMuted;
    if (!isActive && progress > 0.0) {
        textColor = ruwa::ui::core::ThemeColors::interpolate(
            m_colors.textMuted, m_colors.text, progress * 0.5);
    }

    QColor iconTint = isActive ? m_colors.primary : m_colors.textMuted;
    if (!isActive && progress > 0.0) {
        iconTint
            = ruwa::ui::core::ThemeColors::interpolate(m_colors.textMuted, m_colors.text, progress);
    }

    // Icon
    const qreal iconX = item.rect.x() + m_iconMargin;
    const qreal iconY = (height() - m_iconSize) / 2.0;
    const QRectF iconRect(iconX, iconY, m_iconSize, m_iconSize);

    QIcon icon;
    if (item.panel->iconType()) {
        icon = ruwa::ui::core::IconProvider::instance().getColoredIcon(
            *item.panel->iconType(), iconTint);
    } else if (!item.panel->icon().isNull()) {
        icon = item.panel->icon();
    }

    if (!icon.isNull()) {
        const QPixmap pix = icon.pixmap(m_iconSize, m_iconSize);
        if (!pix.isNull()) {
            painter.drawPixmap(iconRect.toRect(), pix);
        }
    } else {
        painter.setPen(Qt::NoPen);
        painter.setBrush(iconTint);
        painter.drawRoundedRect(iconRect, 2, 2);
    }

    // Title — elided, because updateLayout() compresses tabs when the strip is
    // too narrow to show them all at their natural width.
    const qreal textX = iconX + m_iconSize + m_iconMargin;
    const qreal textW = qMax(0.0, item.rect.right() - textX - m_closeMargin - m_closeSize);
    painter.setFont(m_titleFont);
    painter.setPen(textColor);
    const QString shown = QFontMetricsF(m_titleFont).elidedText(item.title, Qt::ElideRight, textW);
    painter.drawText(QRectF(textX, 0, textW, height()), Qt::AlignLeft | Qt::AlignVCenter, shown);

    // Close (×): alpha on the pen only, so it never fights the tab's own opacity.
    if (item.closeRevealProgress > 0.001 && item.panel->isClosable()) {
        const qreal hotMix
            = item.closeHovered ? qBound(0.0, (item.closeRevealProgress - 0.12) / 0.45, 1.0) : 0.0;
        QColor closeColor
            = ruwa::ui::core::ThemeColors::interpolate(m_colors.textMuted, m_colors.text, hotMix);
        closeColor.setAlphaF(closeColor.alphaF() * item.closeRevealProgress);
        painter.setPen(QPen(closeColor, 1.2, Qt::SolidLine, Qt::RoundCap));

        const qreal cx = item.closeRect.center().x();
        const qreal cy = item.closeRect.center().y();
        const qreal sz = m_closeSize * 0.45;
        painter.drawLine(QPointF(cx - sz / 2, cy - sz / 2), QPointF(cx + sz / 2, cy + sz / 2));
        painter.drawLine(QPointF(cx + sz / 2, cy - sz / 2), QPointF(cx - sz / 2, cy + sz / 2));
    }

    painter.restore();
}

// ============================================================================
// Events
// ============================================================================

void DockGroupHeader::resizeEvent(QResizeEvent* event)
{
    QWidget::resizeEvent(event);
    updateLayout();
}

void DockGroupHeader::changeEvent(QEvent* event)
{
    QWidget::changeEvent(event);
    if (event->type() == QEvent::LanguageChange) {
        refreshContents();
    }
}

int DockGroupHeader::tabIndexAt(const QPointF& pos) const
{
    for (int i = 0; i < m_items.size(); ++i) {
        // A tab mid-farewell is on its way out and answers to nothing.
        if (m_items[i].isClosing) {
            continue;
        }
        const TabItem& item = m_items[i];
        if (item.rect.translated(item.slideOffsetX + item.dragOffsetX, item.verticalOffset)
                .contains(pos)) {
            return i;
        }
    }
    return -1;
}

int DockGroupHeader::tabIndexAtCursor() const
{
    return tabIndexAt(mapFromGlobal(QCursor::pos()));
}

bool DockGroupHeader::isCloseButtonAt(int index, const QPointF& pos) const
{
    if (index < 0 || index >= m_items.size()) {
        return false;
    }
    // A panel that refuses to close draws no × (see drawTab), so nothing here
    // is clickable either — the press falls through to selecting the tab.
    const DockPanel* panel = m_items[index].panel.data();
    if (!panel || !panel->isClosable()) {
        return false;
    }
    const TabItem& item = m_items[index];
    return item.closeRect.adjusted(-2, -2, 2, 2)
        .translated(item.slideOffsetX + item.dragOffsetX, item.verticalOffset)
        .contains(pos);
}

void DockGroupHeader::mousePressEvent(QMouseEvent* event)
{
    if (event->button() != Qt::LeftButton) {
        QWidget::mousePressEvent(event);
        return;
    }

    const int index = tabIndexAt(event->position());
    if (index < 0) {
        QWidget::mousePressEvent(event);
        return;
    }

    if (isCloseButtonAt(index, event->position())) {
        DockPanel* closing = m_items[index].panel.data();

        // Hand the stage over NOW, while the tab is still fading: the host
        // slides the successor in over the closing member, which is still alive
        // and still painting its content. The app tab strip does the same — the
        // content transition runs alongside the tab's farewell, not after it.
        DockPanel* successor
            = (closing && closing == m_currentPanel.data()) ? neighbourOfTab(index) : nullptr;
        emit panelCloseStarted(closing, successor);

        // The handover above can re-sync the strip, so the index is re-read.
        startCloseAnimation(indexOfPanel(closing), successor != nullptr);
        return;
    }

    // Activate on press (not release): selecting a tab should feel immediate,
    // and reordering starts from the tab the user just selected.
    if (DockPanel* panel = m_items[index].panel.data()) {
        emit panelActivationRequested(panel);
    }

    m_dragStartIndex = index;
    m_pressedPanel = m_items[index].panel.data();
    m_pressStartGlobal = event->globalPosition().toPoint();
    m_dragGrabOffsetX = event->position().x()
        - (m_items[index].rect.left() + m_items[index].slideOffsetX + m_items[index].dragOffsetX);
    m_dragging = false;
}

void DockGroupHeader::mouseMoveEvent(QMouseEvent* event)
{
    const QPoint globalPos = event->globalPosition().toPoint();

    if (!m_pressedPanel.isNull() && (event->buttons() & Qt::LeftButton)) {
        if (!m_dragging && (globalPos - m_pressStartGlobal).manhattanLength() >= kDragThreshold) {
            m_dragging = true;
        }
        if (m_dragging) {
            updateDraggedTab(globalPos);
        }
        return;
    }

    updateHoverState(event->position());
}

void DockGroupHeader::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_dragging) {
        updateDraggedTab(event->globalPosition().toPoint());
        finishTabDrag();
    } else if (event->button() == Qt::LeftButton) {
        m_pressedPanel = nullptr;
        m_dragStartIndex = -1;
    }

    m_dragging = false;
    QWidget::mouseReleaseEvent(event);
}

QHash<DockPanel*, qreal> DockGroupHeader::visualTabLefts() const
{
    QHash<DockPanel*, qreal> result;
    result.reserve(m_items.size());
    for (const TabItem& item : m_items) {
        if (!item.panel.isNull()) {
            result.insert(item.panel.data(), item.rect.x() + item.slideOffsetX + item.dragOffsetX);
        }
    }
    return result;
}

void DockGroupHeader::updateDraggedTab(const QPoint& globalPos)
{
    DockPanel* panel = m_pressedPanel.data();
    int index = indexOfPanel(panel);
    if (!panel || index < 0) {
        return;
    }

    const QPointF localPos = mapFromGlobal(globalPos);
    const qreal tabWidth = m_items[index].rect.width();
    const qreal minLeft = m_tabPadding;
    const qreal maxLeft = qMax(minLeft, width() - m_tabPadding - tabWidth);
    const qreal visualLeft = qBound(minLeft, localPos.x() - m_dragGrabOffsetX, maxLeft);
    const qreal visualRight = visualLeft + tabWidth;
    const qreal visualCenter = visualLeft + tabWidth * 0.5;

    int targetIndex = index;
    const qreal slotCenter = m_items[index].rect.center().x();
    if (visualCenter < slotCenter) {
        // For differently sized tabs, comparing centers is asymmetric: a wide
        // dragged tab may never get its center left of a narrow neighbour's
        // center. Its leading edge crossing the neighbour's center is the stable
        // insertion threshold used by movable tab bars.
        while (targetIndex > 0 && visualLeft < m_items[targetIndex - 1].rect.center().x()) {
            --targetIndex;
        }
    } else if (visualCenter > slotCenter) {
        while (targetIndex + 1 < m_items.size()
            && visualRight > m_items[targetIndex + 1].rect.center().x()) {
            ++targetIndex;
        }
    }

    if (targetIndex != index) {
        const QHash<DockPanel*, qreal> previousVisualLefts = visualTabLefts();
        m_items.move(index, targetIndex);
        updateLayout();
        runLayoutSlide(previousVisualLefts, panel, kTabReorderSlideMs);
        index = targetIndex;
    }

    m_items[index].dragOffsetX = visualLeft - m_items[index].rect.x();
    update();
}

void DockGroupHeader::finishTabDrag()
{
    DockPanel* panel = m_pressedPanel.data();
    const int finalIndex = indexOfPanel(panel);
    if (!panel || finalIndex < 0) {
        m_pressedPanel = nullptr;
        m_dragStartIndex = -1;
        m_dragging = false;
        return;
    }

    const QHash<DockPanel*, qreal> previousVisualLefts = visualTabLefts();
    m_items[finalIndex].dragOffsetX = 0.0;

    const bool orderChanged = finalIndex != m_dragStartIndex;
    m_pressedPanel = nullptr;
    m_dragStartIndex = -1;
    m_dragging = false;

    if (orderChanged) {
        emit panelReorderRequested(panel, finalIndex);
    }

    // Land the dragged tab in its final slot; neighbours may still be finishing
    // the short crossing animation, so all current visual positions are kept.
    runLayoutSlide(previousVisualLefts, nullptr, kTabReorderSlideMs);
}

void DockGroupHeader::wheelEvent(QWheelEvent* event)
{
    if (m_items.size() < 2) {
        QWidget::wheelEvent(event);
        return;
    }

    // Vertical wheel is the usual gesture over a tab strip; a horizontal wheel
    // (tilt / trackpad swipe) means the same thing here.
    const QPoint delta = event->angleDelta();
    m_wheelAccumulator += (delta.y() != 0) ? delta.y() : delta.x();

    const int steps = m_wheelAccumulator / kWheelNotch;
    if (steps == 0) {
        event->accept();
        return;
    }
    m_wheelAccumulator -= steps * kWheelNotch;

    // Scrolling down/right walks forward through the tab order, and it CLAMPS:
    // a wheel is a continuous gesture, and wrapping past the last tab back to
    // the first reads as an accident. Ctrl+PageUp/Down (DockGroupHost) wraps.
    const int current = indexOfPanel(m_currentPanel.data());
    const int target
        = qBound(0, (current < 0 ? 0 : current) - steps, static_cast<int>(m_items.size()) - 1);

    if (target != current) {
        if (DockPanel* panel = m_items[target].panel.data()) {
            emit panelActivationRequested(panel);
        }
    }

    event->accept();
}

void DockGroupHeader::leaveEvent(QEvent* event)
{
    if (m_hoveredIndex >= 0) {
        startHoverAnimation(m_hoveredIndex, false);
        startCloseRevealAnimation(m_hoveredIndex, false);
        m_items[m_hoveredIndex].closeHovered = false;
        m_hoveredIndex = -1;
    }
    QWidget::leaveEvent(event);
}

void DockGroupHeader::updateHoverState(const QPointF& pos)
{
    const int index = tabIndexAt(pos);

    if (index != m_hoveredIndex) {
        if (m_hoveredIndex >= 0 && m_hoveredIndex < m_items.size()) {
            startHoverAnimation(m_hoveredIndex, false);
            startCloseRevealAnimation(m_hoveredIndex, false);
            m_items[m_hoveredIndex].closeHovered = false;
        }
        m_hoveredIndex = index;
        if (m_hoveredIndex >= 0) {
            startHoverAnimation(m_hoveredIndex, true);
            startCloseRevealAnimation(m_hoveredIndex, true);
            // The full title, for when a compressed tab elides it.
            setToolTip(m_items[m_hoveredIndex].title);
        } else {
            setToolTip(QString());
        }
    }

    if (m_hoveredIndex >= 0 && m_hoveredIndex < m_items.size()) {
        const bool overClose = isCloseButtonAt(m_hoveredIndex, pos);
        if (overClose != m_items[m_hoveredIndex].closeHovered) {
            m_items[m_hoveredIndex].closeHovered = overClose;
            update();
        }
    }
}

// ============================================================================
// Animations
// ============================================================================

void DockGroupHeader::startHoverAnimation(int index, bool hovering)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }

    TabItem& item = m_items[index];
    if (!item.hoverAnim) {
        item.hoverAnim = new QVariantAnimation(this);
        item.hoverAnim->setDuration(anim::duration(kHoverAnimationMs));
        item.hoverAnim->setEasingCurve(QEasingCurve::OutCubic);
        DockPanel* owner = item.panel.data();
        connect(item.hoverAnim, &QVariantAnimation::valueChanged, this,
            [this, owner](const QVariant& value) {
                const int i = indexOfPanel(owner);
                if (i >= 0) {
                    m_items[i].hoverProgress = value.toReal();
                    update();
                }
            });
    }

    item.hoverAnim->stop();
    item.hoverAnim->setStartValue(item.hoverProgress);
    item.hoverAnim->setEndValue(hovering ? 1.0 : 0.0);
    anim::start(item.hoverAnim);
}

void DockGroupHeader::startCloseAnimation(int index, bool matchHandover)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }

    TabItem& item = m_items[index];
    DockPanel* owner = item.panel.data();
    if (item.isClosing || !owner) {
        return;
    }

    item.isClosing = true;

    // The farewell only REPORTS when it lands, and the host waits for its own
    // content slide on top of that. Until then the model still holds the panel,
    // so nothing else in the layout moves underneath the animation — the same
    // deferred-confirm shape the app tab strip uses.
    const qreal startOpacity = item.opacity;
    const qreal startOffset = item.verticalOffset;
    const qreal distance = ruwa::ui::core::ThemeManager::instance().scaled(kTabPopDistance);

    if (!item.fadeAnim) {
        item.fadeAnim = new QVariantAnimation(this);
    }
    item.fadeAnim->stop();
    item.fadeAnim->setStartValue(0.0);
    item.fadeAnim->setEndValue(1.0);
    item.fadeAnim->setDuration(matchHandover ? kCloseFadeWithHandoverMs : kCloseFadeMs);
    item.fadeAnim->setEasingCurve(QEasingCurve::InCubic);

    // Indexes shift while this runs (another tab may leave), so every frame
    // finds its item by panel.
    connect(item.fadeAnim, &QVariantAnimation::valueChanged, this,
        [this, owner, startOpacity, startOffset, distance](const QVariant& value) {
            const int i = indexOfPanel(owner);
            if (i < 0) {
                return;
            }
            const qreal p = value.toReal();
            m_items[i].opacity = startOpacity * (1.0 - p);
            m_items[i].verticalOffset = startOffset + p * (distance - startOffset);
            update();
        });

    connect(
        item.fadeAnim, &QVariantAnimation::finished, this,
        [this, owner]() {
            const int i = indexOfPanel(owner);
            if (i >= 0) {
                // Leave the tab invisible where it fell: the close removes it
                // from the model, and setPanels() then slides the survivors.
                m_items[i].opacity = 0.0;
            }
            emit panelFarewellFinished(owner);
        },
        Qt::SingleShotConnection);

    item.fadeAnim->start();
}

void DockGroupHeader::runLayoutSlide(const QHash<DockPanel*, qreal>& visualLeftBeforeLayout,
    DockPanel* stationaryPanel, int duration)
{
    if (!m_layoutSlideAnim) {
        m_layoutSlideAnim = new QVariantAnimation(this);
        m_layoutSlideAnim->setEasingCurve(QEasingCurve::OutCubic);
        connect(m_layoutSlideAnim, &QVariantAnimation::valueChanged, this,
            [this](const QVariant& value) {
                const qreal t = value.toReal();
                for (TabItem& item : m_items) {
                    item.slideOffsetX = item.slideStartOffsetX * t;
                }
                update();
            });
    }

    m_layoutSlideAnim->stop();

    bool anyShift = false;
    for (TabItem& item : m_items) {
        DockPanel* panel = item.panel.data();
        if (panel == stationaryPanel) {
            item.slideStartOffsetX = 0.0;
            item.slideOffsetX = 0.0;
            continue;
        }
        // A tab that was not on screen before has nothing to slide from.
        const qreal previous = (panel && visualLeftBeforeLayout.contains(panel))
            ? visualLeftBeforeLayout[panel]
            : item.rect.x();
        item.slideStartOffsetX = previous - item.rect.x();
        item.slideOffsetX = item.slideStartOffsetX;
        if (!qFuzzyIsNull(item.slideStartOffsetX)) {
            anyShift = true;
        }
    }

    if (!anyShift) {
        update();
        return;
    }

    m_layoutSlideAnim->setStartValue(1.0);
    m_layoutSlideAnim->setEndValue(0.0);
    m_layoutSlideAnim->setDuration(qMax(0, duration));
    m_layoutSlideAnim->start();
}

void DockGroupHeader::startCloseRevealAnimation(int index, bool reveal)
{
    if (index < 0 || index >= m_items.size()) {
        return;
    }

    TabItem& item = m_items[index];
    if (!item.closeRevealAnim) {
        item.closeRevealAnim = new QVariantAnimation(this);
        DockPanel* owner = item.panel.data();
        connect(item.closeRevealAnim, &QVariantAnimation::valueChanged, this,
            [this, owner](const QVariant& value) {
                const int i = indexOfPanel(owner);
                if (i >= 0) {
                    m_items[i].closeRevealProgress = value.toReal();
                    update();
                }
            });
    }

    item.closeRevealAnim->stop();
    item.closeRevealAnim->setStartValue(item.closeRevealProgress);
    item.closeRevealAnim->setEndValue(reveal ? 1.0 : 0.0);
    item.closeRevealAnim->setDuration(
        anim::duration(reveal ? kCloseRevealInMs : kCloseRevealOutMs));
    item.closeRevealAnim->setEasingCurve(reveal ? QEasingCurve::OutCubic : QEasingCurve::InCubic);
    anim::start(item.closeRevealAnim);
}

} // namespace ruwa::ui::docking
