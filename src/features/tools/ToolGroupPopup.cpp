// SPDX-License-Identifier: MPL-2.0

#include "ToolGroupPopup.h"

#include "features/theme/manager/ThemeColors.h"
#include "features/theme/manager/ThemeManager.h"
#include "shared/widgets/ToolButton.h"
#include "shared/style/PaintingUtils.h"

#include <QApplication>
#include <QContextMenuEvent>
#include <QCursor>
#include <QEvent>
#include <QBoxLayout>
#include <QGraphicsOpacityEffect>
#include <QKeyEvent>
#include <QLinearGradient>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QScreen>
#include <QSignalBlocker>
#include <utility>

namespace ruwa::ui::workspace {

namespace {

QPoint slideOffsetForSide(ToolGroupPopup::Side side, int distance)
{
    switch (side) {
    case ToolGroupPopup::Side::Right:
        return QPoint(-distance, 0);
    case ToolGroupPopup::Side::Left:
        return QPoint(distance, 0);
    case ToolGroupPopup::Side::Bottom:
        return QPoint(0, -distance);
    case ToolGroupPopup::Side::Top:
        return QPoint(0, distance);
    }

    return {};
}

} // namespace

ToolGroupPopup::ToolGroupPopup(QWidget* parent)
    : QWidget(parent)
{
    setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_NoSystemBackground);
    setAttribute(Qt::WA_ShowWithoutActivating);
    setMouseTracking(true);

    m_layout = new QBoxLayout(QBoxLayout::TopToBottom, this);
    m_layout->setContentsMargins(6, 6, 6, 6);
    m_layout->setSpacing(4);

    m_opacityEffect = new QGraphicsOpacityEffect(this);
    m_opacityEffect->setOpacity(0.0);
    setGraphicsEffect(m_opacityEffect);

    m_opacityAnim = new QPropertyAnimation(this, "popupOpacity", this);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_posAnim = new QPropertyAnimation(this, "pos", this);
    m_posAnim->setEasingCurve(QEasingCurve::OutCubic);

    connect(&ruwa::ui::core::ThemeManager::instance(), &ruwa::ui::core::ThemeManager::themeChanged,
        this, [this]() {
            updateIcons();
            update();
        });

    hide();
}

ToolGroupPopup::~ToolGroupPopup()
{
    detachGlobalFilters();
}

void ToolGroupPopup::setItems(const QList<Item>& items)
{
    m_items = items;
    rebuildButtons();
}

void ToolGroupPopup::setCurrentToolId(int toolId)
{
    for (int i = 0; i < m_buttons.size(); ++i) {
        const bool isCurrent = i < m_items.size() && m_items[i].toolId == toolId;
        // Block the toggled signal so it doesn't trigger setActive()'s transition
        // animation, then snap the active visual state. The current tool is already
        // active when the popup opens, so it must appear selected without animating in.
        const QSignalBlocker blocker(m_buttons[i]);
        m_buttons[i]->setChecked(isCurrent);
        m_buttons[i]->setActiveImmediate(isCurrent);
    }
}

void ToolGroupPopup::setSide(Side side)
{
    m_side = side;
}

void ToolGroupPopup::setLayoutMode(LayoutMode mode)
{
    if (m_layoutMode == mode || !m_layout) {
        m_layoutMode = mode;
        return;
    }

    m_layoutMode = mode;
    m_layout->setDirection(
        m_layoutMode == LayoutMode::Horizontal ? QBoxLayout::LeftToRight : QBoxLayout::TopToBottom);
    rebuildButtons();
}

void ToolGroupPopup::showFor(QWidget* anchor, bool animate)
{
    if (!anchor) {
        return;
    }

    QWidget* hostWindow = anchor->window();
    if (!hostWindow) {
        return;
    }

    if (parentWidget() != hostWindow) {
        setParent(hostWindow, Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    }
    detachGlobalFilters();

    m_anchor = anchor;
    const QPoint targetPos = calculateTargetPosition(anchor);
    const QPoint startPos
        = targetPos + (animate ? slideOffsetForSide(m_side, kSlideOffset) : QPoint());

    m_isVisible = true;
    m_isHiding = false;
    m_releaseSelectArmed = (QApplication::mouseButtons() & Qt::LeftButton);
    clearHoveredButton();

    move(startPos);
    show();
    raise();
    qApp->installEventFilter(this);
    hostWindow->installEventFilter(this);

    if (m_releaseSelectArmed) {
        updateHoveredButton(QCursor::pos());
    }

    startShowAnimation(animate);

    m_posAnim->stop();
    m_posAnim->setDuration(kSlideDuration);
    m_posAnim->setStartValue(startPos);
    m_posAnim->setEndValue(targetPos);
    if (animate) {
        m_posAnim->start();
    } else {
        move(targetPos);
    }
}

void ToolGroupPopup::hideAnimated()
{
    if (!isVisible() || m_isHiding) {
        return;
    }
    startHideAnimation();
}

void ToolGroupPopup::hideImmediate()
{
    m_opacityAnim->stop();
    m_posAnim->stop();
    m_isVisible = false;
    m_isHiding = false;
    m_releaseSelectArmed = false;
    detachGlobalFilters();
    clearHoveredButton();
    setPopupOpacity(0.0);
    hide();
    emit hidden();
}

void ToolGroupPopup::setPopupOpacity(qreal opacity)
{
    m_opacity = qBound(0.0, opacity, 1.0);
    if (m_opacityEffect) {
        m_opacityEffect->setOpacity(m_opacity);
    }
    update();
}

bool ToolGroupPopup::eventFilter(QObject* watched, QEvent* event)
{
    if (!(m_isVisible || m_isHiding)) {
        return QWidget::eventFilter(watched, event);
    }

    if (watched == parentWidget()
        && (event->type() == QEvent::Resize || event->type() == QEvent::Move)) {
        hideImmediate();
        return QWidget::eventFilter(watched, event);
    }

    switch (event->type()) {
    case QEvent::MouseMove: {
        if (!m_releaseSelectArmed) {
            break;
        }

        auto* mouseEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mouseEvent) {
            break;
        }

        updateHoveredButton(mouseEvent->globalPosition().toPoint());
        break;
    }
    case QEvent::MouseButtonRelease: {
        auto* mouseEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mouseEvent) {
            break;
        }

        if (m_releaseSelectArmed && mouseEvent->button() == Qt::LeftButton) {
            const QPoint globalPos = mouseEvent->globalPosition().toPoint();
            updateHoveredButton(globalPos);
            const bool selected = triggerHoveredButtonSelection();
            m_releaseSelectArmed = false;
            if (!selected) {
                const QWidget* targetWidget = QApplication::widgetAt(globalPos);
                const bool releasedOverExternalToolButton = targetWidget
                    && qobject_cast<const ToolButton*>(targetWidget) && targetWidget != this
                    && !isAncestorOf(targetWidget);

                if (releasedOverExternalToolButton) {
                    clearHoveredButton();
                    break;
                }

                clearHoveredButton();
                hideAnimated();
            }
        }
        break;
    }
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonDblClick: {
        auto* mouseEvent = dynamic_cast<QMouseEvent*>(event);
        if (!mouseEvent) {
            m_releaseSelectArmed = false;
            clearHoveredButton();
            hideAnimated();
            break;
        }
        const QPoint globalPos = mouseEvent->globalPosition().toPoint();
        const QWidget* targetWidget = QApplication::widgetAt(globalPos);
        if (targetWidget && (targetWidget == this || isAncestorOf(targetWidget))) {
            break;
        }
        m_releaseSelectArmed = false;
        clearHoveredButton();
        hideAnimated();
        break;
    }
    case QEvent::ContextMenu: {
        auto* contextMenuEvent = static_cast<QContextMenuEvent*>(event);
        const QPoint globalPos = contextMenuEvent->globalPos();
        const QWidget* targetWidget = QApplication::widgetAt(globalPos);
        const bool overPopup = targetWidget && (targetWidget == this || isAncestorOf(targetWidget));
        const bool overAnchor = m_anchor && targetWidget
            && (targetWidget == m_anchor || m_anchor->isAncestorOf(targetWidget));
        if (!overPopup && !overAnchor) {
            m_releaseSelectArmed = false;
            clearHoveredButton();
            hideAnimated();
        }
        break;
    }
    case QEvent::KeyPress: {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            m_releaseSelectArmed = false;
            clearHoveredButton();
            hideAnimated();
        }
        break;
    }
    default:
        break;
    }

    return QWidget::eventFilter(watched, event);
}

ToolButton* ToolGroupPopup::buttonAtGlobalPos(const QPoint& globalPos) const
{
    if (!isVisible()) {
        return nullptr;
    }

    const QPoint localPopupPos = mapFromGlobal(globalPos);
    if (!rect().contains(localPopupPos)) {
        return nullptr;
    }

    for (ToolButton* button : m_buttons) {
        if (!button || !button->isVisible()) {
            continue;
        }

        const QPoint localButtonPos = button->mapFromGlobal(globalPos);
        if (button->rect().contains(localButtonPos)) {
            return button;
        }
    }

    return nullptr;
}

void ToolGroupPopup::updateHoveredButton(const QPoint& globalPos)
{
    ToolButton* hoveredButton = buttonAtGlobalPos(globalPos);
    if (hoveredButton == m_hoveredButton) {
        return;
    }

    if (m_hoveredButton) {
        m_hoveredButton->setHovered(false);
    }

    m_hoveredButton = hoveredButton;

    if (m_hoveredButton) {
        m_hoveredButton->setHovered(true);
    }
}

void ToolGroupPopup::clearHoveredButton()
{
    if (m_hoveredButton) {
        m_hoveredButton->setHovered(false);
    }

    m_hoveredButton = nullptr;
}

bool ToolGroupPopup::triggerHoveredButtonSelection()
{
    if (!m_hoveredButton) {
        return false;
    }

    const int buttonIndex = m_buttons.indexOf(m_hoveredButton);
    if (buttonIndex < 0 || buttonIndex >= m_items.size()) {
        return false;
    }

    emit toolSelected(m_items[buttonIndex].toolId);
    hideAnimated();
    return true;
}

void ToolGroupPopup::paintEvent(QPaintEvent* event)
{
    Q_UNUSED(event);

    QPainter painter(this);
    painter.setRenderHint(QPainter::Antialiasing);

    const auto& colors = ruwa::ui::core::ThemeManager::instance().colors();
    QRectF rect = this->rect().adjusted(0.5, 0.5, -0.5, -0.5);
    constexpr qreal radius = 8.0;

    painter.setPen(Qt::NoPen);
    painter.setBrush(colors.surfaceElevated());
    painter.drawRoundedRect(rect, radius, radius);

    {
        QColor borderTop = colors.borderSubtle();
        ruwa::ui::painting::drawGradientBorder(painter, rect, radius, borderTop,
            ruwa::ui::core::ThemeColors::withAlpha(borderTop, borderTop.alpha() / 2));
    }
}

void ToolGroupPopup::rebuildButtons()
{
    clearHoveredButton();

    for (ToolButton* button : std::as_const(m_buttons)) {
        m_layout->removeWidget(button);
        delete button;
    }
    m_buttons.clear();

    for (const Item& item : std::as_const(m_items)) {
        auto* button = new ToolButton(this);
        button->setToolTip(item.tooltip);
        button->setIconType(item.iconType);
        button->setCheckable(true);
        button->setHasGroupIndicator(false);

        connect(button, &QAbstractButton::clicked, this, [this, item]() {
            emit toolSelected(item.toolId);
            hideAnimated();
        });

        m_layout->addWidget(button);
        m_buttons.append(button);
    }

    m_layout->activate();
    const QSize layoutHint = m_layout->sizeHint();
    resize(layoutHint.width(), layoutHint.height());
    updateIcons();
}

QPoint ToolGroupPopup::calculateTargetPosition(QWidget* anchor) const
{
    if (!anchor) {
        return {};
    }

    const int popupWidth = width() > 0 ? width() : sizeHint().width();
    const int popupHeight = height() > 0 ? height() : sizeHint().height();
    QPoint targetPos;

    switch (m_side) {
    case Side::Right: {
        targetPos = anchor->mapToGlobal(QPoint(anchor->width(), 0));
        targetPos.rx() += kPopupGap;
        break;
    }
    case Side::Left: {
        targetPos = anchor->mapToGlobal(QPoint(0, 0));
        targetPos.rx() -= popupWidth + kPopupGap;
        break;
    }
    case Side::Bottom: {
        targetPos = anchor->mapToGlobal(QPoint(anchor->width() / 2, anchor->height()));
        targetPos.rx() -= popupWidth / 2;
        targetPos.ry() += kPopupGap;
        break;
    }
    case Side::Top: {
        targetPos = anchor->mapToGlobal(QPoint(anchor->width() / 2, 0));
        targetPos.rx() -= popupWidth / 2;
        targetPos.ry() -= popupHeight + kPopupGap;
        break;
    }
    }

    if (QScreen* screen = QApplication::screenAt(anchor->mapToGlobal(anchor->rect().center()))) {
        const QRect screenRect = screen->availableGeometry();
        const int minGlobalX = screenRect.left() + 8;
        const int maxGlobalX = screenRect.right() - popupWidth - 7;
        const int minGlobalY = screenRect.top() + 8;
        const int maxGlobalY = screenRect.bottom() - popupHeight - 7;

        if (maxGlobalX >= minGlobalX) {
            targetPos.setX(qBound(minGlobalX, targetPos.x(), maxGlobalX));
        }
        if (maxGlobalY >= minGlobalY) {
            targetPos.setY(qBound(minGlobalY, targetPos.y(), maxGlobalY));
        }
    }

    return targetPos;
}

void ToolGroupPopup::updateIcons()
{
    const auto& theme = ruwa::ui::core::ThemeManager::instance();
    for (int i = 0; i < m_buttons.size() && i < m_items.size(); ++i) {
        m_buttons[i]->setIcon(theme.icons().getIcon(m_items[i].iconType));
    }
}

void ToolGroupPopup::startShowAnimation(bool animateSlide)
{
    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);

    m_opacityAnim->stop();
    m_opacityAnim->setDuration(kShowDuration);
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(1.0);
    m_opacityAnim->start();

    if (!animateSlide) {
        m_posAnim->stop();
    }
}

void ToolGroupPopup::startHideAnimation()
{
    m_isVisible = false;
    m_isHiding = true;

    m_opacityAnim->stop();
    m_posAnim->stop();

    const QPoint currentPos = pos();
    const QPoint endPos = currentPos + slideOffsetForSide(m_side, kSlideOffset);

    m_opacityAnim->setDuration(kHideDuration);
    m_opacityAnim->setStartValue(m_opacity);
    m_opacityAnim->setEndValue(0.0);
    m_opacityAnim->setEasingCurve(QEasingCurve::OutCubic);

    m_posAnim->setDuration(kHideDuration);
    m_posAnim->setStartValue(currentPos);
    m_posAnim->setEndValue(endPos);
    m_posAnim->setEasingCurve(QEasingCurve::OutCubic);

    disconnect(m_opacityAnim, &QPropertyAnimation::finished, this, nullptr);
    connect(m_opacityAnim, &QPropertyAnimation::finished, this, [this]() {
        if (!m_isHiding || m_isVisible) {
            return;
        }
        m_isHiding = false;
        clearHoveredButton();
        detachGlobalFilters();
        hide();
        emit hidden();
    });

    m_opacityAnim->start();
    m_posAnim->start();
}

void ToolGroupPopup::detachGlobalFilters()
{
    if (qApp) {
        qApp->removeEventFilter(this);
    }
    if (parentWidget()) {
        parentWidget()->removeEventFilter(this);
    }
}

} // namespace ruwa::ui::workspace
