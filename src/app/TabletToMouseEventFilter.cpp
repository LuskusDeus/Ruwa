// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   T A B L E T   T O   M O U S E   E V E N T   F I L T E R
// ==========================================================================

#include "app/TabletToMouseEventFilter.h"
#include "services/input/StylusInputManager.h"
#include "features/canvas/rendering/OpenGLCanvasWidget.h"
#include "features/canvas/ui/CanvasPanel.h"
#include "features/canvas/ui/CanvasStylusJoystickContainerWidget.h"
#include "features/layers/ui/LayerListView.h"
#include "features/layers/ui/LayerRowWidget.h"
#include "shared/widgets/inputs/ProgressHandleSlider.h"
#include "shared/widgets/layout/SmoothScrollArea.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QApplication>
#include <QComboBox>
#include <QCoreApplication>
#include <QCursor>
#include <QEnterEvent>
#include <QList>
#include <QLineEdit>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QTabletEvent>
#include <QTextEdit>
#include <QWheelEvent>
#include <QWidget>

namespace ruwa {

namespace {

constexpr auto kUiDragActiveProperty = "ruwa_ui_drag_active";

bool isUiDragActive()
{
    return qApp && qApp->property(kUiDragActiveProperty).toBool();
}

} // namespace

TabletToMouseEventFilter::TabletToMouseEventFilter(QObject* parent)
    : QObject(parent)
{
}

TabletToMouseEventFilter::~TabletToMouseEventFilter()
{
    clearTabletCursorOverride();
}

bool TabletToMouseEventFilter::isCanvasWidget(QWidget* widget)
{
    if (!widget) {
        return false;
    }

    for (QWidget* w = widget; w; w = w->parentWidget()) {
        if (qobject_cast<aether::OpenGLCanvasWidget*>(w)) {
            return true;
        }
    }
    return false;
}

static bool isCanvasPanelWidget(QWidget* widget)
{
    return widget && qobject_cast<ruwa::ui::workspace::CanvasPanel*>(widget);
}

static ruwa::ui::workspace::CanvasPanel* findCanvasPanelAncestor(QWidget* widget)
{
    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (auto* panel = qobject_cast<ruwa::ui::workspace::CanvasPanel*>(current)) {
            return panel;
        }
    }
    return nullptr;
}

static ruwa::ui::workspace::CanvasPanel* findActiveDrawingCanvasPanel(
    QWidget* widget, const QPoint& globalPos)
{
    if (auto* panel = findCanvasPanelAncestor(widget); panel && panel->isDrawingActive()) {
        return panel;
    }

    if (QWidget* widgetAtPos = QApplication::widgetAt(globalPos)) {
        if (widgetAtPos != widget) {
            if (auto* panel = findCanvasPanelAncestor(widgetAtPos);
                panel && panel->isDrawingActive()) {
                return panel;
            }
        }
    }

    if (QWidget* grabber = QWidget::mouseGrabber()) {
        if (auto* panel = findCanvasPanelAncestor(grabber); panel && panel->isDrawingActive()) {
            return panel;
        }
    }

    return nullptr;
}

static bool shouldBlockWidgetMouseDuringCanvasDrawing(QWidget* widget, const QPoint& globalPos)
{
    auto* panel = findActiveDrawingCanvasPanel(widget, globalPos);
    if (!panel || widget == panel || isCanvasPanelWidget(widget)) {
        return false;
    }

    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<aether::OpenGLCanvasWidget*>(current)) {
            return false;
        }
    }

    if (findCanvasPanelAncestor(widget) != panel) {
        return false;
    }

    return true;
}

static bool isPointInsideWidgetGlobalRect(QWidget* widget, const QPoint& globalPos)
{
    if (!widget || !widget->isVisible()) {
        return false;
    }

    return QRect(widget->mapToGlobal(QPoint(0, 0)), widget->size()).contains(globalPos);
}

static ruwa::ui::widgets::CanvasStylusJoystickContainerWidget* findStylusJoystickAncestor(
    QWidget* widget)
{
    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (auto* joystick
            = qobject_cast<ruwa::ui::widgets::CanvasStylusJoystickContainerWidget*>(current)) {
            return joystick;
        }
    }
    return nullptr;
}

static ruwa::ui::workspace::CanvasPanel* findTransparentStylusJoystickPanel(
    QWidget* widget, const QPoint& globalPos)
{
    auto* joystick = findStylusJoystickAncestor(widget);
    if (!joystick) {
        return nullptr;
    }

    if (joystick->hitTestInteractiveArea(QPointF(joystick->mapFromGlobal(globalPos)))) {
        return nullptr;
    }

    return findCanvasPanelAncestor(joystick);
}

static bool shouldBypassCanvasSynthesis(QWidget* widget, const QPoint& globalPos)
{
    auto* panel = findCanvasPanelAncestor(widget);
    if (!panel) {
        return false;
    }

    auto* canvasWidget = panel->findChild<aether::OpenGLCanvasWidget*>();
    if (!canvasWidget || !isPointInsideWidgetGlobalRect(canvasWidget, globalPos)) {
        return false;
    }

    if (!widget) {
        return true;
    }

    if (widget == panel || widget == canvasWidget || widget == canvasWidget->parentWidget()) {
        return true;
    }

    if (auto* joystick = findStylusJoystickAncestor(widget)) {
        return !joystick->hitTestInteractiveArea(QPointF(joystick->mapFromGlobal(globalPos)));
    }

    return false;
}

ruwa::ui::widgets::SmoothScrollArea* TabletToMouseEventFilter::findSmoothScrollArea(QWidget* widget)
{
    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (auto* scrollArea = qobject_cast<ruwa::ui::widgets::SmoothScrollArea*>(current)) {
            return scrollArea;
        }
    }
    return nullptr;
}

bool TabletToMouseEventFilter::shouldUseStylusSwipeForWidget(QWidget* widget)
{
    if (!widget) {
        return false;
    }

    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (qobject_cast<ruwa::ui::widgets::LayerRowWidget*>(current)
            || qobject_cast<ruwa::ui::widgets::LayerListView*>(current)) {
            return false;
        }

        if (qobject_cast<QAbstractButton*>(current) || qobject_cast<QAbstractSlider*>(current)
            || qobject_cast<QAbstractSpinBox*>(current) || qobject_cast<QComboBox*>(current)
            || qobject_cast<QLineEdit*>(current) || qobject_cast<QTextEdit*>(current)
            || qobject_cast<QPlainTextEdit*>(current)
            || qobject_cast<ruwa::ui::widgets::ProgressHandleSlider*>(current)
            || qobject_cast<QAbstractItemView*>(current)) {
            return false;
        }
    }

    return findSmoothScrollArea(widget) != nullptr;
}

void TabletToMouseEventFilter::clearPendingStylusSwipe()
{
    if (m_pendingScrollArea && m_pendingScrollArea->isStylusSwipeActive()) {
        m_pendingScrollArea->cancelStylusSwipe();
    }

    m_pendingScrollArea.clear();
    m_pendingPressTarget.clear();
    m_pendingPressGlobalPos = QPoint();
    m_pendingPressButton = Qt::NoButton;
    m_pendingPressButtons = Qt::NoButton;
    m_pendingPressModifiers = Qt::NoModifier;
    m_stylusSwipeDragging = false;
}

QWidget* TabletToMouseEventFilter::resolveMouseTarget(
    QWidget* watchedWidget, const QPoint& globalPos) const
{
    if (m_activeMouseTarget && !isCanvasWidget(m_activeMouseTarget)) {
        return m_activeMouseTarget;
    }

    if (m_pendingPressTarget && !isCanvasWidget(m_pendingPressTarget)) {
        return m_pendingPressTarget;
    }

    if (QWidget* grabber = QWidget::mouseGrabber()) {
        if (!isCanvasWidget(grabber)) {
            return grabber;
        }
    }

    if (QWidget* widgetAtPos = QApplication::widgetAt(globalPos)) {
        if (shouldBypassCanvasSynthesis(widgetAtPos, globalPos)) {
            return nullptr;
        }
        if (!isCanvasWidget(widgetAtPos)) {
            return widgetAtPos;
        }
    }

    if (watchedWidget && !isCanvasWidget(watchedWidget) && !isCanvasPanelWidget(watchedWidget)) {
        if (shouldBypassCanvasSynthesis(watchedWidget, globalPos)) {
            return nullptr;
        }
        return watchedWidget;
    }

    return nullptr;
}

void TabletToMouseEventFilter::updateHoverTarget(QWidget* target, const QPoint& globalPos)
{
    if (m_hoverTarget == target) {
        return;
    }

    if (m_hoverTarget) {
        QEvent leaveEvent(QEvent::Leave);
        QCoreApplication::sendEvent(m_hoverTarget, &leaveEvent);
    }

    m_hoverTarget = target;

    if (m_hoverTarget) {
        const QPointF localPos = m_hoverTarget->mapFromGlobal(globalPos);
        const QPointF windowPos = m_hoverTarget->window()
            ? m_hoverTarget->window()->mapFromGlobal(globalPos)
            : localPos;
        const QPointF screenPos(globalPos);
        QEnterEvent enterEvent(localPos, windowPos, screenPos);
        QCoreApplication::sendEvent(m_hoverTarget, &enterEvent);
    }
}

void TabletToMouseEventFilter::clearHoverTarget(const QPoint& globalPos)
{
    Q_UNUSED(globalPos);
    updateHoverTarget(nullptr, QPoint());
}

QCursor TabletToMouseEventFilter::effectiveCursorForWidget(QWidget* widget)
{
    for (QWidget* current = widget; current; current = current->parentWidget()) {
        if (current->testAttribute(Qt::WA_SetCursor)) {
            return current->cursor();
        }
    }

    return QCursor(Qt::ArrowCursor);
}

void TabletToMouseEventFilter::updateTabletCursorOverride(QWidget* target)
{
    if (isUiDragActive()) {
        return;
    }

    if (!target) {
        clearTabletCursorOverride();
        return;
    }

    if (m_tabletCursorOverrideActive && m_cursorOverrideTarget == target) {
        return;
    }

    const QCursor cursor = effectiveCursorForWidget(target);
    if (m_tabletCursorOverrideActive) {
        QApplication::changeOverrideCursor(cursor);
        m_cursorOverrideTarget = target;
        return;
    }

    QApplication::setOverrideCursor(cursor);
    m_tabletCursorOverrideActive = true;
    m_cursorOverrideTarget = target;
}

void TabletToMouseEventFilter::clearTabletCursorOverride()
{
    if (!m_tabletCursorOverrideActive) {
        return;
    }

    QApplication::restoreOverrideCursor();
    m_tabletCursorOverrideActive = false;
    m_cursorOverrideTarget.clear();
}

bool TabletToMouseEventFilter::eventFilter(QObject* watched, QEvent* event)
{
    const QEvent::Type type = event->type();
    auto* widget = qobject_cast<QWidget*>(watched);

    auto& stylusInput = services::input::StylusInputManager::instance();
    if (!stylusInput.isDispatchingNativeInput() && stylusInput.usesNativeUiRouting()) {
        const bool nativeButtonsActive = stylusInput.hasActiveNativePointerButtons();

        switch (type) {
        case QEvent::MouseMove: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const QPoint globalPos = mouseEvent->globalPosition().toPoint();

            // QCursor::setPos() posts a normal, asynchronous MouseMove. It is not
            // a new mouse sample and must neither steal pointer ownership from the
            // direct WinTab stream nor cancel pressed-state tracking in widgets.
            const bool nativeCursorWarp = stylusInput.consumeNativeCursorWarpAt(globalPos);

            if (nativeCursorWarp && !stylusInput.nativeCursorPosition()) {
                // Ownership has already moved to the mouse. This is an older
                // SetCursorPos result still draining from the Windows queue; do
                // not let canvas-owned overlays revive the stylus cursor with it.
                mouseEvent->accept();
                return true;
            }

            if (nativeButtonsActive) {
                // While a physical WinTab button is held, the synchronous events
                // dispatched by StylusInputManager are the complete authoritative
                // stream. A delayed compatibility move from Windows can carry
                // LeftButton too, so checking for NoButton is not sufficient.
                mouseEvent->accept();
                return true;
            }

            if (!nativeButtonsActive && !nativeCursorWarp) {
                stylusInput.activateMousePointer();
            }
            break;
        }
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseButtonRelease: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            if (nativeButtonsActive) {
                // Windows can post a compatibility release for the press which
                // activated the window after the direct WinTab stroke has begun.
                // Letting that release reach CanvasPanel ends the stroke while
                // the pen is still physically down. Native synthetic events do
                // not enter this branch because isDispatchingNativeInput() is true.
                mouseEvent->accept();
                return true;
            }
            stylusInput.activateMousePointer();
            break;
        }
        case QEvent::Wheel:
            if (!nativeButtonsActive) {
                // A wheel event transfers ownership without changing the one
                // system position shared by mouse and pen.
                stylusInput.activateMousePointer();
            }
            break;
        default:
            break;
        }
    }

    const bool widgetPressActive = (m_activeMouseTarget && !isCanvasWidget(m_activeMouseTarget))
        || (m_pendingPressTarget && !isCanvasWidget(m_pendingPressTarget));

    if (!widgetPressActive && !m_dispatchingSyntheticMouseEvent && widget) {
        switch (type) {
        case QEvent::MouseButtonPress:
        case QEvent::MouseButtonDblClick:
        case QEvent::MouseMove:
        case QEvent::MouseButtonRelease: {
            auto* mouseEvent = static_cast<QMouseEvent*>(event);
            const QPoint globalPos = mouseEvent->globalPosition().toPoint();
            if (shouldBlockWidgetMouseDuringCanvasDrawing(widget, globalPos)) {
                m_activeMouseTarget.clear();
                clearPendingStylusSwipe();
                clearHoverTarget(globalPos);
                clearTabletCursorOverride();
                return true;
            }
            break;
        }
        case QEvent::Wheel: {
            auto* wheelEvent = static_cast<QWheelEvent*>(event);
            const QPoint globalPos = wheelEvent->globalPosition().toPoint();
            if (shouldBlockWidgetMouseDuringCanvasDrawing(widget, globalPos)) {
                m_activeMouseTarget.clear();
                clearPendingStylusSwipe();
                clearHoverTarget(globalPos);
                clearTabletCursorOverride();
                return true;
            }
            break;
        }
        default:
            break;
        }
    }

    if (m_tabletCursorOverrideActive && !m_dispatchingSyntheticMouseEvent) {
        bool shouldClearOverride = type == QEvent::MouseButtonPress
            || type == QEvent::MouseButtonRelease || type == QEvent::Wheel
            || type == QEvent::WindowDeactivate || type == QEvent::ApplicationDeactivate;
        if (type == QEvent::MouseMove) {
            const auto* mouseEvent = static_cast<QMouseEvent*>(event);
            shouldClearOverride = mouseEvent->source() == Qt::MouseEventNotSynthesized;
        }
        if (shouldClearOverride) {
            clearTabletCursorOverride();
        }
    }

    if (type == QEvent::TabletLeaveProximity) {
        m_activeMouseTarget.clear();
        clearPendingStylusSwipe();
        clearHoverTarget(QPoint());
        clearTabletCursorOverride();
        if (stylusInput.usesNativeUiRouting()) {
            // Ruwa WinTab owns proximity and release tracking. The parallel
            // Windows Ink stream may report a leave while focus is changing;
            // forwarding it would terminate an otherwise active native stroke.
            event->accept();
            return true;
        }
        return false;
    }

    if (type == QEvent::TabletEnterProximity) {
        // A leaked override from a prior interaction (e.g. stale override left
        // by the previous proximity session or by interleaving with another
        // override-cursor user) would otherwise remain visible on top of the
        // canvas's BlankCursor while the user draws.
        clearTabletCursorOverride();
        if (stylusInput.usesNativeUiRouting()) {
            event->accept();
            return true;
        }
        return false;
    }

    if (type != QEvent::TabletPress && type != QEvent::TabletMove
        && type != QEvent::TabletRelease) {
        return false;
    }

    auto* tabletEvent = static_cast<QTabletEvent*>(event);
    const QPoint globalPos = tabletEvent->globalPosition().toPoint();

    if (ruwa::services::input::StylusInputManager::instance().usesNativeUiRouting()) {
        // WinTab (Ruwa) already dispatches every packet through StylusInputManager.
        // Letting the parallel Windows Ink QTabletEvent stream reach the canvas adds
        // delayed, slightly different coordinates to the same interaction. For lasso
        // this makes the polygon double back and leaves holes in the filled result.
        clearTabletCursorOverride();
        tabletEvent->accept();
        return true;
    }

    if (!widgetPressActive) {
        if (auto* panel = findActiveDrawingCanvasPanel(widget, globalPos);
            panel && shouldBlockWidgetMouseDuringCanvasDrawing(widget, globalPos)) {
            m_activeMouseTarget.clear();
            clearPendingStylusSwipe();
            clearHoverTarget(globalPos);
            clearTabletCursorOverride();
            panel->forwardTabletEvent(tabletEvent);
            return true;
        }
    }

    if (!widgetPressActive) {
        if (auto* panel = findTransparentStylusJoystickPanel(widget, globalPos)) {
            m_activeMouseTarget.clear();
            clearPendingStylusSwipe();
            clearHoverTarget(globalPos);
            clearTabletCursorOverride();
            panel->forwardTabletEvent(tabletEvent);
            return tabletEvent->isAccepted();
        }
    }

    if (ruwa::services::input::StylusInputManager::instance().handleTabletEvent(
            widget, tabletEvent)) {
        m_activeMouseTarget.clear();
        clearPendingStylusSwipe();
        clearHoverTarget(tabletEvent->globalPosition().toPoint());
        clearTabletCursorOverride();
        return true;
    }

    if (!widget) {
        clearTabletCursorOverride();
        return false;
    }

    const QPointF globalPosF = tabletEvent->globalPosition();
    QWidget* target = resolveMouseTarget(widget, globalPos);
    if (!target) {
        clearPendingStylusSwipe();
        // Always clear the tablet override cursor when the stylus is over the
        // canvas (target == null).  Without clearing on TabletPress the user
        // could press straight onto the canvas after hovering UI without an
        // intermediate TabletMove on the canvas, leaving a stale arrow
        // override on top of the BlankCursor while drawing.
        clearHoverTarget(globalPos);
        clearTabletCursorOverride();
        if (type == QEvent::TabletRelease) {
            m_activeMouseTarget.clear();
        }
        return false;
    }

    auto effectiveButton = [&]() -> Qt::MouseButton {
        const Qt::MouseButtons buttons = tabletEvent->buttons();
        if (buttons & Qt::LeftButton) {
            return Qt::LeftButton;
        }
        if (buttons & Qt::RightButton) {
            return Qt::RightButton;
        }
        if (buttons & Qt::MiddleButton) {
            return Qt::MiddleButton;
        }
        return tabletEvent->button() != Qt::NoButton ? tabletEvent->button() : Qt::LeftButton;
    };

    auto sendSyntheticMouseEvent
        = [&](QEvent::Type mouseType, QWidget* mouseTarget, const QPointF& mouseGlobalPos,
              Qt::MouseButton button, Qt::MouseButtons buttons, Qt::KeyboardModifiers modifiers) {
              if (!mouseTarget) {
                  return;
              }

              const QPointF mouseLocalPos = mouseTarget->mapFromGlobal(mouseGlobalPos);
              QMouseEvent mouseEvent(
                  mouseType, mouseLocalPos, mouseGlobalPos, button, buttons, modifiers);
              if (mouseType == QEvent::MouseMove && buttons != Qt::NoButton) {
                  clearTabletCursorOverride();
              }
              m_dispatchingSyntheticMouseEvent = true;
              QCoreApplication::sendEvent(mouseTarget, &mouseEvent);
              m_dispatchingSyntheticMouseEvent = false;
          };

    switch (type) {
    case QEvent::TabletPress: {
        const Qt::MouseButton button = effectiveButton();
        if (button != Qt::NoButton) {
            if (auto* scrollArea = findSmoothScrollArea(target);
                scrollArea && shouldUseStylusSwipeForWidget(target)) {
                clearPendingStylusSwipe();
                m_pendingScrollArea = scrollArea;
                m_pendingPressTarget = target;
                m_pendingPressGlobalPos = globalPos;
                m_pendingPressButton = button;
                m_pendingPressButtons = tabletEvent->buttons();
                m_pendingPressModifiers = tabletEvent->modifiers();
                m_stylusSwipeDragging = false;
                updateHoverTarget(target, globalPos);
                updateTabletCursorOverride(target);
                tabletEvent->accept();
                return true;
            }

            m_activeMouseTarget = target;
            updateHoverTarget(target, globalPos);
            sendSyntheticMouseEvent(QEvent::MouseButtonPress, target, globalPosF, button,
                tabletEvent->buttons(), tabletEvent->modifiers());
            updateTabletCursorOverride(target);
        }
        tabletEvent->accept();
        return true;
    }
    case QEvent::TabletMove: {
        if (m_pendingScrollArea) {
            updateHoverTarget(target, globalPos);

            if (!m_stylusSwipeDragging
                && (globalPos - m_pendingPressGlobalPos).manhattanLength()
                    >= QApplication::startDragDistance()) {
                m_pendingScrollArea->beginStylusSwipe(m_pendingPressGlobalPos);
                m_stylusSwipeDragging = true;
            }

            if (m_stylusSwipeDragging) {
                m_pendingScrollArea->updateStylusSwipe(globalPos);
            }

            updateTabletCursorOverride(target);
            tabletEvent->accept();
            return true;
        }

        updateHoverTarget(target, globalPos);
        sendSyntheticMouseEvent(QEvent::MouseMove, target, globalPosF, Qt::NoButton,
            tabletEvent->buttons(), tabletEvent->modifiers());
        updateTabletCursorOverride(target);
        tabletEvent->accept();
        return true;
    }
    case QEvent::TabletRelease: {
        if (m_pendingScrollArea) {
            updateHoverTarget(target, globalPos);

            if (m_stylusSwipeDragging) {
                m_pendingScrollArea->endStylusSwipe(globalPos);
            } else {
                QWidget* pressTarget
                    = m_pendingPressTarget.data() ? m_pendingPressTarget.data() : target;
                m_activeMouseTarget = pressTarget;
                sendSyntheticMouseEvent(QEvent::MouseButtonPress, pressTarget,
                    QPointF(m_pendingPressGlobalPos), m_pendingPressButton, m_pendingPressButtons,
                    m_pendingPressModifiers);
                sendSyntheticMouseEvent(QEvent::MouseButtonRelease, pressTarget, globalPosF,
                    m_pendingPressButton, Qt::NoButton, tabletEvent->modifiers());
                m_activeMouseTarget.clear();
            }

            clearPendingStylusSwipe();
            clearTabletCursorOverride();
            tabletEvent->accept();
            return true;
        }

        const Qt::MouseButton button
            = tabletEvent->button() != Qt::NoButton ? tabletEvent->button() : effectiveButton();
        if (button != Qt::NoButton) {
            updateHoverTarget(target, globalPos);
            sendSyntheticMouseEvent(QEvent::MouseButtonRelease, target, globalPosF, button,
                Qt::NoButton, tabletEvent->modifiers());
        }
        m_activeMouseTarget.clear();
        clearTabletCursorOverride();
        tabletEvent->accept();
        return true;
    }
    default:
        return false;
    }
}

void TabletToMouseEventFilter::ensureRunsFirst(QApplication* app)
{
    if (!app) {
        return;
    }

    QList<TabletToMouseEventFilter*> filters;
    for (QObject* child : app->children()) {
        if (auto* filter = qobject_cast<TabletToMouseEventFilter*>(child)) {
            filters.append(filter);
        }
    }

    if (filters.isEmpty()) {
        return;
    }

    auto* filter = filters.constFirst();
    for (int i = 1; i < filters.size(); ++i) {
        app->removeEventFilter(filters.at(i));
        filters.at(i)->deleteLater();
    }

    app->removeEventFilter(filter);
    app->installEventFilter(filter);
}

} // namespace ruwa
