// SPDX-License-Identifier: MPL-2.0

// ListCollapseAnimator.cpp
#include "ListCollapseAnimator.h"

#include <QBoxLayout>
#include <QEasingCurve>
#include <QLayoutItem>
#include <QPainter>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QSpacerItem>
#include <QVariantAnimation>
#include <QWidget>

#include <algorithm>
#include <limits>

namespace ruwa::ui::widgets {

namespace {

/// Placeholder that occupies the collapsing run's layout slot. Its fixed height
/// shrinks toward zero (driving the box layout to slide neighbours together)
/// while the captured snapshot scales down and fades out.
class CollapsingSnapshot final : public QWidget {
public:
    CollapsingSnapshot(const QPixmap& snapshot, int fullHeight, QWidget* parent)
        : QWidget(parent)
        , m_snapshot(snapshot)
        , m_fullHeight(qMax(0, fullHeight))
    {
        // Purely decorative: never steal hover/click from the live list.
        setAttribute(Qt::WA_TransparentForMouseEvents, true);
        setAttribute(Qt::WA_TranslucentBackground, true);
        setFocusPolicy(Qt::NoFocus);
        setFixedHeight(m_fullHeight);
    }

    /// @param progress 0 (untouched) .. 1 (fully collapsed). Expected pre-eased.
    void setProgress(qreal progress)
    {
        m_progress = qBound(0.0, progress, 1.0);
        setFixedHeight(qMax(0, qRound(m_fullHeight * (1.0 - m_progress))));
        update();
    }

protected:
    void paintEvent(QPaintEvent*) override
    {
        if (m_snapshot.isNull() || height() <= 0) {
            return;
        }

        QPainter painter(this);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.setOpacity(qMax(0.0, 1.0 - m_progress));

        const qreal dpr = m_snapshot.devicePixelRatio() > 0.0 ? m_snapshot.devicePixelRatio() : 1.0;
        const qreal pw = m_snapshot.width() / dpr;
        const qreal ph = m_snapshot.height() / dpr;

        // Shrink toward the snapshot's own centre so it reads as the row pulling
        // inward as the gap closes. Horizontally centred in the (full-width) slot;
        // vertically centred on the shrinking widget, which clips to a thinning band.
        const qreal scale = 1.0 - 0.16 * m_progress;
        const qreal w = pw * scale;
        const qreal h = ph * scale;
        const qreal x = (width() - w) / 2.0;
        const qreal y = (height() - h) / 2.0;
        painter.drawPixmap(
            QRectF(x, y, w, h), m_snapshot, QRectF(0, 0, m_snapshot.width(), m_snapshot.height()));
    }

private:
    QPixmap m_snapshot;
    int m_fullHeight = 0;
    qreal m_progress = 0.0;
};

} // namespace

struct ListCollapseAnimator::ActiveCollapse {
    QPointer<QWidget> snapshot; // CollapsingSnapshot in the layout slot
    QPointer<QBoxLayout> layout; // owning layout (for clean removal)
    QVariantAnimation* animation = nullptr;
    std::function<void()> onFinished;
    bool finishing = false; // re-entrancy guard
};

ListCollapseAnimator::ListCollapseAnimator(QObject* parent)
    : QObject(parent)
{
}

ListCollapseAnimator::~ListCollapseAnimator()
{
    finishAll();
}

void ListCollapseAnimator::collapseRange(QBoxLayout* layout, QWidget* content, int startIndex,
    int endIndex, int durationMs, std::function<void()> onFinished)
{
    const auto runFinishedNow = [&onFinished]() {
        if (onFinished) {
            onFinished();
        }
    };

    if (!layout || !content || startIndex < 0 || endIndex < startIndex
        || endIndex >= layout->count()) {
        runFinishedNow();
        return;
    }

    // Measure the run: bounding box of its widget items (snapshot source) and the
    // total vertical space it occupies (placeholder start height).
    int top = std::numeric_limits<int>::max();
    int bottom = std::numeric_limits<int>::min();
    int fullHeight = 0;
    int widgetCount = 0;
    for (int i = startIndex; i <= endIndex; ++i) {
        QLayoutItem* item = layout->itemAt(i);
        if (!item) {
            continue;
        }
        if (QWidget* w = item->widget()) {
            const QRect g = w->geometry();
            top = qMin(top, g.top());
            bottom = qMax(bottom, g.bottom() + 1);
            fullHeight += g.height();
            ++widgetCount;
        } else {
            fullHeight += item->sizeHint().height();
        }
    }

    if (widgetCount == 0 || bottom <= top) {
        // Nothing visible to snapshot — just drop the items and finish.
        for (int i = endIndex; i >= startIndex; --i) {
            QLayoutItem* item = layout->takeAt(i);
            if (!item) {
                continue;
            }
            if (QWidget* w = item->widget()) {
                w->hide();
                w->deleteLater();
            }
            delete item;
        }
        runFinishedNow();
        return;
    }

    const QRect snapRect(0, top, qMax(1, content->width()), bottom - top);
    const QPixmap snapshot = content->grab(snapRect);

    // Tear out the run (bottom-up so indices stay valid) and replace it with one
    // placeholder. Widgets are hidden + deleteLater()'d; the caller's model can
    // already treat them as gone.
    for (int i = endIndex; i >= startIndex; --i) {
        QLayoutItem* item = layout->takeAt(i);
        if (!item) {
            continue;
        }
        if (QWidget* w = item->widget()) {
            w->hide();
            w->deleteLater();
        }
        delete item;
    }

    auto* snap = new CollapsingSnapshot(snapshot, fullHeight, content);
    layout->insertWidget(startIndex, snap);
    snap->show();

    auto* collapse = new ActiveCollapse;
    collapse->snapshot = snap;
    collapse->layout = layout;
    collapse->onFinished = std::move(onFinished);

    auto* anim = new QVariantAnimation(this);
    anim->setStartValue(0.0);
    anim->setEndValue(1.0);
    anim->setDuration(durationMs > 0 ? durationMs : kDefaultDurationMs);
    anim->setEasingCurve(QEasingCurve::InOutCubic);
    collapse->animation = anim;

    QPointer<CollapsingSnapshot> snapGuard(snap);
    connect(anim, &QVariantAnimation::valueChanged, this, [this, snapGuard](const QVariant& value) {
        if (snapGuard) {
            snapGuard->setProgress(value.toReal());
        }
        emit stepped();
    });
    connect(anim, &QVariantAnimation::finished, this,
        [this, collapse]() { finishCollapse(collapse, /*jumpToEnd=*/false); });

    m_active.append(collapse);
    anim->start();
}

void ListCollapseAnimator::finishCollapse(ActiveCollapse* collapse, bool jumpToEnd)
{
    if (!collapse || collapse->finishing) {
        return;
    }
    collapse->finishing = true;

    if (collapse->animation) {
        collapse->animation->stop();
        collapse->animation->deleteLater();
        collapse->animation = nullptr;
    }

    if (collapse->snapshot) {
        if (collapse->layout) {
            collapse->layout->removeWidget(collapse->snapshot);
        }
        collapse->snapshot->deleteLater();
        collapse->snapshot = nullptr;
    }

    m_active.removeAll(collapse);

    std::function<void()> cb = std::move(collapse->onFinished);
    delete collapse;

    if (jumpToEnd) {
        emit stepped();
    }
    if (cb) {
        cb();
    }
}

void ListCollapseAnimator::finishAll()
{
    // Snapshot the list: each finish mutates m_active.
    const QVector<ActiveCollapse*> pending = m_active;
    for (ActiveCollapse* collapse : pending) {
        finishCollapse(collapse, /*jumpToEnd=*/true);
    }
}

} // namespace ruwa::ui::widgets
