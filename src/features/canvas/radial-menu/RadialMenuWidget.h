// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   R A D I A L   M E N U
// ======================================================================================
//   File        : RadialMenuWidget.h
//   Description : The circular canvas menu itself: presentation and input only.
//                 It knows nothing about commands — the controller hands it a
//                 page of resolved slots and receives an index back.
// ======================================================================================

#ifndef RUWA_UI_CANVAS_RADIALMENUWIDGET_H
#define RUWA_UI_CANVAS_RADIALMENUWIDGET_H

#include <QIcon>
#include <QKeySequence>
#include <QRegion>
#include <QPointF>
#include <QSizeF>
#include <QString>
#include <QVector>
#include <QWidget>

class QHideEvent;
class QShowEvent;
class QVariantAnimation;
class QWheelEvent;

namespace ruwa::shared::rendering {
class ICanvasBackdropSource;
}

namespace ruwa::ui::widgets {

/**
 * @brief Fully painted radial menu shown on canvas right-click.
 *
 * Geometry is derived from the theme scale on every rebuild, so the widget is
 * sized to hold the ring *and* its outward labels; the ring therefore always
 * sits at the widget centre and hit-testing is plain polar arithmetic around
 * that centre.
 */
class RadialMenuWidget : public QWidget {
    Q_OBJECT
    Q_PROPERTY(qreal showProgress READ showProgress WRITE setShowProgress)

public:
    /// One seat, already resolved to what should be drawn.
    struct Slot {
        QString title;
        QKeySequence shortcut;
        QIcon icon;
        bool enabled = true;
        bool opensPage = false;
        bool empty = false;
    };

    struct Page {
        QString title;
        QVector<Slot> items;
        bool canGoBack = false;
    };

    /// How a page swap should be animated.
    enum class PageTransition {
        None, ///< Snap, for the first page and for configuration previews
        IntoGroup, ///< The picked seat becomes the new hub
        OutOfGroup ///< The hub shrinks back into the seat it came from
    };

    explicit RadialMenuWidget(QWidget* parent = nullptr);

    /// Replace the shown page. Recomputes geometry; keeps the menu centred.
    ///
    /// @p pivotIndex names the seat the transition turns on: for IntoGroup it
    /// indexes the *outgoing* page (the seat just picked), for OutOfGroup the
    /// *incoming* one (the seat the hub folds back into).
    void setPage(
        const Page& page, PageTransition transition = PageTransition::None, int pivotIndex = -1);

    /// Show centred on @p centerInParent (parent coordinates), clamped so the
    /// whole ring stays inside the parent.
    ///
    /// @p armReleaseSelect opens the menu as a press-and-hold gesture: the
    /// button that opened it is still down and the canvas holds the implicit
    /// mouse grab, so hover and the picking release are tracked through the
    /// application event filter until that button comes back up.
    void showAt(const QPoint& centerInParent, bool armReleaseSelect = false);

    void hideMenu(bool animate = true);
    void hideImmediate();
    bool isMenuVisible() const { return isVisible() && !m_isHiding; }

    qreal showProgress() const { return m_showProgress; }
    void setShowProgress(qreal progress);

    /// One rounded rect the GPU backdrop pass has to frost and refract. The menu
    /// is not one silhouette but a scatter of small pieces (hub, buttons, label
    /// pills, title), so it hands the canvas a list instead of its own rect.
    struct BackdropShape {
        QRectF rect; ///< Widget-local logical coordinates
        qreal radius = 0.0;
        /// Relative to the menu's own fade: pieces entering or leaving during a
        /// page swap are frosted only as much as they are painted.
        qreal opacity = 1.0;
    };

    /// Glass pieces of the menu as currently laid out, empty while hidden.
    /// Sampled per canvas frame, so it follows the open animation.
    QVector<BackdropShape> backdropShapes() const;

    void setBackdropSource(ruwa::shared::rendering::ICanvasBackdropSource* source);

signals:
    /// A slot was activated (index into the current page's slots).
    void slotTriggered(int index);

    /// The hub was used as a back button on a nested page.
    void backRequested();

    /// The menu closed without activating anything.
    void dismissed();

protected:
    void paintEvent(QPaintEvent* event) override;
    void mouseMoveEvent(QMouseEvent* event) override;
    void mousePressEvent(QMouseEvent* event) override;
    void wheelEvent(QWheelEvent* event) override;
    void leaveEvent(QEvent* event) override;
    void showEvent(QShowEvent* event) override;
    void hideEvent(QHideEvent* event) override;
    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    /// Ring/label sizes in device-independent pixels for the current theme scale.
    struct Metrics {
        qreal hubRadius = 0.0;
        qreal ringRadius = 0.0;
        qreal slotRadius = 0.0; ///< Half the side of a slot's rounded square
        qreal slotCorner = 0.0; ///< Corner radius of a slot, as on the tools panel
        qreal labelGap = 0.0;
        qreal pillHeight = 0.0;
        qreal pillRadius = 0.0;
        qreal titlePillHeight = 0.0;
        qreal sideGutter = 0.0; ///< Horizontal room the widest label needs
        qreal verticalGutter = 0.0; ///< Room for labels plus the title pill
    };

    /// A piece of the outgoing page, kept only to fly it away and fade it out.
    /// Rects are stored relative to the widget centre so that the resize a page
    /// swap usually brings cannot shift them.
    struct Ghost {
        QRectF rect;
        qreal radius = 0.0;
        QPointF push; ///< Full displacement, reached at the end of the transition
        QIcon icon;
        QString title;
        QKeySequence shortcut;
        bool enabled = true;
        bool label = false; ///< Caption pill rather than a seat
    };

    void rebuildMetrics();
    QPointF center() const;

    /// Snapshot the current page as ghosts and set up the morphing piece. Runs
    /// before the new page is installed, so it still sees the old layout.
    void captureTransition(PageTransition transition, int pivotIndex, const Page& incoming);
    /// Second half of the setup, once the incoming layout is known.
    void finishTransitionSetup();
    void clearTransition();
    /// Eased 0..1 of the page transition; 1 when none is running.
    qreal transitionProgress() const;
    /// How far the incoming page has come up, 0..1. It enters the same way the
    /// menu itself does, only later in the transition.
    qreal transitionEntrance() const;
    /// Combined entrance weight of the content being painted: the open/close
    /// animation, narrowed further while a page transition brings a page in.
    qreal entranceAmount() const;

    /// Resting angle of a seat in radians, 0 = straight up, growing clockwise.
    qreal seatAngle(int index) const;
    /// Animated hover weight of a seat, 0..1. Two seats can both be non-zero
    /// while a hover crosses from one to the other.
    qreal hoverAmount(int index) const;
    /// Restart the crossfade after the hover target changed.
    void startHoverAnimation();
    /// How far seat @p index is pushed along the ring, in radians, by whatever
    /// is hovered. Seats next to the hovered one give way; the one opposite it
    /// does not move at all.
    qreal hoverAngleOffset(int index) const;

    /// Unit direction of slot @p index (screen coordinates, y down), including
    /// the hover push.
    QPointF slotDirection(int index) const;
    QPointF slotCenter(int index, qreal ringScale) const;
    /// Rounded-square button of slot @p index, centred on its seat.
    QRectF slotRect(int index, qreal ringScale) const;
    QRectF labelRect(int index, qreal ringScale) const;
    QSizeF labelSize(int index) const;
    /// Page-title pill under the ring (empty when the page has no title).
    QRectF titlePillRect() const;

    /// Slot under @p pos, or -1. Selection is by angle, not by the icon disc:
    /// anywhere in the ring band outside the hub counts (plus the seat's own
    /// label), which is what makes a radial menu quick to hit. Points beyond
    /// the band belong to the canvas, not to the seat that happens to share
    /// their angle.
    int slotAtPosition(const QPointF& pos) const;
    bool isInHub(const QPointF& pos) const;
    /// Outer edge of the clickable ring band.
    qreal ringHitRadius(qreal ringScale) const;
    /// Ring scale currently being painted (the open/close animation).
    qreal currentRingScale() const;

    /// The menu's actual shape: hub + ring band + label pills (+ the title
    /// pill), padded by @p padding. Used both as the widget mask — so clicks
    /// in the gaps reach the canvas instead of the bounding square — and as
    /// the "is this point ours" test.
    QRegion menuRegion(qreal padding) const;
    bool containsMenuPoint(const QPointF& pos) const;
    void updateMask();

    /// Single entry point for hover: repaints and picks the cursor.
    void applyHoverState(int slotIndex, bool hubHovered);
    /// Hover from a global position — used while another widget holds the grab.
    void updateHoverFromGlobal(const QPoint& globalPos);
    void activateSlot(int index);
    /// Close and report the dismissal.
    void dismiss();
    bool handleShortcutKey(int key);

    /// Frosted backdrop (or its opaque fallback) plus border and specular rim,
    /// the shared canvas-overlay glass, on one rounded rect. @p accent is the
    /// hover weight: it washes the glass and pulls the border to the accent
    /// colour.
    void paintGlassShape(
        QPainter& painter, const QRectF& rect, qreal radius, qreal accent = 0.0) const;

    /// Body of a caption pill: text plus its shortcut keycap.
    void paintLabelContent(QPainter& painter, const QRectF& pill, const QString& title,
        const QKeySequence& shortcut, const QColor& textColor) const;
    void paintGhosts(QPainter& painter) const;
    /// The piece that turns a seat into the hub, or the hub back into a seat.
    void paintMorph(QPainter& painter) const;

    void paintHoverWedge(QPainter& painter, qreal ringScale) const;
    void paintHub(QPainter& painter, qreal ringScale) const;
    void paintSlots(QPainter& painter, qreal ringScale) const;
    void paintLabels(QPainter& painter, qreal ringScale) const;
    void paintTitle(QPainter& painter) const;

    Page m_page;
    Metrics m_metrics;
    QVector<QSizeF> m_labelSizes;

    int m_hoveredSlot = -1;
    bool m_hubHovered = false;
    /// Hover weights the animation interpolates towards the current target, and
    /// the values they started from when the target last changed.
    QVector<qreal> m_slotHover;
    QVector<qreal> m_slotHoverFrom;
    qreal m_hubHover = 0.0;
    qreal m_hubHoverFrom = 0.0;
    /// The highlight wedge is one rotating object, not one per seat: it fades
    /// in on the seat it appears over, then swings round to each next seat.
    /// Angle is continuous radians measured like a seat angle (0 = straight up).
    qreal m_wedgeAngle = 0.0;
    qreal m_wedgeAngleFrom = 0.0;
    qreal m_wedgeAngleTo = 0.0;
    /// A swing that starts from rest eases in and out; one that interrupts a
    /// swing already in flight must start at full speed instead, or a hover
    /// swept quickly across seats keeps restarting inside the slow lead-in and
    /// the wedge barely moves.
    bool m_wedgeSwingFromRest = true;
    qreal m_wedgeOpacity = 0.0;
    qreal m_wedgeOpacityFrom = 0.0;
    qreal m_wedgeOpacityTo = 0.0;
    bool m_isHiding = false;
    /// Press-and-hold gesture in progress: the next release picks.
    bool m_releaseSelectArmed = false;
    bool m_activated = false; ///< Suppresses dismissed() after a real pick
    qreal m_showProgress = 0.0;

    ruwa::shared::rendering::ICanvasBackdropSource* m_backdropSource = nullptr;
    /// Page transition. The morph rects are centre-relative like the ghosts.
    bool m_transitionActive = false;
    bool m_transitionForward = true;
    /// Seat of the incoming page the morph lands on; only set going back, where
    /// that seat must not also be drawn by the incoming page itself.
    int m_transitionPivot = -1;
    QVector<Ghost> m_ghosts;
    QRectF m_morphFrom;
    QRectF m_morphTo;
    qreal m_morphFromRadius = 0.0;
    qreal m_morphToRadius = 0.0;
    QIcon m_morphIcon;
    qreal m_transitionRaw = 0.0;

    QVariantAnimation* m_progressAnim = nullptr;
    QVariantAnimation* m_hoverAnim = nullptr;
    QVariantAnimation* m_transitionAnim = nullptr;
};

} // namespace ruwa::ui::widgets

#endif // RUWA_UI_CANVAS_RADIALMENUWIDGET_H
