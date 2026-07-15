// SPDX-License-Identifier: MPL-2.0

// BrushPackOverlay.cpp
#include "BrushPackOverlay.h"
#include "features/brush/ui/BrushPackPanel.h"

#include <QWidget>
#include <QVariant>
#include <QApplication>
#include <QMouseEvent>
#include <QKeyEvent>
#include <QTabletEvent>
#include <QResizeEvent>
#include <QTimer>
#include <QCursor>

namespace ruwa::ui::widgets {

BrushPackOverlay::BrushPackOverlay(QWidget* container)
    : QObject(container)
    , m_container(container)
{
    setupPanel();
    setupAnimations();

    m_container->installEventFilter(this);
}

BrushPackOverlay::~BrushPackOverlay() = default;

void BrushPackOverlay::setupPanel()
{
    m_panel = new BrushPackPanel(m_container);
    m_panel->setFocusPolicy(Qt::StrongFocus);
    m_panel->setMouseTracking(true);
    m_panel->hide();

    connect(m_panel, &BrushPackPanel::positionChanged, this,
        [this](const QPoint&) { m_userMovedPanel = true; });
}

void BrushPackOverlay::setupAnimations()
{
    m_posAnimation = new QPropertyAnimation(this, "panelPos", this);
    m_posAnimation->setDuration(PositionAnimationDuration);
    m_posAnimation->setEasingCurve(QEasingCurve::InOutCubic);
}

QPoint BrushPackOverlay::panelPos() const
{
    return m_panel ? m_panel->pos() : QPoint();
}

void BrushPackOverlay::setPanelPos(const QPoint& pos)
{
    if (m_panel) {
        m_panel->move(pos);
    }
}

void BrushPackOverlay::onSourceWidgetMoved(QWidget* sourceWidget)
{
    if (!m_panel || !m_panel->isVisible() || m_isShowing || m_isHiding)
        return;
    if (m_userMovedPanel)
        return;
    if (m_sourceWidget != sourceWidget)
        return;
    QPoint newPos = calculatePanelPosition(sourceWidget);
    if (newPos != m_panel->pos()) {
        m_panel->move(newPos);
    }
}

void BrushPackOverlay::showPanel(QWidget* sourceWidget, const QPoint* slideFromPos)
{
    if (!m_panel || !m_container)
        return;

    // Don't open if container is too small for the panel
    if (!canShowPanel())
        return;

    // Toggle if already visible for same source
    if (m_panel->isVisible() && !m_isHiding && m_sourceWidget == sourceWidget
        && sourceWidget != nullptr) {
        if (m_userMovedPanel) {
            // Reattach: morph from compact back to full size and slide to correct position
            m_userMovedPanel = false;
            m_isShowing = true; // prevent close-on-click-outside during morph

            const QSize fullSize = m_panel->fullPanelSize();
            const QPoint targetPos = calculatePanelPosition(sourceWidget, fullSize);
            animatePanelTo(targetPos);
            m_panel->setCompactMode(false, true);

            QTimer::singleShot(PositionAnimationDuration, this, [this]() { m_isShowing = false; });
            return;
        }
        hidePanel();
        return;
    }

    // Switch source if already visible
    if (m_panel->isVisible() && !m_isHiding) {
        m_sourceWidget = sourceWidget;
        QPoint targetPos = calculatePanelPosition(sourceWidget);
        animatePanelTo(targetPos);
        return;
    }

    if (m_isShowing || m_isHiding) {
        forceHide();
    }

    m_isShowing = true;
    m_isHiding = false;
    m_userMovedPanel = false; // Reset when opening via brushpack button
    m_sourceWidget = sourceWidget;
    m_panel->setCompactMode(false, false); // Always show in full size

    QPoint targetPos = calculatePanelPosition(sourceWidget);

    // Start position: slide horizontally
    QPoint startPos = targetPos;
    if (slideFromPos && sourceWidget && sourceWidget->isVisible()) {
        // Reopen case: small offset from target, but direction from where user had the panel
        QPoint sourceInContainer;
        if (m_container->isAncestorOf(sourceWidget)) {
            sourceInContainer = sourceWidget->mapTo(m_container, QPoint(0, 0));
        } else {
            sourceInContainer = m_container->mapFromGlobal(sourceWidget->mapToGlobal(QPoint(0, 0)));
        }
        if (slideFromPos->x() < sourceInContainer.x()) {
            // Panel was to the left — slide in from left
            startPos.setX(targetPos.x() - SlideOffset);
        } else {
            // Panel was to the right — slide in from right
            startPos.setX(targetPos.x() + SlideOffset);
        }
    } else if (sourceWidget && sourceWidget->isVisible()) {
        QPoint sourceInContainer;
        if (m_container->isAncestorOf(sourceWidget)) {
            sourceInContainer = sourceWidget->mapTo(m_container, QPoint(0, 0));
        } else {
            sourceInContainer = m_container->mapFromGlobal(sourceWidget->mapToGlobal(QPoint(0, 0)));
        }
        // Slide from the direction of the source widget
        if (targetPos.x() < sourceInContainer.x()) {
            // Panel is to the left — slide in from right
            startPos.setX(targetPos.x() + SlideOffset);
        } else {
            // Panel is to the right — slide in from left
            startPos.setX(targetPos.x() - SlideOffset);
        }
        // No vertical offset — purely horizontal slide
    }

    m_panel->move(startPos);
    m_panel->raise();

    qApp->installEventFilter(this);

    animatePanelTo(targetPos);
    m_panel->showAnimated();

    QTimer::singleShot(PositionAnimationDuration, this, [this]() {
        m_isShowing = false;
        emit shown();
    });

    QTimer::singleShot(50, this, [this]() {
        if (m_panel && m_panel->isVisible()) {
            m_panel->setFocus();
        }
    });
}

void BrushPackOverlay::hidePanel()
{
    if (m_isHiding || !m_panel->isVisible()) {
        return;
    }

    m_isHiding = true;
    m_isShowing = false;

    qApp->removeEventFilter(this);

    // Slide out horizontally toward source
    if (m_panel && m_posAnimation) {
        QPoint currentPos = m_panel->pos();
        QPoint endPos = currentPos;

        if (m_sourceWidget && m_sourceWidget->isVisible()) {
            QPoint sourceInContainer;
            if (m_container->isAncestorOf(m_sourceWidget)) {
                sourceInContainer = m_sourceWidget->mapTo(m_container, QPoint(0, 0));
            } else {
                sourceInContainer
                    = m_container->mapFromGlobal(m_sourceWidget->mapToGlobal(QPoint(0, 0)));
            }
            if (currentPos.x() < sourceInContainer.x()) {
                // Panel is to the left — slide out toward right (source)
                endPos.setX(currentPos.x() + SlideOffset);
            } else {
                // Panel is to the right — slide out toward left (source)
                endPos.setX(currentPos.x() - SlideOffset);
            }
        } else {
            endPos.setX(currentPos.x() + SlideOffset);
        }

        m_posAnimation->stop();
        m_posAnimation->setStartValue(currentPos);
        m_posAnimation->setEndValue(endPos);
        m_posAnimation->start();
    }

    m_panel->hideAnimated();

    QTimer::singleShot(PositionAnimationDuration, this, [this]() {
        m_isHiding = false;
        m_sourceWidget = nullptr;
        emit hidden();
    });
}

void BrushPackOverlay::forceHide()
{
    qApp->removeEventFilter(this);

    if (m_panel) {
        m_panel->hide();
    }
    if (m_posAnimation) {
        m_posAnimation->stop();
    }
    m_isShowing = false;
    m_isHiding = false;
    m_sourceWidget = nullptr;
}

bool BrushPackOverlay::isActive() const
{
    return m_panel && m_panel->isVisible() && !m_isHiding;
}

bool BrushPackOverlay::canShowPanel() const
{
    if (!m_panel || !m_container)
        return false;

    int containerW = m_container->width();
    int containerH = m_container->height();
    int panelW = m_panel->width();
    int panelH = m_panel->height();

    // Need at least MinContainerMargin on each side
    return (containerW >= panelW + 2 * MinContainerMargin)
        && (containerH >= panelH + 2 * MinContainerMargin);
}

QPoint BrushPackOverlay::calculatePanelPosition(
    QWidget* sourceWidget, const QSize& panelSizeOverride) const
{
    if (!m_panel || !m_container)
        return QPoint();

    int containerWidth = m_container->width();
    int containerHeight = m_container->height();
    int panelWidth = panelSizeOverride.isValid() ? panelSizeOverride.width() : m_panel->width();
    int panelHeight = panelSizeOverride.isValid() ? panelSizeOverride.height() : m_panel->height();

    if (!sourceWidget) {
        return QPoint((containerWidth - panelWidth) / 2, (containerHeight - panelHeight) / 2);
    }

    // Defensive: ensure sourceWidget is actually visible and has a valid parent chain
    if (!sourceWidget->isVisible() || !sourceWidget->window()) {
        return QPoint((containerWidth - panelWidth) / 2, (containerHeight - panelHeight) / 2);
    }

    QPoint sourceInContainer;
    // mapTo can crash if source is not an ancestor — use try-catch equivalent
    if (m_container->isAncestorOf(sourceWidget) || sourceWidget == m_container) {
        sourceInContainer = sourceWidget->mapTo(m_container, QPoint(0, 0));
    } else {
        // Fallback: map through global coordinates
        QPoint globalPos = sourceWidget->mapToGlobal(QPoint(0, 0));
        sourceInContainer = m_container->mapFromGlobal(globalPos);
    }
    QSize sourceSize = sourceWidget->size();

    // Position to the left of the source widget
    int x = sourceInContainer.x() - panelWidth - OffsetFromSource;
    int y = sourceInContainer.y();

    // If doesn't fit on left, try right
    if (x < OffsetFromSource) {
        x = sourceInContainer.x() + sourceSize.width() + OffsetFromSource;
    }

    // Vertical centering relative to source
    y = sourceInContainer.y() + (sourceSize.height() - panelHeight) / 2;

    // Constrain to container
    x = qBound(OffsetFromSource, x, containerWidth - panelWidth - OffsetFromSource);
    y = qBound(OffsetFromSource, y, containerHeight - panelHeight - OffsetFromSource);

    return QPoint(x, y);
}

QPoint BrushPackOverlay::clampPanelToContainer(const QPoint& pos) const
{
    if (!m_panel || !m_container)
        return pos;
    int cw = m_container->width();
    int ch = m_container->height();
    int pw = m_panel->width();
    int ph = m_panel->height();
    int maxX = cw - pw - PanelEdgeMargin;
    int maxY = ch - ph - PanelEdgeMargin;
    return QPoint(qBound(PanelEdgeMargin, pos.x(), qMax(PanelEdgeMargin, maxX)),
        qBound(PanelEdgeMargin, pos.y(), qMax(PanelEdgeMargin, maxY)));
}

void BrushPackOverlay::animatePanelTo(const QPoint& targetPos)
{
    if (!m_panel || !m_posAnimation)
        return;

    m_posAnimation->stop();
    m_posAnimation->setStartValue(m_panel->pos());
    m_posAnimation->setEndValue(targetPos);
    m_posAnimation->start();
}

bool BrushPackOverlay::isPanelOrChild(QWidget* widget) const
{
    if (!widget || !m_panel)
        return false;
    return widget == m_panel || m_panel->isAncestorOf(widget);
}

bool BrushPackOverlay::isPanelAuxiliaryWidget(QWidget* widget) const
{
    if (!widget || !m_panel) {
        return false;
    }

    return m_panel->ownsAuxiliaryWidget(widget);
}

bool BrushPackOverlay::isSourceOrChild(QWidget* widget) const
{
    if (!widget || !m_sourceWidget)
        return false;
    return widget == m_sourceWidget || m_sourceWidget->isAncestorOf(widget);
}

bool BrushPackOverlay::isComboPopupOfPanel(QWidget* widget) const
{
    if (!widget || !m_panel)
        return false;
    for (QWidget* w = widget; w; w = w->parentWidget()) {
        const QVariant v = w->property("ruwa_owner_combo");
        if (v.isValid()) {
            if (QWidget* owner = v.value<QWidget*>()) {
                return isPanelOrChild(owner);
            }
            break;
        }
    }
    return false;
}

bool BrushPackOverlay::eventFilter(QObject* watched, QEvent* event)
{
    // Container resize
    if (watched == m_container && event->type() == QEvent::Resize) {
        if (m_panel && m_panel->isVisible() && !m_isShowing && !m_isHiding) {
            // Hide panel if container became too small
            if (!canShowPanel()) {
                forceHide();
                return false;
            }
            if (m_userMovedPanel) {
                auto* resizeEvent = static_cast<QResizeEvent*>(event);
                QSize oldSize = resizeEvent->oldSize();
                QSize newSize = resizeEvent->size();
                QRect newGeo = m_container->geometry();
                QRect oldGeo = m_lastContainerGeometry;
                int pw = m_panel->width();
                int ph = m_panel->height();
                QPoint pos = m_panel->pos();

                if (oldSize.width() <= 0 || oldSize.height() <= 0) {
                    oldSize = newSize;
                    oldGeo = newGeo;
                }

                // Snap only when the RIGHT/BOTTOM edge moved (not when left/top moved)
                bool rightEdgeMoved
                    = (newSize.width() != oldSize.width()) && (oldGeo.x() == newGeo.x());
                bool bottomEdgeMoved
                    = (newSize.height() != oldSize.height()) && (oldGeo.y() == newGeo.y());

                bool wasAtRight = oldSize.width() > 0
                    && pos.x() >= oldSize.width() - pw - PanelEdgeMargin - EdgeSnapThreshold;
                bool wasAtBottom = oldSize.height() > 0
                    && pos.y() >= oldSize.height() - ph - PanelEdgeMargin - EdgeSnapThreshold;

                int newX = pos.x();
                int newY = pos.y();
                if (newSize.width() > oldSize.width() && wasAtRight && rightEdgeMoved) {
                    newX = newSize.width() - pw - PanelEdgeMargin;
                }
                if (newSize.height() > oldSize.height() && wasAtBottom && bottomEdgeMoved) {
                    newY = newSize.height() - ph - PanelEdgeMargin;
                }

                m_lastContainerGeometry = newGeo;

                QPoint clamped = clampPanelToContainer(QPoint(newX, newY));
                if (clamped != m_panel->pos()) {
                    m_panel->move(clamped);
                }
            } else if (m_sourceWidget) {
                m_panel->move(calculatePanelPosition(m_sourceWidget));
            }
        }
        return false;
    }

    // Only process when panel is visible
    if (!m_panel || !m_panel->isVisible() || m_isHiding || m_isShowing) {
        return false;
    }

    const QRect panelGlobalRect(m_panel->mapToGlobal(QPoint(0, 0)), m_panel->size());

    auto shouldBlockPassThrough
        = [this, watched, &panelGlobalRect](const QPoint& globalPos) -> bool {
        if (!panelGlobalRect.contains(globalPos)) {
            return false;
        }

        if (QWidget* watchedWidget = qobject_cast<QWidget*>(watched)) {
            if (isPanelAuxiliaryWidget(watchedWidget)) {
                return false;
            }
            if (watchedWidget->window() && watchedWidget->window() != m_container->window()) {
                return false;
            }
            if (isPanelOrChild(watchedWidget)) {
                return false;
            }
        }

        QWidget* widgetAtPos = QApplication::widgetAt(globalPos);
        if (isPanelAuxiliaryWidget(widgetAtPos)) {
            return false;
        }
        if (widgetAtPos && widgetAtPos->window()
            && widgetAtPos->window() != m_container->window()) {
            return false;
        }
        if (isPanelOrChild(widgetAtPos)) {
            return false;
        }

        return true;
    };

    if (event->type() == QEvent::MouseButtonPress || event->type() == QEvent::MouseButtonRelease
        || event->type() == QEvent::MouseMove || event->type() == QEvent::MouseButtonDblClick) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        if (shouldBlockPassThrough(mouseEvent->globalPosition().toPoint())) {
            return true;
        }
    }

    if (event->type() == QEvent::TabletPress || event->type() == QEvent::TabletMove
        || event->type() == QEvent::TabletRelease) {
        auto* tabletEvent = static_cast<QTabletEvent*>(event);
        if (shouldBlockPassThrough(tabletEvent->globalPosition().toPoint())) {
            return true;
        }
    }

    // Mouse press - check if outside panel
    if (event->type() == QEvent::MouseButtonPress) {
        auto* mouseEvent = static_cast<QMouseEvent*>(event);
        QPoint globalPos = mouseEvent->globalPosition().toPoint();
        QWidget* clickedWidget = QApplication::widgetAt(globalPos);

        // Click on panel or its children - don't close
        if (isPanelOrChild(clickedWidget)) {
            return false;
        }

        // Click on detached auxiliary windows owned by the panel - don't close.
        if (isPanelAuxiliaryWidget(clickedWidget)) {
            return false;
        }

        // Click on combobox dropdown (popup) - don't close
        if (isComboPopupOfPanel(clickedWidget)) {
            return false;
        }

        if (clickedWidget && clickedWidget->window()
            && clickedWidget->window() != m_container->window()) {
            return false;
        }

        // Safety: if click is inside panel bounds but widgetAt returned something else,
        // consume it to prevent click-through to canvas.
        if (panelGlobalRect.contains(globalPos)) {
            return true;
        }

        // Click on source widget - don't close (let toggle work)
        if (isSourceOrChild(clickedWidget)) {
            return false;
        }

        // If user moved the panel, don't close on canvas click
        if (m_userMovedPanel) {
            return false;
        }

        // Click anywhere else - close panel
        hidePanel();
        return false;
    }

    // Tablet press should mirror mouse press behavior (stylus outside-tap closes panel).
    if (event->type() == QEvent::TabletPress) {
        auto* tabletEvent = static_cast<QTabletEvent*>(event);
        const QPoint globalPos = tabletEvent->globalPosition().toPoint();
        QWidget* clickedWidget = QApplication::widgetAt(globalPos);

        // Tap on panel or its children - don't close.
        if (isPanelOrChild(clickedWidget)) {
            return false;
        }

        // Tap on detached auxiliary windows owned by the panel - don't close.
        if (isPanelAuxiliaryWidget(clickedWidget)) {
            return false;
        }

        // Tap on combobox dropdown (popup) - don't close.
        if (isComboPopupOfPanel(clickedWidget)) {
            return false;
        }

        if (clickedWidget && clickedWidget->window()
            && clickedWidget->window() != m_container->window()) {
            return false;
        }

        // Safety: if tablet tap is inside panel bounds but widgetAt returned something else,
        // consume it to prevent pass-through to canvas beneath.
        if (panelGlobalRect.contains(globalPos)) {
            return true;
        }

        // Tap on source widget - don't close (let toggle work).
        if (isSourceOrChild(clickedWidget)) {
            return false;
        }

        // If user moved the panel, don't close on canvas tap
        if (m_userMovedPanel) {
            return false;
        }

        // Tap anywhere else - close panel.
        hidePanel();
        return false;
    }

    // Prevent wheel-through when cursor is over panel bounds.
    if (event->type() == QEvent::Wheel) {
        QPoint globalPos = QCursor::pos();
        if (panelGlobalRect.contains(globalPos)) {
            QWidget* hovered = QApplication::widgetAt(globalPos);
            if (isPanelAuxiliaryWidget(hovered)) {
                return false;
            }
            if (!isPanelOrChild(hovered)) {
                return true;
            }
        }
    }

    // Escape key
    if (event->type() == QEvent::KeyPress) {
        auto* keyEvent = static_cast<QKeyEvent*>(event);
        if (keyEvent->key() == Qt::Key_Escape) {
            hidePanel();
            return true;
        }
    }

    return false;
}

} // namespace ruwa::ui::widgets
