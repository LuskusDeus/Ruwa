// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   C U R S O R   M A N A G E R
// ==========================================================================

#include "CanvasCursorManager.h"
#include "CanvasStylusJoystickContainerWidget.h"
#include "services/input/StylusInputManager.h"

#include <QApplication>
#include <QCursor>
#include <QEvent>
#include <QMouseEvent>
#include <QScreen>
#include <QTabletEvent>
#include <QTimer>
#include <QWidget>
#include <QWindow>

namespace ruwa::ui::workspace {

namespace {

constexpr auto kUiDragActiveProperty = "ruwa_ui_drag_active";

bool isUiDragActive()
{
    return qApp && qApp->property(kUiDragActiveProperty).toBool();
}

bool exclusionWidgetContainsGlobalPos(QWidget* widget, const QPoint& globalPos)
{
    if (!widget || !widget->isVisible()) {
        return false;
    }

    const QPoint localPos = widget->mapFromGlobal(globalPos);
    if (!widget->rect().contains(localPos)) {
        return false;
    }

    if (auto* stylusJoystick
        = qobject_cast<ruwa::ui::widgets::CanvasStylusJoystickContainerWidget*>(widget)) {
        return stylusJoystick->hitTestInteractiveArea(QPointF(localPos));
    }

    const QRegion mask = widget->mask();
    return mask.isEmpty() || mask.contains(localPos);
}

bool exclusionWidgetHasActiveInteraction(QWidget* widget)
{
    if (!widget || !widget->isVisible()) {
        return false;
    }

    if (auto* stylusJoystick
        = qobject_cast<ruwa::ui::widgets::CanvasStylusJoystickContainerWidget*>(widget)) {
        return stylusJoystick->isJoystickInteractionActive();
    }

    return false;
}

} // namespace

CanvasCursorManager::CanvasCursorManager(
    QWidget* container, QWidget* canvasWidget, QWidget* overlayWidget, QObject* parent)
    : QObject(parent)
    , m_container(container)
    , m_canvasWidget(canvasWidget)
    , m_overlayWidget(overlayWidget)
{
    if (m_container) {
        m_container->installEventFilter(this);
    }
    if (m_canvasWidget) {
        m_canvasWidget->installEventFilter(this);
    }
    if (overlayWidget) {
        overlayWidget->installEventFilter(this);
    }

    ensureWindowTracking();
    QTimer::singleShot(0, this, [this]() {
        ensureWindowTracking();
        refreshCursorPosition();
    });
}

CanvasCursorManager::~CanvasCursorManager() = default;

void CanvasCursorManager::ensureWindowTracking()
{
    QWidget* topLevel = m_container ? m_container->window() : nullptr;
    if (m_windowFilterTarget != topLevel) {
        if (m_windowFilterTarget) {
            m_windowFilterTarget->removeEventFilter(this);
        }

        m_windowFilterTarget = topLevel;
        if (m_windowFilterTarget && m_windowFilterTarget != m_container) {
            m_windowFilterTarget->installEventFilter(this);
        }
    }

    QWindow* windowHandle = m_windowFilterTarget ? m_windowFilterTarget->windowHandle() : nullptr;
    if (m_trackedWindowHandle == windowHandle) {
        return;
    }

    QObject::disconnect(m_screenChangedConnection);
    m_screenChangedConnection = QMetaObject::Connection {};
    m_trackedWindowHandle = windowHandle;

    if (m_trackedWindowHandle) {
        m_screenChangedConnection = connect(m_trackedWindowHandle, &QWindow::screenChanged, this,
            [this](QScreen*) { refreshCursorPosition(); });
    }
}

bool CanvasCursorManager::eventFilter(QObject* /*watched*/, QEvent* event)
{
    ensureWindowTracking();

    switch (event->type()) {
    case QEvent::Resize:
        if (m_wasOverCanvas && m_activeOverlay) {
            positionOverlay(QCursor::pos());
            if (m_overlayWidget) {
                m_activeOverlay->stackUnder(m_overlayWidget);
            }
        }
        break;
    case QEvent::Move:
    case QEvent::Show:
    case QEvent::WindowStateChange:
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    case QEvent::DevicePixelRatioChange:
#endif
        refreshCursorPosition();
        break;
    case QEvent::Enter:
    case QEvent::MouseButtonPress:
    case QEvent::MouseButtonRelease:
    case QEvent::MouseMove: {
        if (event->type() == QEvent::Enter) {
            refreshCursorPosition();
            break;
        }

        const auto* mouseEvent = static_cast<QMouseEvent*>(event);
        // QCursor::setPos() keeps the shared Windows pointer in sync with the
        // direct WinTab backend. Windows delivers its generated MouseMove later,
        // potentially after a newer WinTab sample. Do not let that stale cursor
        // warp overwrite the GL cursor's direct-packet position.
        if (event->type() == QEvent::MouseMove
            && ruwa::services::input::StylusInputManager::instance().shouldIgnoreCanvasMouseMove(
                mouseEvent)) {
            break;
        }

        const QPoint globalPos = mouseEvent->globalPosition().toPoint();
        updateCursorPosition(globalPos);
        break;
    }
    case QEvent::TabletMove:
    case QEvent::TabletPress:
    case QEvent::TabletRelease: {
        auto* te = static_cast<QTabletEvent*>(event);
        updateCursorPosition(te->globalPosition().toPoint());
        break;
    }
    case QEvent::Wheel:
        // Wheel input can transfer ownership to the mouse without a preceding
        // MouseMove, so re-evaluate the custom cursor at the shared pointer.
        refreshCursorPosition();
        break;
    case QEvent::Leave:
        refreshCursorPosition();
        break;
    default:
        break;
    }

    return false;
}

void CanvasCursorManager::setRequestedCursor(Qt::CursorShape shape)
{
    m_hasRequestedCursor = true;
    m_requestedCursor = shape;
}

void CanvasCursorManager::clearRequestedCursor()
{
    m_hasRequestedCursor = false;
}

void CanvasCursorManager::setCursorResolver(CursorResolver resolver)
{
    m_cursorResolver = std::move(resolver);
}

void CanvasCursorManager::setUseGLBrushCursor(bool use)
{
    m_useGLBrushCursor = use;
}

void CanvasCursorManager::setBrushCursorCallback(BrushCursorCallback callback)
{
    m_brushCursorCallback = std::move(callback);
}

void CanvasCursorManager::setUseGLEyedropperCursor(bool use)
{
    m_useGLEyedropperCursor = use;
}

void CanvasCursorManager::setEyedropperCursorCallback(EyedropperCursorCallback callback)
{
    m_eyedropperCursorCallback = std::move(callback);
}

void CanvasCursorManager::setActiveOverlay(QWidget* overlay)
{
    if (m_activeOverlay == overlay) {
        return;
    }

    if (m_activeOverlay) {
        m_activeOverlay->hide();
    }

    m_activeOverlay = overlay;

    if (m_activeOverlay && m_container) {
        m_activeOverlay->setParent(m_container);
        m_activeOverlay->setAttribute(Qt::WA_TransparentForMouseEvents, true);
    }
}

void CanvasCursorManager::setSuppressed(bool suppressed)
{
    if (m_suppressed == suppressed) {
        return;
    }

    m_suppressed = suppressed;
    if (m_suppressed) {
        if (m_brushCursorCallback) {
            m_brushCursorCallback(std::nullopt);
        }
        if (m_eyedropperCursorCallback) {
            m_eyedropperCursorCallback(std::nullopt);
        }
        m_wasOverCanvas = false;
        applyOverlayState(false);
    }
}

void CanvasCursorManager::updateCursorPosition(const QPoint& globalPos)
{
    if (m_suppressed || m_overlayDragActive) {
        if (m_brushCursorCallback) {
            m_brushCursorCallback(std::nullopt);
        }
        if (m_eyedropperCursorCallback) {
            m_eyedropperCursorCallback(std::nullopt);
        }
        m_wasOverCanvas = false;
        applyOverlayState(false);
        return;
    }

    bool exclusionInteractionActive = false;
    for (const auto& widget : m_cursorExclusionWidgets) {
        if (exclusionWidgetHasActiveInteraction(widget)) {
            exclusionInteractionActive = true;
            break;
        }
    }

    const bool overCanvas
        = !isUiDragActive() && !exclusionInteractionActive && isOverCanvas(globalPos);

    if (overCanvas && m_cursorResolver) {
        auto cursor = m_cursorResolver(globalPos);
        if (cursor.has_value()) {
            m_hasRequestedCursor = true;
            m_requestedCursor = *cursor;
        }
    }

    const bool showToolOverlay = overCanvas && !m_hasRequestedCursor;
    if (m_useGLBrushCursor && m_brushCursorCallback) {
        m_brushCursorCallback(showToolOverlay ? std::optional<QPoint>(globalPos) : std::nullopt);
    }
    if (m_useGLEyedropperCursor && m_eyedropperCursorCallback) {
        m_eyedropperCursorCallback(
            showToolOverlay ? std::optional<QPoint>(globalPos) : std::nullopt);
    }

    m_wasOverCanvas = overCanvas;
    applyOverlayState(overCanvas);

    if (overCanvas && m_activeOverlay) {
        positionOverlay(globalPos);
    }
}

void CanvasCursorManager::refreshCursorPosition()
{
    updateCursorPosition(activeCursorPosition());
}

QPoint CanvasCursorManager::activeCursorPosition() const
{
    const auto nativePos
        = ruwa::services::input::StylusInputManager::instance().nativeCursorPosition();
    return nativePos.value_or(QCursor::pos());
}

void CanvasCursorManager::setOverlayDragActive(bool active)
{
    if (m_overlayDragActive == active) {
        return;
    }
    m_overlayDragActive = active;
    // Re-evaluate now: when a drag begins this hides the custom cursor (the dragged
    // on-canvas widget lags behind the pointer, so the pointer keeps crossing its edge
    // and would otherwise make the cursor flicker); when it ends, normal behavior resumes.
    refreshCursorPosition();
}

void CanvasCursorManager::addCursorExclusionWidget(QWidget* widget)
{
    if (!widget) {
        return;
    }
    for (const auto& w : m_cursorExclusionWidgets) {
        if (w == widget) {
            return;
        }
    }
    m_cursorExclusionWidgets.append(widget);
    widget->installEventFilter(this);
}

bool CanvasCursorManager::isOverCanvas(const QPoint& globalPos) const
{
    if (!m_container || !m_canvasWidget) {
        return false;
    }

    QWidget* topLevel = m_container->window();
    if (!topLevel || !topLevel->isActiveWindow()) {
        // The GL cursor belongs to Ruwa's active canvas interaction. It must not
        // remain painted over an inactive window while Windows shows the real
        // system pointer for the foreground application.
        return false;
    }

    const QPoint canvasLocalPos = m_canvasWidget->mapFromGlobal(globalPos);
    if (!m_canvasWidget->rect().contains(canvasLocalPos)) {
        return false;
    }
    if (m_overlayWidget && m_overlayWidget->isVisible()) {
        const QPoint overlayLocalPos = m_overlayWidget->mapFromGlobal(globalPos);
        if (m_overlayWidget->rect().contains(overlayLocalPos)) {
            return false;
        }
    }
    for (const auto& w : m_cursorExclusionWidgets) {
        if (exclusionWidgetContainsGlobalPos(w, globalPos)) {
            return false;
        }
    }

    // A floating dock panel (or any other real widget) can sit geometrically on
    // top of the canvas. Those are not in m_cursorExclusionWidgets, and while the
    // pointer is inside one no mouse-move events reach this manager — so the GL
    // cursor would freeze at its last on-canvas position instead of disappearing.
    // Verify the topmost widget under the cursor actually belongs to the canvas
    // subtree. Mouse-transparent on-canvas overlays (zoom info, etc.) are skipped
    // by widgetAt and fall through to the GL widget, so they still count as canvas.
    // A synchronously dispatched Ruwa WinTab event has already passed the
    // routing manager's topmost-widget test (or belongs to an active captured
    // canvas stroke). Repeating QApplication::widgetAt here would perform
    // another full widget-tree/window hit test for every 200-266+ Hz packet.
    if (!ruwa::services::input::StylusInputManager::instance().isDispatchingNativeInput()) {
        if (QWidget* topmost = QApplication::widgetAt(globalPos)) {
            if (!isManagedCanvasWidget(topmost)) {
                return false;
            }
        }
    }

    return true;
}

void CanvasCursorManager::applyOverlayState(bool overCanvas)
{
    if (!m_canvasWidget) {
        return;
    }

    auto applyCursor = [this](const QCursor& cursor) {
        m_canvasWidget->setCursor(cursor);
        if (m_container) {
            m_container->setCursor(cursor);
        }
    };
    auto clearCursor = [this]() {
        m_canvasWidget->unsetCursor();
        if (m_container) {
            m_container->unsetCursor();
        }
    };

    if (overCanvas && m_hasRequestedCursor) {
        applyCursor(QCursor(m_requestedCursor));
        if (m_activeOverlay) {
            m_activeOverlay->hide();
        }
    } else if (overCanvas && (m_useGLBrushCursor || m_useGLEyedropperCursor)) {
        applyCursor(QCursor(Qt::BlankCursor));
        if (m_activeOverlay) {
            m_activeOverlay->hide();
        }
    } else if (overCanvas && m_activeOverlay) {
        applyCursor(QCursor(Qt::BlankCursor));
        m_activeOverlay->show();
        if (m_overlayWidget) {
            m_activeOverlay->stackUnder(m_overlayWidget);
        }
    } else {
        clearCursor();
        if (m_activeOverlay) {
            m_activeOverlay->hide();
        }

        if (QWidget* hoveredWidget = QApplication::widgetAt(QCursor::pos())) {
            if (isManagedCanvasWidget(hoveredWidget)) {
                hoveredWidget->unsetCursor();
            }
        }
    }
}

bool CanvasCursorManager::isManagedCanvasWidget(const QWidget* widget) const
{
    if (!widget) {
        return false;
    }

    if (widget == m_container) {
        return true;
    }

    const QWidget* current = widget;
    while (current) {
        if (current == m_canvasWidget) {
            return true;
        }
        current = current->parentWidget();
    }

    return false;
}

void CanvasCursorManager::positionOverlay(const QPoint& globalPos)
{
    if (!m_container || !m_activeOverlay) {
        return;
    }

    const QPoint containerTopLeft = m_container->mapToGlobal(QPoint(0, 0));
    const QPoint localPos = globalPos - containerTopLeft;
    const QSize size = m_activeOverlay->size();
    const QRect containerRect = m_container->rect();

    int x = localPos.x() - size.width() / 2;
    int y = localPos.y() - size.height() / 2;

    if (size.width() <= containerRect.width() && size.height() <= containerRect.height()) {
        x = qBound(containerRect.left(), x, containerRect.right() - size.width());
        y = qBound(containerRect.top(), y, containerRect.bottom() - size.height());
    }

    m_activeOverlay->move(x, y);
}

} // namespace ruwa::ui::workspace
