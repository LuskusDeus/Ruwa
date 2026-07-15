// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   T A B L E T   T O   M O U S E   E V E N T   F I L T E R
// ==========================================================================
// When AA_SynthesizeMouseForUnhandledTabletEvents is false (needed for canvas
// pressure-sensitive drawing), Qt does not synthesize mouse events from tablet
// events. This filter synthesizes mouse events for stylus input outside the
// canvas so that UI elements (buttons, sliders, etc.) respond to stylus taps.
// ==========================================================================

#ifndef RUWA_APP_TABLETTOMOUSEEVENTFILTER_H
#define RUWA_APP_TABLETTOMOUSEEVENTFILTER_H

#include <QApplication>
#include <QCursor>
#include <QPoint>
#include <QObject>
#include <QPointer>

class QTabletEvent;
class QPoint;
class QWidget;

namespace ruwa {

namespace ui::widgets {
class SmoothScrollArea;
}

/**
 * @brief Global event filter that synthesizes mouse events from tablet events
 *        for widgets outside the canvas drawing area.
 *
 * Installed on QApplication. Intercepts TabletPress, TabletMove, TabletRelease
 * and converts them to MouseButtonPress, MouseMove, MouseButtonRelease when
 * the target widget is not the canvas drawing surface.
 */
class TabletToMouseEventFilter : public QObject {
    Q_OBJECT

public:
    explicit TabletToMouseEventFilter(QObject* parent = nullptr);
    ~TabletToMouseEventFilter() override;

    bool eventFilter(QObject* watched, QEvent* event) override;

    /// Call when a canvas panel is created so this filter runs first (before canvas event filters).
    static void ensureRunsFirst(::QApplication* app);

private:
    QWidget* resolveMouseTarget(QWidget* watchedWidget, const QPoint& globalPos) const;
    static ui::widgets::SmoothScrollArea* findSmoothScrollArea(QWidget* widget);
    static bool shouldUseStylusSwipeForWidget(QWidget* widget);
    void clearPendingStylusSwipe();
    void updateHoverTarget(QWidget* target, const QPoint& globalPos);
    void clearHoverTarget(const QPoint& globalPos);
    void updateTabletCursorOverride(QWidget* target);
    void clearTabletCursorOverride();
    static bool isCanvasWidget(QWidget* widget);
    static QCursor effectiveCursorForWidget(QWidget* widget);

    QPointer<QWidget> m_activeMouseTarget;
    QPointer<QWidget> m_hoverTarget;
    QPointer<QWidget> m_cursorOverrideTarget;
    QPointer<ui::widgets::SmoothScrollArea> m_pendingScrollArea;
    QPointer<QWidget> m_pendingPressTarget;
    QPoint m_pendingPressGlobalPos;
    Qt::MouseButton m_pendingPressButton = Qt::NoButton;
    Qt::MouseButtons m_pendingPressButtons = Qt::NoButton;
    Qt::KeyboardModifiers m_pendingPressModifiers = Qt::NoModifier;
    bool m_stylusSwipeDragging = false;
    bool m_tabletCursorOverrideActive = false;
    bool m_dispatchingSyntheticMouseEvent = false;
};

} // namespace ruwa

#endif // RUWA_APP_TABLETTOMOUSEEVENTFILTER_H
