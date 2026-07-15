// SPDX-License-Identifier: MPL-2.0

// MenuPopup.cpp
#include "MenuPopup.h"
#include "OverlayContainer.h"
#include "features/theme/manager/ThemeManager.h"
#include "features/theme/manager/ThemeColors.h"
#include "shared/resources/IconProvider.h"
#include "shared/style/PaintingUtils.h"

#include <QVBoxLayout>
#include <QFontMetrics>
#include <QResizeEvent>
#include <QPainter>
#include <QPainterPath>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QEnterEvent>
#include <QApplication>
#include <QScreen>
#include <QGraphicsOpacityEffect>
#include <QPolygonF>
#include <QTimer>
#include <QDateTime>
#include <QCursor>
namespace ruwa::ui::widgets {

namespace {
/// Soft-shadow extent below the attached (top-level) popup body, scaled.
int attachedShadowExtentPx()
{
    return ruwa::ui::core::ThemeManager::instance().scaled(
        ruwa::ui::painting::kAttachedShadowExtentBase);
}
/// Soft-shadow extent on each side of the attached popup body, scaled.
int attachedShadowSideExtentPx()
{
    return ruwa::ui::core::ThemeManager::instance().scaled(
        ruwa::ui::painting::kAttachedShadowSideExtentBase);
}
/// Top-edge flare radius (how far the flat top insets to the body sides), scaled.
int attachedOuterCornerRadiusPx()
{
    return ruwa::ui::core::ThemeManager::instance().scaled(
        ruwa::ui::painting::kAttachedOuterCornerRadiusBase);
}
} // namespace

// ============================================================================
// MenuItemWidget Implementation
// ============================================================================

MenuItemWidget::MenuItemWidget(const MenuItem& item, QWidget* parent)
    : QWidget(parent)
    , m_item(item)
    , m_checked(item.isToggle ? item.checked : false)
{
    if (item.separator) {
        setFixedHeight(9);
        setCursor(Qt::ArrowCursor);
    } else {
        setFixedHeight(32);
        setCursor(item.enabled ? Qt::PointingHandCursor : Qt::ArrowCursor);
    }

    setMouseTracking(true);
}

namespace {
int menuItemReservedRightPx(const MenuItem& item, const QFont& baseFont)
{
    int reservedRight = 16;
    if (item.hasSubmenu()) {
        reservedRight = qMax(reservedRight, 28);
    }
    if (!item.shortcut.isEmpty()) {
        QFont shortcutFont = baseFont;
        shortcutFont.setPointSize(8);
        const int sw = QFontMetrics(shortcutFont).horizontalAdvance(item.shortcut);
        reservedRight = qMax(reservedRight, sw + 24);
    }
    return reservedRight;
}

int menuItemPreferredWidthPx(const MenuItem& item, const QFont& baseFont)
{
    if (item.separator) {
        return 0;
    }
    QFont textFont = baseFont;
    textFont.setPointSize(9);
    const int textLeft = item.isToggle ? 32 : 16;
    const int textW = QFontMetrics(textFont).horizontalAdvance(item.text);
    return textLeft + textW + menuItemReservedRightPx(item, baseFont);
}
} // namespace

QSize MenuItemWidget::sizeHint() const
{
    if (m_item.separator) {
        return QSize(0, 9);
    }
    return QSize(menuItemPreferredWidthPx(m_item, font()), 32);
}

QSize MenuItemWidget::minimumSizeHint() const
{
    return sizeHint();
}

void MenuItemWidget::setHovered(bool hovered)
{
    if (m_isHovered != hovered) {
        m_isHovered = hovered;
        update();
    }
}

void MenuItemWidget::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    if (m_item.separator) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.border);
        painter.drawRect(12, 4, width() - 24, 1);
        return;
    }

    QRectF rect = this->rect().adjusted(4, 2, -4, -2);

    // Background
    if (!m_item.enabled) {
        // Disabled - no background
    } else if (m_isPressed) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.overlay(0.12));
        painter.drawRoundedRect(rect, 4, 4);
    } else if (m_isHovered) {
        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.overlay(0.08));
        painter.drawRoundedRect(rect, 4, 4);
    }

    // Toggle: when checked show Confirm icon, when unchecked show nothing (reserve space for
    // alignment)
    int textLeft = 16;
    if (m_item.isToggle) {
        if (m_checked) {
            const auto& theme = ruwa::ui::core::ThemeManager::instance();
            const int iconSize = theme.scaled(14);
            const int iconX = 10;
            const int iconY = (height() - iconSize) / 2;
            QRect iconRect(iconX, iconY, iconSize, iconSize);

            QColor iconColor = m_item.enabled ? colors.text : colors.textDisabled();
            QIcon icon = ruwa::ui::core::IconProvider::instance().getColoredIcon(
                ruwa::ui::core::IconProvider::StandardIcon::Confirm, iconColor);
            if (!icon.isNull()) {
                icon.paint(&painter, iconRect);
            }
        }
        textLeft = 32;
    }

    // Text
    QColor textColor = m_item.enabled ? colors.text : colors.textDisabled();
    painter.setPen(textColor);

    QFont textFont = font();
    textFont.setPointSize(9);
    painter.setFont(textFont);

    const int reservedRight = menuItemReservedRightPx(m_item, font());
    const int textW = qMax(1, width() - textLeft - reservedRight);
    QRect textRect(textLeft, 0, textW, height());
    painter.drawText(textRect, Qt::AlignLeft | Qt::AlignVCenter, m_item.text);

    // Shortcut
    int rightMargin = 16;
    if (m_item.hasSubmenu()) {
        rightMargin = 28; // Space for arrow
        // Submenu arrow (chevron right)
        QColor arrowColor = m_item.enabled ? colors.textMuted : colors.textDisabled();
        painter.setPen(arrowColor);
        painter.setBrush(arrowColor);
        QPolygonF arrow;
        int ax = width() - 18;
        int ay = height() / 2;
        arrow << QPointF(ax, ay - 3) << QPointF(ax, ay + 3) << QPointF(ax + 4, ay);
        painter.drawPolygon(arrow);
    }

    if (!m_item.shortcut.isEmpty()) {
        QColor shortcutColor = m_item.enabled ? colors.textMuted : colors.textDisabled();
        painter.setPen(shortcutColor);

        QFont shortcutFont = font();
        shortcutFont.setPointSize(8);
        painter.setFont(shortcutFont);

        const int shortcutBlockW = reservedRight - rightMargin;
        QRect shortcutRect(width() - reservedRight, 0, qMax(1, shortcutBlockW), height());
        painter.drawText(shortcutRect, Qt::AlignRight | Qt::AlignVCenter, m_item.shortcut);
    }
}

void MenuItemWidget::enterEvent(QEnterEvent* event)
{
    Q_UNUSED(event);
    if (!m_item.separator && m_item.enabled) {
        m_isHovered = true;
        emit hovered();
        if (m_item.hasSubmenu()) {
            emit submenuHovered(m_item.submenu);
        }
        update();
    }
}

void MenuItemWidget::leaveEvent(QEvent* event)
{
    Q_UNUSED(event);
    m_isHovered = false;
    update();
}

void MenuItemWidget::mousePressEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && !m_item.separator && m_item.enabled) {
        m_isPressed = true;
        update();
    }
    QWidget::mousePressEvent(event);
}

void MenuItemWidget::mouseReleaseEvent(QMouseEvent* event)
{
    if (event->button() == Qt::LeftButton && m_isPressed) {
        m_isPressed = false;
        if (rect().contains(event->pos()) && m_item.enabled) {
            if (m_item.isToggle) {
                m_checked = !m_checked;
                if (m_item.toggleAction) {
                    m_item.toggleAction(m_checked);
                }
            }
            emit clicked();
        }
        update();
    }
    QWidget::mouseReleaseEvent(event);
}

// ============================================================================
// MenuPopup Implementation
// ============================================================================

MenuPopup::MenuPopup(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::Widget);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setMouseTracking(true);

    hide();

    m_layout = new QVBoxLayout(this);
    m_layout->setContentsMargins(4, 6, 4, 6);
    m_layout->setSpacing(0);

    // No QGraphicsDropShadowEffect - it causes resize/collapse when combined with
    // QPropertyAnimation
    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_opacityAnim = new QPropertyAnimation(this, "popupOpacity", this);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
}

MenuPopup::~MenuPopup()
{
    qDeleteAll(m_itemWidgets);
}

void MenuPopup::setItems(const QList<MenuItem>& items)
{
    m_items = items;
    rebuildItems();
}

void MenuPopup::rebuildItems()
{
    m_isRebuilding = true;

    // Top-level popup hangs from the TopBar seam like MessagePopup: reserve side +
    // bottom padding for the soft shadow and the top-edge flare so the menu content
    // sits inside the visible body. Submenus stay a compact floating rounded rect.
    const int topPad = 6;
    int sidePad = 4;
    int bottomPad = 6;
    if (!m_isSubmenu) {
        sidePad = attachedShadowSideExtentPx() + attachedOuterCornerRadiusPx() + 4;
        bottomPad = 6 + attachedShadowExtentPx();
    }
    m_layout->setContentsMargins(sidePad, topPad, sidePad, bottomPad);

    // Remove and delete old widgets
    for (auto* widget : m_itemWidgets) {
        m_layout->removeWidget(widget);
        delete widget;
    }
    m_itemWidgets.clear();

    for (int i = 0; i < m_items.size(); ++i) {
        const auto& item = m_items[i];
        auto* widget = new MenuItemWidget(item, this);

        connect(widget, &MenuItemWidget::clicked, this, [this, i, item]() {
            emit itemClicked(i, item.isToggle);
            if (!item.isToggle) {
                if (item.action) {
                    item.action();
                }
                hidePopup();
            }
        });

        connect(widget, &MenuItemWidget::hovered, this, [this, widget]() {
            for (auto* w : m_itemWidgets) {
                if (w != widget) {
                    w->setHovered(false);
                }
            }
            m_hoveredItem = widget;
            reconcileSubmenu(); // Arbitrates show/switch/hide with diagonal-aim awareness
        });

        connect(
            widget, &MenuItemWidget::submenuHovered, this, [this, widget](const QList<MenuItem>&) {
                m_hoveredItem = widget;
                reconcileSubmenu();
            });

        m_layout->addWidget(widget);
        m_itemWidgets.append(widget);
    }

    m_layout->activate();

    // Compute target size directly from items
    int h = topPad + bottomPad;
    for (const auto& item : m_items) {
        h += item.separator ? 9 : 32;
    }

    int itemWidth = 0;
    for (const auto& item : m_items) {
        itemWidth = qMax(itemWidth, menuItemPreferredWidthPx(item, font()));
    }
    const int w = qMax(1, itemWidth + 2 * sidePad);

    m_cachedWidth = w;
    m_cachedHeight = h;
    m_targetHeight = h;
    m_displayHeight = h;
    setFixedSize(w, h);

    m_isRebuilding = false;
}

void MenuPopup::ensureSubmenuPopup()
{
    if (m_submenuPopup)
        return;

    QWidget* overlay = parentWidget();
    if (!overlay)
        return;

    m_submenuPopup = new MenuPopup(overlay);
    m_submenuPopup->setParent(overlay);
    m_submenuPopup->m_isSubmenu = true;

    if (OverlayContainer* oc = OverlayContainer::instance(overlay->window())) {
        oc->registerPopup(m_submenuPopup);
    }

    connect(m_submenuPopup, &MenuPopup::itemClicked, this, [this](int, bool isToggle) {
        if (!isToggle)
            hidePopup();
    });
    connect(m_submenuPopup, &MenuPopup::mouseLeft, this, [this]() { emit mouseLeft(); });
    connect(m_submenuPopup, &MenuPopup::shown, this, [this]() { startSubmenuAimPoll(); });
    connect(m_submenuPopup, &MenuPopup::hidden, this, [this]() {
        m_submenuAnchor = nullptr;
        m_submenuCloseArmedAt = -1;
        stopSubmenuAimPoll();
    });
}

QPoint MenuPopup::calculateSubmenuPosition(QWidget* anchor) const
{
    if (!anchor || !parentWidget() || !m_submenuPopup)
        return QPoint();

    bool showToRight = true;
    QPoint anchorGlobal = anchor->mapToGlobal(QPoint(anchor->width(), 0));
    QPoint targetPos = parentWidget()->mapFromGlobal(anchorGlobal);
    targetPos.setX(targetPos.x() + SUBMENU_GAP);
    targetPos.setY(targetPos.y() - 6); // Align with menu top margin

    QPoint globalPos = parentWidget()->mapFromGlobal(targetPos);
    QScreen* screen = QApplication::screenAt(globalPos);
    if (screen) {
        QRect screenRect = screen->availableGeometry();
        if (globalPos.x() + m_submenuPopup->width() > screenRect.right()) {
            // Show to the left of anchor instead
            showToRight = false;
            anchorGlobal = anchor->mapToGlobal(QPoint(0, 0));
            targetPos = parentWidget()->mapFromGlobal(anchorGlobal);
            targetPos.setX(targetPos.x() - m_submenuPopup->width() - SUBMENU_GAP);
            targetPos.setY(targetPos.y() - 6);
        }
        if (globalPos.y() + m_submenuPopup->height() > screenRect.bottom()) {
            targetPos.setY(screenRect.bottom() - m_submenuPopup->height()
                - parentWidget()->mapFromGlobal(QPoint(0, 0)).y());
        }
    }
    m_submenuPopup->m_submenuSlideFromLeft = showToRight;
    return targetPos;
}

QPoint MenuPopup::calculatePosition(QWidget* anchor) const
{
    if (!anchor || !parentWidget())
        return QPoint();

    const int sideInset = attachedShadowSideExtentPx() + attachedOuterCornerRadiusPx();

    // Top: glue the popup to the TopBar seam, exactly like MessagePopup so both
    // read as one surface growing out of the bar.
    int topY;
    if (auto* oc = qobject_cast<OverlayContainer*>(parentWidget())) {
        topY = oc->messagePopupAnchorY();
    } else {
        QPoint anchorBottom
            = parentWidget()->mapFromGlobal(anchor->mapToGlobal(QPoint(0, anchor->height())));
        topY = anchorBottom.y();
    }

    // X: align the visible (narrow) body left edge under the button left edge.
    QPoint anchorLeft = parentWidget()->mapFromGlobal(anchor->mapToGlobal(QPoint(0, 0)));
    QPoint targetPos(anchorLeft.x() - sideInset, topY);

    QPoint globalPos = parentWidget()->mapToGlobal(targetPos);
    QScreen* screen = QApplication::screenAt(globalPos);
    if (screen) {
        QRect screenRect = screen->availableGeometry();
        if (globalPos.x() + width() > screenRect.right()) {
            targetPos.setX(targetPos.x() - (globalPos.x() + width() - screenRect.right()));
        }
        const int leftLimit = parentWidget()->mapFromGlobal(QPoint(screenRect.left(), 0)).x();
        if (targetPos.x() < leftLimit) {
            targetPos.setX(leftLimit);
        }
    }
    return targetPos;
}

void MenuPopup::ensurePosAnim()
{
    if (!m_posAnim) {
        m_posAnim = new QPropertyAnimation(this, "pos", this);
        m_posAnim->setEasingCurve(QEasingCurve::OutCubic);
        // Update overlay mask on every frame so popup isn't clipped during fast switching
        connect(m_posAnim, &QPropertyAnimation::valueChanged, this, [this]() {
            if (m_isVisible || m_isHiding)
                emit contentChanged();
        });
    }
}

void MenuPopup::ensureHeightAnim()
{
    if (!m_heightAnim) {
        m_heightAnim = new QPropertyAnimation(this, "displayHeight", this);
        m_heightAnim->setEasingCurve(QEasingCurve::OutCubic);
    }
}

QRectF MenuPopup::attachedBodyRect() const
{
    const int shadowExtent = attachedShadowExtentPx();
    const int shadowSideExtent = attachedShadowSideExtentPx();
    if (height() <= shadowExtent + 1 || width() <= shadowSideExtent * 2 + 1) {
        return {};
    }
    return QRectF(rect()).adjusted(
        shadowSideExtent, 0.0, -shadowSideExtent - 0.5, -shadowExtent - 0.5);
}

void MenuPopup::animateToPosition(const QPoint& targetPos)
{
    ensurePosAnim();
    m_posAnim->stop();
    m_posAnim->setDuration(SLIDE_DURATION);
    m_posAnim->setStartValue(pos());
    m_posAnim->setEndValue(targetPos);
    m_posAnim->start();
}

void MenuPopup::showSubmenu(QWidget* anchor, const QList<MenuItem>& items)
{
    if (items.isEmpty())
        return;

    ensureSubmenuPopup();
    m_submenuPopup->setItems(items);
    QPoint targetPos = calculateSubmenuPosition(anchor);
    m_submenuPopup->showAt(targetPos, m_submenuPopup->m_submenuSlideFromLeft);
    emit contentChanged();
}

void MenuPopup::hideSubmenu(bool immediate)
{
    if (m_submenuPopup && m_submenuPopup->isPopupVisible()) {
        if (immediate) {
            m_submenuAnchor = nullptr;
            m_pendingSubmenuAnchor = nullptr;
            m_pendingSubmenuItems.clear();
            m_submenuPopup->forceHide();
        } else {
            m_submenuPopup->hidePopup();
        }
    }
}

// ----------------------------------------------------------------------------
// Submenu hover arbitration: diagonal-aim detection + close debounce.
//
// The cursor often travels diagonally from a parent item toward its submenu,
// crossing a sibling item on the way. Naively, that sibling's hover would tear
// down the open submenu. Instead we detect when the cursor is "aiming" at the
// open submenu (its recent movement vector points into the submenu's near edge)
// and defer any switch/close until it settles. A short debounce also keeps the
// submenu alive across a momentary leave.
// ----------------------------------------------------------------------------

void MenuPopup::startSubmenuAimPoll()
{
    if (m_isSubmenu)
        return;
    if (!m_submenuAimTimer) {
        m_submenuAimTimer = new QTimer(this);
        m_submenuAimTimer->setInterval(SUBMENU_AIM_POLL_MS);
        connect(m_submenuAimTimer, &QTimer::timeout, this, [this]() {
            m_prevMousePos = m_lastMousePos;
            m_lastMousePos = QCursor::pos();
            reconcileSubmenu();
        });
    }
    m_prevMousePos = m_lastMousePos = QCursor::pos();
    m_submenuCloseArmedAt = -1;
    m_submenuAimTimer->start();
}

void MenuPopup::stopSubmenuAimPoll()
{
    if (m_submenuAimTimer) {
        m_submenuAimTimer->stop();
    }
}

QRect MenuPopup::submenuGlobalRect() const
{
    if (!m_submenuPopup)
        return {};
    QRect r(m_submenuPopup->mapToGlobal(QPoint(0, 0)), m_submenuPopup->size());
    // Widen toward the parent so the connecting gap counts as "inside" — crossing
    // the bridge between parent and submenu must not trigger a close.
    return r.adjusted(-(SUBMENU_GAP + 4), -2, SUBMENU_GAP + 4, 2);
}

bool MenuPopup::isMouseAimingAtSubmenu() const
{
    if (!m_submenuPopup || !m_submenuPopup->isPopupVisible())
        return false;

    // Require some actual movement; a settled cursor is not "aiming".
    if ((m_lastMousePos - m_prevMousePos).manhattanLength() < SUBMENU_AIM_MIN_MOVE) {
        return false;
    }

    const QRect sub(m_submenuPopup->mapToGlobal(QPoint(0, 0)), m_submenuPopup->size());
    QPointF nearTop, nearBottom;
    if (m_submenuPopup->m_submenuSlideFromLeft) {
        // Submenu sits to the right of the parent → near edge is its left side.
        nearTop = QPointF(sub.left(), sub.top());
        nearBottom = QPointF(sub.left(), sub.bottom());
    } else {
        nearTop = QPointF(sub.right(), sub.top());
        nearBottom = QPointF(sub.right(), sub.bottom());
    }

    // Cone from the previous cursor position to the submenu's near edge. If the
    // current position lands inside that cone, the cursor is heading for the
    // submenu and we should keep it open.
    QPolygonF cone;
    cone << QPointF(m_prevMousePos) << nearTop << nearBottom;
    return cone.containsPoint(QPointF(m_lastMousePos), Qt::OddEvenFill);
}

void MenuPopup::reconcileSubmenu()
{
    // Only the top-level popup arbitrates, and only while it is actually up — never
    // resurrect a submenu while the whole menu is closing.
    if (m_isSubmenu || !m_isVisible || m_isHiding)
        return;

    const bool subVisible
        = m_submenuPopup && m_submenuPopup->isPopupVisible() && !m_submenuPopup->isHiding();
    const QPoint gp = QCursor::pos();

    // Cursor parked over the open submenu (or the bridge to it) → keep it open.
    if (subVisible && submenuGlobalRect().contains(gp)) {
        m_submenuCloseArmedAt = -1;
        return;
    }

    MenuItemWidget* desired
        = (m_hoveredItem && m_hoveredItem->hasSubmenu()) ? m_hoveredItem.data() : nullptr;

    // Already in the desired state.
    if (desired == m_submenuAnchor) {
        m_submenuCloseArmedAt = -1;
        return;
    }

    // Mid-flight toward the open submenu → defer any change until the cursor settles.
    if (subVisible && isMouseAimingAtSubmenu()) {
        m_submenuCloseArmedAt = -1;
        return;
    }

    if (desired) {
        m_submenuCloseArmedAt = -1;
        commitSubmenuChange(desired);
        return;
    }

    // Closing: debounce so a momentary leave doesn't tear the submenu down.
    if (!subVisible) {
        m_submenuCloseArmedAt = -1;
        return;
    }
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    if (m_submenuCloseArmedAt < 0) {
        m_submenuCloseArmedAt = now;
        return;
    }
    if (now - m_submenuCloseArmedAt >= SUBMENU_CLOSE_DELAY_MS) {
        m_submenuCloseArmedAt = -1;
        commitSubmenuChange(nullptr);
    }
}

void MenuPopup::commitSubmenuChange(MenuItemWidget* desired)
{
    m_pendingSubmenuAnchor = nullptr;
    m_pendingSubmenuItems.clear();

    if (!desired) {
        m_submenuAnchor = nullptr;
        hideSubmenu(false);
        return;
    }

    if (m_submenuAnchor && m_submenuPopup && m_submenuPopup->isPopupVisible()
        && !m_submenuPopup->isHiding()) {
        m_submenuAnchor = desired;
        switchSubmenuTo(desired, desired->submenuItems()); // smooth slide + resize
    } else {
        m_submenuAnchor = desired;
        showSubmenu(desired, desired->submenuItems());
    }
}

void MenuPopup::showAt(const QPoint& pos, bool slideFromLeft)
{
    if (!parentWidget())
        return;

    m_isVisible = true;
    m_isHiding = false; // May be re-shown mid-hide (fast submenu re-hover)
    m_isAnimatingHeight = false;
    m_displayHeight = m_targetHeight;

    QPoint startPos = pos;
    if (m_isSubmenu && slideFromLeft) {
        startPos.setX(pos.x() - SLIDE_OFFSET);
    } else if (m_isSubmenu && !slideFromLeft) {
        startPos.setX(pos.x() + SLIDE_OFFSET);
    }

    move(startPos);
    show();
    raise();

    if (m_isSubmenu && startPos != pos) {
        ensurePosAnim();
        m_posAnim->stop();
        m_posAnim->setDuration(SLIDE_DURATION);
        m_posAnim->setStartValue(startPos);
        m_posAnim->setEndValue(pos);
        m_posAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_posAnim->start();
    }

    startShowAnimation();
    emit shown();
}

void MenuPopup::switchTo(QWidget* anchor, const QList<MenuItem>& items)
{
    if (!anchor || !parentWidget() || m_isHiding)
        return;

    hideSubmenu(true); // Immediate when switching menus
    m_isVisible = true;

    // Save current visual height before rebuild changes it
    int oldDisplayH = m_displayHeight > 0 ? m_displayHeight : height();

    setItems(items);
    // rebuildItems() set fixedSize to full target — needed for correct position calculation

    QPoint targetPos = calculatePosition(anchor);
    animateSwitchTo(targetPos, oldDisplayH);
}

void MenuPopup::animateSwitchTo(const QPoint& targetPos, int oldDisplayH)
{
    // Revert visual height to old value, then animate position + height to new target.
    m_isAnimatingHeight = true;
    m_displayHeight = oldDisplayH;
    setFixedHeight(oldDisplayH);

    animateToPosition(targetPos);

    ensureHeightAnim();
    m_heightAnim->stop();
    m_heightAnim->setDuration(SLIDE_DURATION);
    m_heightAnim->setStartValue(oldDisplayH);
    m_heightAnim->setEndValue(m_targetHeight);
    m_heightAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_heightAnim->start();

    emit contentChanged();
}

void MenuPopup::switchSubmenuTo(QWidget* anchor, const QList<MenuItem>& items)
{
    ensureSubmenuPopup();
    if (!m_submenuPopup)
        return;

    // If not actually visible yet, fall back to a fresh slide-in.
    if (!m_submenuPopup->isPopupVisible() || m_submenuPopup->isHiding()) {
        showSubmenu(anchor, items);
        return;
    }

    // Capture the old visual height before setItems rebuilds to the new target.
    MenuPopup* sub = m_submenuPopup;
    const int oldDisplayH = sub->m_displayHeight > 0 ? sub->m_displayHeight : sub->height();

    sub->m_isVisible = true;
    sub->setItems(items); // updates width/height to the new menu's full size

    // Position is computed against the new width, then we animate pos + height.
    const QPoint targetPos = calculateSubmenuPosition(anchor);
    sub->animateSwitchTo(targetPos, oldDisplayH);

    emit contentChanged();
}

void MenuPopup::showBelow(QWidget* anchor, bool slideFromTop)
{
    if (!anchor || !parentWidget())
        return;

    m_isVisible = true;
    // Size is set by setItems/rebuildItems before this is called; ensure full
    // height so calculatePosition uses the final width for screen clamping.
    m_displayHeight = m_targetHeight;
    setFixedHeight(m_targetHeight);

    QPoint targetPos = calculatePosition(anchor);
    move(targetPos);
    show();
    raise();

    if (slideFromTop) {
        // Reveal by growing the body downward from the TopBar seam (MessagePopup feel).
        ensureHeightAnim();
        m_isAnimatingHeight = true;
        m_displayHeight = 0;
        setFixedHeight(0);
        m_heightAnim->stop();
        m_heightAnim->setDuration(SLIDE_DURATION);
        m_heightAnim->setStartValue(0);
        m_heightAnim->setEndValue(m_targetHeight);
        m_heightAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_heightAnim->start();
    }

    startShowAnimation();
    emit shown();
}

void MenuPopup::hidePopup()
{
    if (!m_isVisible)
        return;

    stopSubmenuAimPoll();
    m_submenuCloseArmedAt = -1;
    m_hoveredItem = nullptr;
    m_pendingSubmenuAnchor = nullptr;
    m_pendingSubmenuItems.clear();
    hideSubmenu(false); // Animate submenu close
    emit aboutToHide();
    startHideAnimation();
}

void MenuPopup::forceHide()
{
    stopSubmenuAimPoll();
    m_submenuCloseArmedAt = -1;
    m_hoveredItem = nullptr;
    hideSubmenu(true);
    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
    m_opacityAnim->stop();
    if (m_posAnim)
        m_posAnim->stop();
    if (m_heightAnim)
        m_heightAnim->stop();
    m_isVisible = false;
    m_isHiding = false;
    m_isAnimatingHeight = false;
    setPopupOpacity(0.0);
    hide();
    emit hidden();
}

void MenuPopup::setPopupOpacity(qreal opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(m_opacity);
    }
    update();

    if (qFuzzyIsNull(m_opacity) && !m_isVisible && !m_isHiding) {
        hide();
        emit hidden();
    }
}

void MenuPopup::setDisplayHeight(int h)
{
    m_displayHeight = h;
    setFixedHeight(h);
    update();

    if (h == m_targetHeight) {
        m_isAnimatingHeight = false;
        // Restore cached height guard to the final target
        m_cachedHeight = m_targetHeight;
    }

    if (isVisible() && m_isVisible) {
        emit contentChanged();
    }
}

void MenuPopup::leaveEvent(QEvent* event)
{
    QWidget::leaveEvent(event);
    if (m_isVisible) {
        emit mouseLeft();
    }
}

void MenuPopup::resizeEvent(QResizeEvent* event)
{
    // During rebuild, ignore all resize events — rebuildItems sets the final size via setFixedSize
    if (m_isRebuilding) {
        return;
    }

    // During height animation, allow all size changes (driven by setDisplayHeight)
    if (m_isAnimatingHeight) {
        QWidget::resizeEvent(event);
        return;
    }

    // Guard: prevent external collapse (e.g. layout recalc) when we have a known target size
    if (m_cachedHeight > 0 && event->size().height() < m_cachedHeight) {
        setFixedSize(
            m_cachedWidth > 0 ? m_cachedWidth : qMax(event->size().width(), 1), m_cachedHeight);
        return;
    }

    QWidget::resizeEvent(event);
    if (isVisible() && m_isVisible && !m_isHiding) {
        emit contentChanged();
    }
}

void MenuPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();

    if (m_isSubmenu) {
        // Submenu: plain floating rounded rect (it hangs beside its parent, not the bar).
        QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);
        constexpr int radius = 8;

        painter.setPen(Qt::NoPen);
        painter.setBrush(colors.surface);
        painter.drawRoundedRect(rect, radius, radius);

        QColor borderTop = colors.border;
        ruwa::ui::painting::drawGradientBorder(painter, rect, radius, borderTop,
            ruwa::ui::core::ThemeColors::withAlpha(borderTop, borderTop.alpha() / 2));
        return;
    }

    // Top-level popup: attached-to-TopBar silhouette shared with MessagePopup —
    // flat flared top that merges with the bar seam, rounded bottom, soft shadow.
    const QRectF body = attachedBodyRect();
    if (!body.isValid()) {
        return;
    }

    const int outerCornerRadius = attachedOuterCornerRadiusPx();
    const int shadowExtent = attachedShadowExtentPx();
    const int shadowSideExtent = attachedShadowSideExtentPx();
    constexpr int radius = ruwa::ui::painting::kAttachedCornerRadius;

    const QPainterPath shape
        = ruwa::ui::painting::attachedPopupPath(body, outerCornerRadius, radius);

    // The visible body is inset by outerCornerRadius on the sides (see attachedPopupPath),
    // so the shadow must hug that inset rect — not the raw body — to avoid a side gap.
    const QRectF shadowBody = body.adjusted(outerCornerRadius, 0.0, -outerCornerRadius, 0.0);
    ruwa::ui::painting::drawAttachedPopupShadow(
        painter, shadowBody, shadowSideExtent, shadowExtent, colors.shadow(255), colors.isDark);

    // Fill (matches MessagePopup so both attached surfaces read identically)
    painter.setPen(Qt::NoPen);
    QLinearGradient fillGradient(body.topLeft(), body.bottomLeft());
    fillGradient.setColorAt(0.0, colors.surface);
    fillGradient.setColorAt(
        1.0, ruwa::ui::core::ThemeColors::adjustBrightness(colors.surface, 100.0 / 102));
    painter.setBrush(fillGradient);
    painter.drawPath(shape);

    // Border (top edge omitted — it merges with the seam)
    const QRectF borderRect = body.adjusted(0.5, 0.0, -0.5, -0.5);
    const QPainterPath borderPath
        = ruwa::ui::painting::attachedPopupBorderPath(borderRect, outerCornerRadius, radius - 0.5);
    QColor borderTop = colors.border;
    QLinearGradient borderGradient(borderRect.topLeft(), borderRect.bottomLeft());
    borderGradient.setColorAt(0.0, borderTop);
    borderGradient.setColorAt(
        1.0, ruwa::ui::core::ThemeColors::withAlpha(borderTop, borderTop.alpha() / 2));
    QPen borderPen;
    borderPen.setBrush(borderGradient);
    borderPen.setWidthF(1.0);
    borderPen.setCosmetic(true);
    borderPen.setCapStyle(Qt::SquareCap);
    borderPen.setJoinStyle(Qt::RoundJoin);
    painter.setPen(borderPen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(borderPath);
}

void MenuPopup::startShowAnimation()
{
    // Disconnect any hide-finished handler from previous hide cycle
    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);

    m_opacityAnim->stop();
    m_opacityAnim->setDuration(SHOW_DURATION);
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(1.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_opacityAnim->start();
}

void MenuPopup::startHideAnimation()
{
    m_isVisible = false;
    m_isHiding = true;

    m_opacityAnim->stop();
    ensurePosAnim();
    m_posAnim->stop();
    if (m_heightAnim)
        m_heightAnim->stop();

    m_opacityAnim->setDuration(SLIDE_DURATION);
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(0.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    if (m_isSubmenu) {
        // Submenu: slide horizontally back toward its parent while fading.
        m_isAnimatingHeight = false;
        const QPoint currentPos = pos();
        const QPoint endPos = m_submenuSlideFromLeft
            ? currentPos + QPoint(SLIDE_OFFSET, 0) // Slide out to the right
            : currentPos - QPoint(SLIDE_OFFSET, 0); // Slide out to the left
        m_posAnim->setDuration(SLIDE_DURATION);
        m_posAnim->setStartValue(currentPos);
        m_posAnim->setEndValue(endPos);
        m_posAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_posAnim->start();
    } else {
        // Attached popup: retract the body upward into the TopBar seam (no move),
        // mirroring the height-reveal it opened with.
        ensureHeightAnim();
        m_isAnimatingHeight = true;
        m_heightAnim->stop();
        m_heightAnim->setDuration(SLIDE_DURATION);
        m_heightAnim->setStartValue(m_displayHeight > 0 ? m_displayHeight : height());
        m_heightAnim->setEndValue(0);
        m_heightAnim->setEasingCurve(QEasingCurve::InCubic);
        m_heightAnim->start();
    }

    // Disconnect any previous hide-finished connection, then connect for this hide cycle
    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
    connect(m_opacityAnim, &QPropertyAnimation::finished, this, [this]() {
        if (m_isHiding) {
            m_isHiding = false;
            m_isAnimatingHeight = false;
            if (m_heightAnim)
                m_heightAnim->stop();
            hide();
            emit hidden();
        }
    });

    m_opacityAnim->start();
}

} // namespace ruwa::ui::widgets
