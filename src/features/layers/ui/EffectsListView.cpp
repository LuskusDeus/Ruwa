// SPDX-License-Identifier: MPL-2.0

// EffectsListView.cpp
#include "EffectsListView.h"

#include "shared/widgets/layout/SmoothScrollArea.h"
#include "shared/widgets/reorderlist/AnimatedListLayout.h"
#include "shared/widgets/reorderlist/ListDragDrop.h"
#include "shared/widgets/reorderlist/ReorderableRowWidget.h"

#include <QApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QPainter>
#include <QParallelAnimationGroup>
#include <QPixmap>
#include <QPropertyAnimation>
#include <QRegion>
#include <QVariantAnimation>
#include <QVBoxLayout>

namespace ruwa::ui::workspace {

using ruwa::ui::widgets::AnimatedListLayout;
using ruwa::ui::widgets::ListDragDrop;
using ruwa::ui::widgets::ReorderableRowWidget;
using ruwa::ui::widgets::SmoothScrollArea;

namespace {

// Matches the engine's hard-coded 300 ms ghost flight (ListDragDrop) and the
// layout's default slide duration, so the source fade-out, the neighbour slide
// and the flying ghost all run on one clock during a drop settle.
constexpr int kAnimMs = 300;

// Matches EffectCard's kDragDimOpacity (the layers panel's 0.35 drag dim): the
// source-collapse fade starts at exactly the brightness the dragged card had.
constexpr qreal kSourceDimOpacity = 0.35;

// Card heights are reported in device pixels (the layout runs with
// scaleRowHeights=false), so the full height is just effectiveRowHeight().
int rowFullHeight(ReorderableRowWidget* row)
{
    return row ? qMax(0, row->effectiveRowHeight()) : 0;
}

// Reveal only the top `visibleHeight` px of the row via a clip mask. A clip mask
// also clips the row's child widgets, so this works for the composite effect
// cards (unlike a paint-time opacity, which would only fade the card
// background). Used for the reveal-in and removal roll-ups, where the row's
// neighbours only ever approach from below, so a top anchor stays flush.
void setRowVisibleMask(ReorderableRowWidget* row, int visibleHeight)
{
    if (!row)
        return;
    const int width = qMax(0, row->width());
    const int fullHeight = rowFullHeight(row);
    const int clamped = qBound(0, visibleHeight, fullHeight);
    if (clamped >= fullHeight) {
        row->clearMask();
        return;
    }
    row->setMask(QRegion(0, 0, width, clamped));
}

// Lightweight stand-in that fades out a static snapshot of the drag source,
// mirroring the layers panel where the dimmed source row fades in place while
// the neighbours slide over its slot. EffectCards are child-widget containers,
// so animating the live card would re-render its whole subtree (labels, spin
// boxes, a live gradient preview) every frame -> stutter on tall cards.
// Rendering the card once and blitting the pixmap at an animated opacity stays
// smooth regardless of card complexity.
class FadingSnapshot : public QWidget {
public:
    FadingSnapshot(const QPixmap& pixmap, QWidget* parent)
        : QWidget(parent)
        , m_pixmap(pixmap)
    {
        setAttribute(Qt::WA_TransparentForMouseEvents);
        // No system background: without this the widget's rect can get erased
        // to an opaque flat colour by the backing store, which reads as an
        // empty hole that "clips" the cards sliding across it.
        setAttribute(Qt::WA_TranslucentBackground);
    }

    void setFadeOpacity(qreal v)
    {
        m_opacity = qBound(0.0, v, 1.0);
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_pixmap.isNull() || m_opacity <= 0.0)
            return;
        QPainter p(this);
        p.setOpacity(m_opacity);
        p.drawPixmap(0, 0, m_pixmap);
    }

private:
    QPixmap m_pixmap;
    qreal m_opacity = 1.0;
};

} // namespace

EffectsListView::EffectsListView(QWidget* parent)
    : QWidget(parent)
{
    auto* mainLayout = new QVBoxLayout(this);
    mainLayout->setContentsMargins(0, 0, 0, 0);
    mainLayout->setSpacing(0);

    m_scroll = new SmoothScrollArea(this);
    m_scroll->setFillBackground(false);
    m_scroll->setVerticalScrollBarPolicy(Qt::ScrollBarAsNeeded);
    m_scroll->setContentWidthFixedToViewport(true);
    mainLayout->addWidget(m_scroll);

    m_content = new QWidget();
    m_content->setObjectName("effects_list_content");
    m_content->setAttribute(Qt::WA_TranslucentBackground);
    m_content->installEventFilter(this);
    m_scroll->setWidget(m_content);

    m_layout = new AnimatedListLayout(m_content, this);
    m_layout->setScaleRowHeights(false); // effect cards report device-pixel heights
    m_layout->setRowSpacing(8);
    // Keep the neighbour-slide on the same clock as our source fade-out and the
    // ghost flight, so a drop settle stays flush instead of desyncing.
    m_layout->setAnimationDuration(kAnimMs);
    connect(m_layout, &AnimatedListLayout::contentHeightChanged, this,
        &EffectsListView::onContentHeightChanged);

    m_dragDrop = new ListDragDrop(m_content, m_layout, this);
    // Flat list: no depth resolver, no indent metrics.
    connect(m_dragDrop, &ListDragDrop::sourceRowCollapseRequested, this,
        &EffectsListView::onSourceRowCollapseRequested);
    connect(m_dragDrop, &ListDragDrop::ghostSettled, this, &EffectsListView::onGhostSettled);
    connect(m_dragDrop, &ListDragDrop::dragCancelled, this, &EffectsListView::onDragCancelled);
}

EffectsListView::~EffectsListView()
{
    if (m_dragCursorOverride) {
        QApplication::restoreOverrideCursor();
        m_dragCursorOverride = false;
    }
}

// ============================================================================
// Queries
// ============================================================================

ReorderableRowWidget* EffectsListView::rowById(const QUuid& id) const
{
    for (auto* row : m_rows) {
        if (row && row->itemId() == id)
            return row;
    }
    return nullptr;
}

int EffectsListView::indexOfRow(const QUuid& id) const
{
    for (int i = 0; i < m_rows.size(); ++i) {
        if (m_rows[i] && m_rows[i]->itemId() == id)
            return i;
    }
    return -1;
}

QList<QPair<QUuid, ReorderableRowWidget*>> EffectsListView::buildEntries() const
{
    QList<QPair<QUuid, ReorderableRowWidget*>> entries;
    entries.reserve(m_rows.size());
    for (auto* row : m_rows) {
        if (row)
            entries.append({ row->itemId(), row });
    }
    return entries;
}

void EffectsListView::relayout(bool animate)
{
    m_layout->updateLayout(buildEntries(), animate, {}, {});
}

void EffectsListView::stopSyncAnim()
{
    if (m_syncAnim) {
        m_syncAnim->stop();
        m_syncAnim->deleteLater();
        m_syncAnim = nullptr;
    }
    // stop() never emits finished(), so an interrupted transition must finish
    // its cleanup here: rows that were mid-removal would otherwise survive as
    // orphaned, half-masked widgets floating over the list. Kept-row masks and
    // the content height are re-established by the follow-up syncRows().
    for (const auto& row : m_rowsToDelete) {
        if (row) {
            row->hide();
            row->deleteLater();
        }
    }
    m_rowsToDelete.clear();
    m_animating = false;
}

// ============================================================================
// Reconcile / animate
// ============================================================================

void EffectsListView::syncRows(const QList<ReorderableRowWidget*>& orderedRows,
    const QSet<QUuid>& newIds, const QList<ReorderableRowWidget*>& removedRows, bool animate)
{
    stopSyncAnim();

    const bool doAnimate = animate && !m_skipNextAnim && isVisible();
    m_skipNextAnim = false;

    const QSet<ReorderableRowWidget*> removedSet(removedRows.begin(), removedRows.end());
    const QSet<ReorderableRowWidget*> keepSet(orderedRows.begin(), orderedRows.end());

    // Record pre-transition Y for rows that survive (for the slide).
    QHash<QUuid, int> oldY;
    for (auto* row : m_rows) {
        if (row)
            oldY.insert(row->itemId(), row->y());
    }
    const int oldContentH = m_content->height();

    // Hide rows that vanish purely due to filtering (not model removals).
    for (auto* row : m_rows) {
        if (row && !keepSet.contains(row) && !removedSet.contains(row)) {
            row->hide();
        }
    }

    // Adopt the new order.
    m_rows = orderedRows;
    const int w = m_content->width();
    for (auto* row : m_rows) {
        if (!row)
            continue;
        row->setParent(m_content);
        if (w > 0)
            row->resize(w, rowFullHeight(row));
        row->clearMask();
        row->show();
    }

    // Snap the layout to the final positions/height (no engine animation — we
    // drive the transition ourselves so it also covers reveal/roll-up).
    m_animating = doAnimate;
    m_layout->updateLayout(buildEntries(), false, {}, {});
    const int newContentH = m_layout->contentHeight();

    if (!doAnimate) {
        m_animating = false;
        m_content->setFixedHeight(newContentH);
        for (auto* row : removedRows) {
            if (row) {
                row->hide();
                row->deleteLater();
            }
        }
        if (m_scroll)
            m_scroll->refreshScrollGeometry();
        return;
    }

    m_syncAnim = new QParallelAnimationGroup(this);

    // Kept / new rows: slide from old position, or reveal in place.
    for (auto* row : m_rows) {
        if (!row)
            continue;
        const QUuid id = row->itemId();
        const int finalY = row->y();

        if (newIds.contains(id) || !oldY.contains(id)) {
            // Newly created (or re-appearing) row: reveal via mask at its slot.
            row->move(0, finalY);
            const int fullH = rowFullHeight(row);
            setRowVisibleMask(row, newIds.contains(id) ? 0 : fullH);
            if (newIds.contains(id)) {
                auto* hAnim = new QVariantAnimation(m_syncAnim);
                hAnim->setDuration(kAnimMs);
                hAnim->setEasingCurve(QEasingCurve::OutCubic);
                hAnim->setStartValue(0);
                hAnim->setEndValue(fullH);
                connect(hAnim, &QVariantAnimation::valueChanged, row,
                    [row](const QVariant& v) { setRowVisibleMask(row, v.toInt()); });
                m_syncAnim->addAnimation(hAnim);
            }
        } else {
            const int oy = oldY.value(id);
            if (oy != finalY) {
                row->move(0, oy);
                auto* posAnim = new QPropertyAnimation(row, "pos", m_syncAnim);
                posAnim->setDuration(kAnimMs);
                posAnim->setEasingCurve(QEasingCurve::InOutCubic);
                posAnim->setStartValue(QPoint(0, oy));
                posAnim->setEndValue(QPoint(0, finalY));
                m_syncAnim->addAnimation(posAnim);
            }
        }
    }

    // Removed rows: roll up in place, then delete on finish.
    m_rowsToDelete.clear();
    for (auto* row : removedRows) {
        if (!row)
            continue;
        m_rowsToDelete.append(row);
        const int startH = rowFullHeight(row);
        auto* hAnim = new QVariantAnimation(m_syncAnim);
        hAnim->setDuration(kAnimMs);
        hAnim->setEasingCurve(QEasingCurve::InOutCubic);
        hAnim->setStartValue(startH);
        hAnim->setEndValue(0);
        connect(hAnim, &QVariantAnimation::valueChanged, row,
            [row](const QVariant& v) { setRowVisibleMask(row, v.toInt()); });
        m_syncAnim->addAnimation(hAnim);
    }

    // Content height grows/shrinks alongside.
    m_targetContentHeight = newContentH;
    m_content->setFixedHeight(oldContentH);
    auto* contentAnim = new QVariantAnimation(m_syncAnim);
    contentAnim->setDuration(kAnimMs);
    contentAnim->setEasingCurve(QEasingCurve::InOutCubic);
    contentAnim->setStartValue(oldContentH);
    contentAnim->setEndValue(newContentH);
    connect(contentAnim, &QVariantAnimation::valueChanged, this,
        [this](const QVariant& v) { m_content->setFixedHeight(v.toInt()); });
    m_syncAnim->addAnimation(contentAnim);

    connect(m_syncAnim, &QParallelAnimationGroup::finished, this, [this]() {
        m_animating = false;
        m_syncAnim = nullptr;
        for (auto* row : m_rows) {
            if (row)
                row->clearMask();
        }
        for (const auto& row : m_rowsToDelete) {
            if (row) {
                row->hide();
                row->deleteLater();
            }
        }
        m_rowsToDelete.clear();
        m_content->setFixedHeight(m_targetContentHeight);
        if (m_scroll)
            m_scroll->refreshScrollGeometry();
    });

    m_syncAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

// ============================================================================
// Content height
// ============================================================================

void EffectsListView::onContentHeightChanged(int newHeight)
{
    // Ignored while our transition animation, an active drag, or the settle
    // window manages the content height explicitly (syncRows always sets it).
    if (m_animating || m_dragActive || m_dragSettling || !m_content)
        return;
    m_content->setFixedHeight(newHeight);
    if (m_scroll)
        m_scroll->refreshScrollGeometry();
}

// ============================================================================
// Drag
// ============================================================================

void EffectsListView::beginDrag(const QUuid& id, const QPoint& globalPos)
{
    if (!m_dragDrop || m_dragDrop->isDragging())
        return;
    if (m_animating || m_layout->isAnimating())
        return;

    auto* row = rowById(id);
    if (!row)
        return;

    m_draggingId = id;
    m_dragSettling = false;
    row->setDragging(true);
    m_dragDrop->startDrag(id, { id }, row, globalPos);

    m_dragActive = true;
    qApp->installEventFilter(this);

    // The grip holds Qt's *implicit* mouse grab (not exposed via mouseGrabber),
    // so its open-hand cursor would stick to the pointer during and after the
    // drag. An application override cursor masks it for the whole drag and, when
    // balanced by restoreOverrideCursor() in endDragSession(), forces the cursor
    // to be re-evaluated on release.
    if (!m_dragCursorOverride) {
        QApplication::setOverrideCursor(Qt::ClosedHandCursor);
        m_dragCursorOverride = true;
    }
}

void EffectsListView::endDragSession()
{
    m_dragActive = false;
    m_draggingId = QUuid();
    qApp->removeEventFilter(this);
    if (m_dragCursorOverride) {
        QApplication::restoreOverrideCursor();
        m_dragCursorOverride = false;
    }
}

bool EffectsListView::eventFilter(QObject* watched, QEvent* event)
{
    if (watched == m_content && event->type() == QEvent::Resize) {
        const int w = m_content->width();
        for (auto* row : m_rows) {
            if (row)
                row->resize(w, row->height());
        }
    }

    if (m_dragActive && m_dragDrop) {
        switch (event->type()) {
        case QEvent::MouseMove: {
            auto* me = static_cast<QMouseEvent*>(event);
            m_dragDrop->updateDrag(me->globalPosition().toPoint());
            return true;
        }
        case QEvent::MouseButtonRelease: {
            auto* me = static_cast<QMouseEvent*>(event);
            if (me->button() != Qt::LeftButton)
                break;
            endDragSession();
            // Do NOT clear the source's dragging (dim) state here: it must stay
            // dimmed through its fade-out collapse, exactly like the layers panel,
            // which only resets dragging in onGhostSettled/onDragCancelled.
            // Clearing it on release makes the source card flash bright the
            // instant it should begin disappearing.
            // Freeze content-height reactions until the settle commits, so the
            // shrinking layout (source excluded) can't clip the collapsing card.
            m_dragSettling = true;
            m_dragDrop->endDrag(me->globalPosition().toPoint());
            return true;
        }
        case QEvent::KeyPress: {
            auto* ke = static_cast<QKeyEvent*>(event);
            if (ke->key() == Qt::Key_Escape) {
                m_dragDrop->cancelDrag();
                return true;
            }
            break;
        }
        default:
            break;
        }
    }
    return QWidget::eventFilter(watched, event);
}

void EffectsListView::onSourceRowCollapseRequested(const QUuid& sourceId)
{
    auto* row = rowById(sourceId);
    if (!row)
        return;

    if (m_sourceCollapseAnim) {
        m_sourceCollapseAnim->stop();
        m_sourceCollapseAnim = nullptr;
    }
    if (m_sourceSnapshot) {
        m_sourceSnapshot->deleteLater();
        m_sourceSnapshot = nullptr;
    }

    const int startH = rowFullHeight(row);
    if (startH <= 0 || row->width() <= 0 || !isVisible()) {
        row->hide();
        return;
    }

    // Reference behaviour (LayerListView::animateSourceRowCollapse): the dimmed
    // source fades out IN PLACE while the neighbours slide to close its slot
    // and the ghost flies to the gap. Fade a static snapshot rather than the
    // live card (see FadingSnapshot).
    //
    // Render with the drag-dim graphics effect REMOVED: QWidget::render() on a
    // widget with an active QGraphicsEffect is unreliable (can produce a blank
    // pixmap). Bake the dim into the fade instead: the animation starts at the
    // same 0.35 the live card showed, so there is no brightness jump at release.
    // The row is hidden immediately below, so dropping its drag state early
    // cannot flash it bright.
    row->setDragging(false);
    const qreal dpr = row->devicePixelRatioF();
    QPixmap snapshot(row->size() * dpr);
    snapshot.setDevicePixelRatio(dpr);
    snapshot.fill(Qt::transparent);
    row->render(&snapshot);

    auto* shot = new FadingSnapshot(snapshot, m_content);
    shot->setGeometry(row->x(), row->y(), row->width(), startH);
    shot->setFadeOpacity(kSourceDimOpacity); // no full-brightness first frame
    shot->show();
    // Stack the ghost UNDER the live rows: the neighbours sliding onto the
    // source's slot cover it progressively (the "card squashes the ghost"
    // read), and no overlay sits above the moving cards to clip them.
    shot->lower();
    m_sourceSnapshot = shot;

    row->hide();
    row->clearMask();

    QPointer<FadingSnapshot> sp(shot);
    m_sourceCollapseAnim = new QVariantAnimation(this);
    m_sourceCollapseAnim->setDuration(kAnimMs);
    m_sourceCollapseAnim->setEasingCurve(QEasingCurve::InOutCubic);
    m_sourceCollapseAnim->setStartValue(kSourceDimOpacity);
    m_sourceCollapseAnim->setEndValue(0.0);
    connect(m_sourceCollapseAnim, &QVariantAnimation::valueChanged, shot, [sp](const QVariant& v) {
        if (sp)
            sp->setFadeOpacity(v.toReal());
    });
    connect(m_sourceCollapseAnim, &QVariantAnimation::finished, this, [this, sp]() {
        if (sp)
            sp->deleteLater();
        m_sourceSnapshot = nullptr;
        m_sourceCollapseAnim = nullptr;
    });
    m_sourceCollapseAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void EffectsListView::onGhostSettled(const QUuid& movedId, int dropInsertIndex, int /*targetDepth*/)
{
    endDragSession();
    m_dragSettling = false;
    if (m_sourceCollapseAnim) {
        m_sourceCollapseAnim->stop();
        m_sourceCollapseAnim = nullptr;
    }
    if (m_sourceSnapshot) {
        m_sourceSnapshot->deleteLater();
        m_sourceSnapshot = nullptr;
    }
    if (m_dragDrop)
        m_dragDrop->destroyGhost();
    m_layout->clearDragEndState();

    for (auto* row : m_rows) {
        if (row) {
            row->setDragging(false);
            row->clearMask();
        }
    }

    // dropInsertIndex is the insertion point in the current list (source still
    // counted). Convert to the post-removal index moveLayerEffect expects.
    const int oldIndex = indexOfRow(movedId);
    int finalIndex = dropInsertIndex;
    if (oldIndex >= 0 && dropInsertIndex > oldIndex) {
        finalIndex = dropInsertIndex - 1;
    }

    // The settle animation already moved neighbours; the commit rebuild should
    // snap (no second slide).
    m_skipNextAnim = true;
    emit reordered(movedId, finalIndex);
}

void EffectsListView::onDragCancelled()
{
    endDragSession();
    m_dragSettling = false;
    if (m_sourceCollapseAnim) {
        m_sourceCollapseAnim->stop();
        m_sourceCollapseAnim = nullptr;
    }
    if (m_sourceSnapshot) {
        m_sourceSnapshot->deleteLater();
        m_sourceSnapshot = nullptr;
    }
    m_layout->clearDragEndState();
    for (auto* row : m_rows) {
        if (row) {
            row->setDragging(false);
            row->clearMask();
            row->show();
        }
    }
    relayout(false); // snap back to resting positions
}

} // namespace ruwa::ui::workspace
