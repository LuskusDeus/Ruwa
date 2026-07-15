// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C A N V A S   C U R S O R   M A N A G E R
// ==========================================================================

#ifndef RUWA_UI_WIDGETS_WORKSPACE_CANVASCURSORMANAGER_CANVASCURSORMANAGER_H
#define RUWA_UI_WIDGETS_WORKSPACE_CANVASCURSORMANAGER_CANVASCURSORMANAGER_H

#include <QMetaObject>
#include <QObject>
#include <QPoint>
#include <QPointer>
#include <QWidget>
#include <QWindow>
#include <Qt>

#include <functional>
#include <optional>
#include <QList>

namespace ruwa::ui::workspace {

/**
 * @brief Manages cursor overlay for the canvas area only.
 *
 * When the cursor is over the canvas (OpenGL widget), the system cursor is
 * hidden and a custom overlay widget is shown instead. When the cursor moves
 * over other widgets (BrushControlOverlay, BrushPackPanel, etc.), the system
 * cursor is restored and the overlay is hidden.
 *
 * The manager does NOT affect overlays like BrushControlOverlay or BrushPackPanel —
 * those widgets manage their own cursors independently.
 */
class CanvasCursorManager : public QObject {
    Q_OBJECT

public:
    /// @param overlayWidget Optional widget (e.g. BrushControlOverlay) that overlays the canvas.
    ///        Event filter is installed on it too, so we update when cursor leaves it (e.g. to
    ///        canvas).
    explicit CanvasCursorManager(QWidget* container, QWidget* canvasWidget,
        QWidget* overlayWidget = nullptr, QObject* parent = nullptr);
    ~CanvasCursorManager() override;

    /// Set the active overlay widget to show instead of cursor. nullptr = use system cursor.
    void setActiveOverlay(QWidget* overlay);

    /// Request a specific system cursor when over canvas (e.g. in transform mode).
    /// Clears overlay mode and shows the requested cursor instead of BlankCursor.
    void setRequestedCursor(Qt::CursorShape shape);

    /// Clear the requested cursor override. Normal overlay behavior resumes.
    void clearRequestedCursor();

    /// Optional callback to resolve cursor for a given global position (e.g. transform handle
    /// hover). Called when cursor is over canvas. Return value overrides requested cursor for that
    /// position.
    using CursorResolver = std::function<std::optional<Qt::CursorShape>(const QPoint& globalPos)>;
    void setCursorResolver(CursorResolver resolver);

    /// When true, use GL-rendered brush cursor (inversion) instead of QWidget overlay.
    void setUseGLBrushCursor(bool use);

    /// Callback for GL brush cursor. Contains the global position when the cursor
    /// should be visible; std::nullopt means that it should be hidden.
    using BrushCursorCallback = std::function<void(const std::optional<QPoint>& globalPos)>;
    void setBrushCursorCallback(BrushCursorCallback callback);

    /// When true, use GL-rendered eyedropper cursor (magnifier, color border, invert border).
    void setUseGLEyedropperCursor(bool use);

    /// Callback for GL eyedropper cursor. Contains the global position when the cursor
    /// should be visible; std::nullopt means that it should be hidden.
    using EyedropperCursorCallback = std::function<void(const std::optional<QPoint>& globalPos)>;
    void setEyedropperCursorCallback(EyedropperCursorCallback callback);

    /// Call from parent's mouseMoveEvent to update overlay position and visibility.
    void updateCursorPosition(const QPoint& globalPos);

    /// Re-evaluate cursor visibility after state changes such as a tool switch.
    /// Uses the direct WinTab position while native routing owns the stylus.
    void refreshCursorPosition();

    /// Returns the active pointer position for canvas interactions.
    QPoint activeCursorPosition() const;

    /// Check if the given global position is over the canvas (not over BrushControlOverlay, etc.)
    bool isOverCanvas(const QPoint& globalPos) const;

    /// Add widget that excludes custom cursor when visible and under the cursor (e.g. MessagePopup)
    void addCursorExclusionWidget(QWidget* widget);

    /// Temporarily suppress cursor changes while canvas loading/appearance animation is active.
    void setSuppressed(bool suppressed);
    bool isSuppressed() const { return m_suppressed; }

    /// Suppress the custom canvas cursor while an on-canvas overlay widget (stylus joystick,
    /// brush control overlay) is being dragged. Those widgets trail the pointer with a lag,
    /// so the pointer repeatedly crosses their edge; without this the cursor would flicker.
    void setOverlayDragActive(bool active);

protected:
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    void ensureWindowTracking();
    void applyOverlayState(bool overCanvas);
    bool isManagedCanvasWidget(const QWidget* widget) const;
    void positionOverlay(const QPoint& globalPos);

    QPointer<QWidget> m_container;
    QPointer<QWidget> m_canvasWidget;
    QPointer<QWidget> m_overlayWidget; ///< Widget above cursor (BrushControlOverlay, etc.)
    QList<QPointer<QWidget>>
        m_cursorExclusionWidgets; ///< MessagePopup, etc. — hide custom cursor when over these
    QPointer<QWidget> m_activeOverlay;
    QPointer<QWidget>
        m_windowFilterTarget; ///< Top-level window we installed filter on (for multi-monitor)
    QPointer<QWindow>
        m_trackedWindowHandle; ///< Native window handle used for screenChanged tracking
    QMetaObject::Connection m_screenChangedConnection;

    bool m_wasOverCanvas = false;
    bool m_hasRequestedCursor = false;
    Qt::CursorShape m_requestedCursor = Qt::ArrowCursor;
    CursorResolver m_cursorResolver;
    bool m_useGLBrushCursor = false;
    BrushCursorCallback m_brushCursorCallback;
    bool m_useGLEyedropperCursor = false;
    EyedropperCursorCallback m_eyedropperCursorCallback;
    bool m_suppressed = false;
    bool m_overlayDragActive
        = false; ///< An on-canvas overlay widget is being dragged (laggy follow).
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WIDGETS_WORKSPACE_CANVASCURSORMANAGER_CANVASCURSORMANAGER_H
