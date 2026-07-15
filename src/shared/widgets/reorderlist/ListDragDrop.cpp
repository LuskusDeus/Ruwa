// SPDX-License-Identifier: MPL-2.0

// ListDragDrop.cpp
#include "shared/widgets/reorderlist/ListDragDrop.h"
#include "shared/widgets/reorderlist/ReorderableRowWidget.h"
#include "shared/widgets/reorderlist/AnimatedListLayout.h"

#include "features/theme/manager/ThemeManager.h"

#include <QPainter>
#include <QPainterPath>
#include <QPropertyAnimation>
#include <QVariantAnimation>
#include <QParallelAnimationGroup>
#include <QApplication>
#include <QGraphicsBlurEffect>
#include <QGraphicsScene>
#include <QGraphicsPixmapItem>
#include <QImage>

namespace ruwa::ui::widgets {

using namespace ruwa::ui::core;

namespace {

QPixmap blurPixmap(const QPixmap& src, qreal radius)
{
    if (src.isNull())
        return src;
    const qreal dpr = src.devicePixelRatio();
    QGraphicsScene scene;
    QGraphicsPixmapItem item;
    item.setPixmap(src);
    auto* blur = new QGraphicsBlurEffect;
    blur->setBlurRadius(radius);
    blur->setBlurHints(QGraphicsBlurEffect::PerformanceHint);
    item.setGraphicsEffect(blur);
    scene.addItem(&item);

    QImage img(src.size(), QImage::Format_ARGB32_Premultiplied);
    img.setDevicePixelRatio(dpr);
    img.fill(Qt::transparent);
    QPainter p(&img);
    const QRectF logical(0, 0, src.width() / dpr, src.height() / dpr);
    scene.render(&p, logical, logical);
    p.end();

    QPixmap out = QPixmap::fromImage(img);
    out.setDevicePixelRatio(dpr);
    return out;
}

} // namespace

// ============================================================================
// DragGhostWidget
// ============================================================================

DragGhostWidget::DragGhostWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    if (!parent) {
        setWindowFlags(Qt::FramelessWindowHint | Qt::Tool | Qt::WindowStaysOnTopHint);
    }
}

void DragGhostWidget::setSnapshot(const QPixmap& pixmap)
{
    m_snapshot = pixmap;
    m_multiCount = 0;
    setVisualContentSize(pixmap.size() / pixmap.devicePixelRatio());
}

void DragGhostWidget::setMultiDragCount(int count, int width)
{
    m_multiCount = count;
    m_snapshot = QPixmap(); // Clear snapshot, use label mode

    const auto& tm = ThemeManager::instance();
    int w = (width > 0) ? width : tm.scaled(160);
    int h = tm.scaled(32);
    setVisualContentSize(QSize(w, h));
}

void DragGhostWidget::setVisualContentSize(const QSize& size)
{
    QSize clamped = size.expandedTo(QSize(1, 1));
    if (m_visualContentSize == clamped)
        return;
    m_visualContentSize = clamped;
    int pad = visualPadding();
    setFixedSize(m_visualContentSize.width() + pad * 2, m_visualContentSize.height() + pad * 2);
    update();
}

QPoint DragGhostWidget::contentTopLeft() const
{
    return QPoint(
        (width() - m_visualContentSize.width()) / 2, (height() - m_visualContentSize.height()) / 2);
}

int DragGhostWidget::visualPadding() const
{
    return ThemeManager::instance().scaled(24);
}

void DragGhostWidget::setGhostOpacity(qreal v)
{
    m_opacity = v;
    update();
}

void DragGhostWidget::setGhostScale(qreal v)
{
    if (qFuzzyCompare(m_scale, v))
        return;
    m_scale = qBound(0.01, v, 1.0);
    update();
}

void DragGhostWidget::setGhostRotation(qreal v)
{
    if (qFuzzyCompare(m_rotation, v))
        return;
    m_rotation = v;
    update();
}

void DragGhostWidget::setRowSnapshots(
    const QList<QPixmap>& snapshots, const QList<int>& targetYsInParent)
{
    if (!snapshots.isEmpty()) {
        m_rowSnapshots = snapshots;
        m_morphProgress = 0.0;
        m_rowSkipPositioning.clear();
    }
    if (!targetYsInParent.isEmpty()) {
        m_targetYsInParent = targetYsInParent;
    }
    update();
}

void DragGhostWidget::setRowSkipPositioning(const QList<bool>& skip)
{
    m_rowSkipPositioning = skip;
    update();
}

void DragGhostWidget::setMorphProgress(qreal v)
{
    m_morphProgress = qBound(0.0, v, 1.0);
    update();
}

void DragGhostWidget::setBlurredBackdrop(const QPixmap& fullParentBlurred)
{
    m_blurredBackdrop = fullParentBlurred;
    update();
}

void DragGhostWidget::setBackdropOpacity(qreal v)
{
    v = qBound(0.0, v, 1.0);
    if (qFuzzyCompare(m_backdropOpacity, v))
        return;
    m_backdropOpacity = v;
    update();
}

void DragGhostWidget::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setRenderHint(QPainter::SmoothPixmapTransform);

    // Apply scale around center (for fade-out shrink animation)
    if (!qFuzzyCompare(m_scale, 1.0) || !qFuzzyIsNull(m_rotation)) {
        p.translate(width() / 2.0, height() / 2.0);
        if (!qFuzzyIsNull(m_rotation)) {
            p.rotate(m_rotation);
        }
        p.scale(m_scale, m_scale);
        p.translate(-width() / 2.0, -height() / 2.0);
    }

    // --- Frosted-glass backdrop: blurred region of parent window painted
    //     under the content with rounded corners. Fades in over ~1 sec.
    if (!m_blurredBackdrop.isNull() && m_backdropOpacity > 0.001) {
        const QPoint contentPos = contentTopLeft();
        const QRectF contentRect(contentPos, QSizeF(m_visualContentSize));
        // Glass matches the snapshot bounds exactly (no halo).
        QRectF glassRect = contentRect;

        // Source region in parent (topLevel) logical coordinates.
        // QPainter handles DPR scaling against the pixmap automatically.
        QPoint widgetTopLeftInParent = pos();
        QRectF srcLogical(QPointF(widgetTopLeftInParent) + glassRect.topLeft(), glassRect.size());

        const qreal radius = 8.0;
        QPainterPath clip;
        clip.addRoundedRect(glassRect, radius, radius);

        // Backdrop has its own opacity (animated independently at fade-in
        // and on drop). It is NOT multiplied by m_opacity so the glass is
        // fully visible during drag even when the snapshot is at 0.85.
        const qreal glassAlpha = m_backdropOpacity;

        p.save();
        p.setOpacity(glassAlpha);
        p.setClipPath(clip);
        p.drawPixmap(glassRect, m_blurredBackdrop, srcLogical);

        // Stronger frosted tint + subtle vertical sheen
        const auto& c = ThemeManager::instance().colors();
        QColor tintTop = c.surface;
        tintTop.setAlpha(140);
        QColor tintBot = c.surface;
        tintBot.setAlpha(90);
        QLinearGradient grad(glassRect.topLeft(), glassRect.bottomLeft());
        grad.setColorAt(0.0, tintTop);
        grad.setColorAt(1.0, tintBot);
        p.fillRect(glassRect, grad);
        p.restore();

        // Highlight border on top
        p.save();
        p.setOpacity(glassAlpha);
        QColor border = c.text;
        border.setAlpha(70);
        p.setPen(QPen(border, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(glassRect.adjusted(0.5, 0.5, -0.5, -0.5), radius, radius);
        p.restore();
    }

    // --- Morph mode: pill fades out, N row snapshots fly to their positions ---
    if (!m_rowSnapshots.isEmpty() && m_morphProgress >= 0.0) {
        const auto& c = ThemeManager::instance().colors();
        auto& tm = ThemeManager::instance();
        const QPoint contentPos = contentTopLeft();
        const QRectF contentRect(contentPos, QSizeF(m_visualContentSize));
        int pillW = tm.scaled(160);
        int pillH = tm.scaled(32);
        QRectF pillRect(contentRect.center().x() - pillW / 2.0,
            contentRect.center().y() - pillH / 2.0, pillW, pillH);

        // Draw pill with fading opacity — disappears in first 40% of morph
        qreal pillOpacity = m_morphProgress < 0.4 ? m_opacity * (1.0 - m_morphProgress / 0.4) : 0.0;
        if (pillOpacity > 0.01) {
            p.setOpacity(pillOpacity);
            QRectF r = pillRect.adjusted(0.5, 0.5, -0.5, -0.5);
            QColor bg = c.surface;
            bg.setAlpha(220);
            p.setPen(Qt::NoPen);
            p.setBrush(bg);
            p.drawRoundedRect(r, 6, 6);
            QColor bord = c.primary;
            bord.setAlpha(100);
            p.setPen(QPen(bord, 1.0));
            p.setBrush(Qt::NoBrush);
            p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 5.5, 5.5);
            QFont font;
            font.setFamily(c.fonts.uiFont);
            font.setPixelSize(tm.scaledFontSize(9));
            p.setFont(font);
            p.setPen(c.text);
            p.drawText(pillRect.toRect(), Qt::AlignCenter,
                QString("Dragging %1 layers").arg(m_rowSnapshots.size()));
        }

        // Draw N row snapshots flying from pill centre to their target positions
        // Snapshots appear quickly (visible by 20% morph) so they're not hidden by pill
        qreal rowOpacity = m_morphProgress < 0.2 ? m_opacity * (m_morphProgress / 0.2) : m_opacity;
        if (rowOpacity > 0.01 && !m_rowSnapshots.isEmpty()) {
            // Centre of the content area in parent coordinates
            QPoint widgetTopLeft = pos();
            QPointF contentTopLeftInParent = QPointF(widgetTopLeft) + QPointF(contentPos);
            QPointF pillCentre(contentTopLeftInParent.x() + contentRect.width() / 2.0,
                contentTopLeftInParent.y() + contentRect.height() / 2.0);

            for (int i = 0; i < m_rowSnapshots.size() && i < m_targetYsInParent.size(); ++i) {
                const QPixmap& snap = m_rowSnapshots[i];
                if (snap.isNull())
                    continue;

                int snapH = snap.height() / snap.devicePixelRatio();
                int snapW = snap.width() / snap.devicePixelRatio();

                // Target position in parent coordinates
                QPointF targetTopLeft(contentTopLeftInParent.x(), m_targetYsInParent[i]);

                // Source position: pill centre, or target if row already in position
                bool skipPos = (i < m_rowSkipPositioning.size() && m_rowSkipPositioning[i]);
                QPointF sourceTopLeft = skipPos
                    ? targetTopLeft
                    : QPointF(pillCentre.x() - snapW / 2.0, pillCentre.y() - snapH / 2.0);

                // Interpolate (no-op when source == target)
                QPointF currentTopLeft
                    = sourceTopLeft + m_morphProgress * (targetTopLeft - sourceTopLeft);

                // Convert to local coordinates (subtract ghost top-left)
                QPointF localPos = currentTopLeft - QPointF(widgetTopLeft);

                p.setOpacity(rowOpacity);
                p.drawPixmap(localPos.toPoint(), snap);
            }
        }
        return;
    }

    p.setOpacity(m_opacity);

    if (m_multiCount > 0 || (!m_rowSnapshots.isEmpty() && m_morphProgress < 0.0)) {
        // Multi-drag label — compact pill centered in widget (ghost may be expanded)
        const auto& c = ThemeManager::instance().colors();
        auto& tm = ThemeManager::instance();
        const QPoint contentPos = contentTopLeft();
        const QRectF contentRect(contentPos, QSizeF(m_visualContentSize));
        int pillW = tm.scaled(160);
        int pillH = tm.scaled(32);
        QRectF pillRect(contentRect.center().x() - pillW / 2.0,
            contentRect.center().y() - pillH / 2.0, pillW, pillH);
        QRectF r = pillRect.adjusted(0.5, 0.5, -0.5, -0.5);

        QColor bg = c.surface;
        bg.setAlpha(220);
        p.setPen(Qt::NoPen);
        p.setBrush(bg);
        p.drawRoundedRect(r, 6, 6);

        QColor bord = c.primary;
        bord.setAlpha(100);
        p.setPen(QPen(bord, 1.0));
        p.setBrush(Qt::NoBrush);
        p.drawRoundedRect(r.adjusted(0.5, 0.5, -0.5, -0.5), 5.5, 5.5);

        QFont font;
        font.setFamily(c.fonts.uiFont);
        int count = m_multiCount > 0 ? m_multiCount : m_rowSnapshots.size();
        font.setPixelSize(tm.scaledFontSize(9));
        p.setFont(font);
        p.setPen(c.text);
        p.drawText(pillRect.toRect(), Qt::AlignCenter, QString("Dragging %1 layers").arg(count));
    } else if (!m_snapshot.isNull()) {
        // Snapshot mode
        p.drawPixmap(contentTopLeft(), m_snapshot);
    }
}

// ============================================================================
// DropIndicatorWidget
// ============================================================================

DropIndicatorWidget::DropIndicatorWidget(QWidget* parent)
    : QWidget(parent)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setAttribute(Qt::WA_TransparentForMouseEvents);
    setFixedHeight(3);
}

void DropIndicatorWidget::setIndentLevel(int depth)
{
    m_depth = depth;
    update();
}

void DropIndicatorWidget::setIndentMetrics(int indentPerLevel, int basePad)
{
    m_indentPerLevel = indentPerLevel;
    m_basePad = basePad;
    update();
}

void DropIndicatorWidget::setStyle(Style style)
{
    if (m_style == style) {
        return;
    }
    m_style = style;
    if (m_style == Style::DragLine) {
        setFixedHeight(3);
    } else {
        auto& tm = ThemeManager::instance();
        setFixedHeight(tm.scaled(20));
    }
    update();
}

void DropIndicatorWidget::setIndicatorIcon(const QPixmap& pixmap)
{
    m_icon = pixmap;
    update();
}

void DropIndicatorWidget::setIndicatorOpacity(qreal v)
{
    m_opacity = v;
    update();
}

void DropIndicatorWidget::paintEvent(QPaintEvent* e)
{
    Q_UNUSED(e);
    if (m_opacity <= 0)
        return;

    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setOpacity(m_opacity);

    const auto& c = ThemeManager::instance().colors();
    auto& tm = ThemeManager::instance();

    int indent = m_depth * tm.scaled(m_indentPerLevel) + tm.scaled(m_basePad);

    if (m_style == Style::DragLine) {
        // Draw indicator line
        QColor lineCol = c.primary;
        lineCol.setAlpha(200);

        // Circle at left
        qreal circleR = 3.0;
        p.setPen(Qt::NoPen);
        p.setBrush(lineCol);
        p.drawEllipse(QPointF(indent + circleR, height() / 2.0), circleR, circleR);

        // Line
        QPen pen(lineCol, 2.0);
        pen.setCapStyle(Qt::RoundCap);
        p.setPen(pen);
        p.drawLine(QPointF(indent + circleR * 2 + 2, height() / 2.0),
            QPointF(width() - tm.scaled(m_basePad), height() / 2.0));
        return;
    }

    const int rectX = indent;
    const int rectY = tm.scaled(2);
    const int rectW = qMax(1, width() - rectX - tm.scaled(m_basePad));
    const int rectH = qMax(1, height() - rectY * 2);

    QColor fill = c.primary;
    QRectF blockRect(rectX, rectY, rectW, rectH);

    p.setPen(Qt::NoPen);
    p.setBrush(fill);
    p.drawRoundedRect(blockRect, tm.scaled(6), tm.scaled(6));

    if (!m_icon.isNull()) {
        const int iconSide = qMin(rectH - tm.scaled(4), tm.scaled(12));
        if (iconSide > 0) {
            QPixmap icon
                = m_icon.scaled(iconSide, iconSide, Qt::KeepAspectRatio, Qt::SmoothTransformation);
            // Trim transparent padding from icon to keep arrow centered visually.
            QImage iconImg = icon.toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
            int minX = iconImg.width();
            int minY = iconImg.height();
            int maxX = -1;
            int maxY = -1;
            for (int y = 0; y < iconImg.height(); ++y) {
                const QRgb* scan = reinterpret_cast<const QRgb*>(iconImg.constScanLine(y));
                for (int x = 0; x < iconImg.width(); ++x) {
                    if (qAlpha(scan[x]) > 0) {
                        minX = qMin(minX, x);
                        minY = qMin(minY, y);
                        maxX = qMax(maxX, x);
                        maxY = qMax(maxY, y);
                    }
                }
            }
            if (maxX >= minX && maxY >= minY) {
                icon = icon.copy(QRect(QPoint(minX, minY), QPoint(maxX, maxY)));
            }

            QColor iconColor = c.textOnPrimary();
            QPixmap tinted(icon.size());
            tinted.fill(Qt::transparent);
            QPainter ip(&tinted);
            ip.drawPixmap(0, 0, icon);
            ip.setCompositionMode(QPainter::CompositionMode_SourceIn);
            ip.fillRect(tinted.rect(), iconColor);
            ip.end();

            const int iconX = rectX + (rectW - tinted.width()) / 2;
            const int iconY = rectY + (rectH - tinted.height()) / 2;
            p.drawPixmap(iconX, iconY, tinted);
        }
    }
}

// ============================================================================
// ListDragDrop
// ============================================================================

ListDragDrop::ListDragDrop(QWidget* viewport, AnimatedListLayout* layout, QObject* parent)
    : QObject(parent)
    , m_viewport(viewport)
    , m_layout(layout)
{
    m_dragFollowTimer.setInterval(12);
    m_dragFollowTimer.setTimerType(Qt::PreciseTimer);
    connect(&m_dragFollowTimer, &QTimer::timeout, this, &ListDragDrop::tickGhostFollow);
}

ListDragDrop::~ListDragDrop()
{
    destroyGhost();
}

// ============================================================================
// Drag Start
// ============================================================================

void ListDragDrop::startDrag(const QUuid& sourceId, const QSet<QUuid>& allSelectedIds,
    ReorderableRowWidget* sourceWidget, const QPoint& globalPos, const QSet<QUuid>& descendantIds,
    bool copyMode)
{
    if (m_dragging)
        return;
    m_dragging = true;
    m_copyMode = copyMode;

    m_sourceId = sourceId;
    m_draggedIds = allSelectedIds;
    m_descendantIds = descendantIds;
    if (m_draggedIds.isEmpty()) {
        m_draggedIds.insert(sourceId);
    }

    m_sourcePos = sourceWidget->mapToParent(QPoint(0, 0));
    m_dropInsertIndex = -1;
    m_dropTargetDepth = 0;

    // Remember flat index of source for target calculation
    m_sourceFlatIndex = sourceFlatIndex();

    createGhost(sourceWidget, globalPos);

    // Mark dragged rows
    sourceWidget->setDragging(true);

    emit dragStarted();
}

void ListDragDrop::createGhost(ReorderableRowWidget* sourceWidget, const QPoint& globalPos)
{
    destroyGhost();

    // Use window as parent for ghost (sits above everything)
    QWidget* topLevel = m_viewport->window();

    m_ghost = new DragGhostWidget(topLevel);

    if (m_draggedIds.size() > 1) {
        // Compact pill during drag (follows cursor); expands to layer width on release
        m_ghost->setMultiDragCount(m_draggedIds.size(), -1);
    } else if (m_draggedIds.size() == 1 && !m_descendantIds.isEmpty() && m_layout) {
        // Group with expanded children: composite snapshot of group + all descendants
        QSet<QUuid> excludeIds = allExcludeIds();
        QList<ReorderableRowWidget*> rowsToSnapshot;
        for (int i = 0; i < m_layout->entryCount(); ++i) {
            auto* w = m_layout->rowWidgetAtIndex(i);
            if (w && excludeIds.contains(w->itemId())) {
                rowsToSnapshot.append(w);
            }
        }

        if (rowsToSnapshot.size() > 1) {
            auto& tm = ThemeManager::instance();
            int scaledSpacing = tm.scaled(m_layout->rowSpacing());
            int totalH = 0;
            int totalW = 0;
            QList<QPixmap> rowPixmaps;
            for (int i = 0; i < rowsToSnapshot.size(); ++i) {
                auto* row = rowsToSnapshot[i];
                row->setDragging(false);
                row->setRowOpacity(1.0);
                QPixmap snap(row->size() * row->devicePixelRatioF());
                snap.setDevicePixelRatio(row->devicePixelRatioF());
                snap.fill(Qt::transparent);
                row->render(&snap);
                row->setDragging(true);
                rowPixmaps.append(snap);
                int snapH = snap.height() / snap.devicePixelRatio();
                int snapW = snap.width() / snap.devicePixelRatio();
                totalW = qMax(totalW, snapW);
                totalH += snapH + (i < rowsToSnapshot.size() - 1 ? scaledSpacing : 0);
            }

            qreal dpr = sourceWidget->devicePixelRatioF();
            QPixmap composite(QSize(totalW, totalH) * dpr);
            composite.setDevicePixelRatio(dpr);
            composite.fill(Qt::transparent);
            QPainter compPainter(&composite);
            compPainter.setRenderHint(QPainter::Antialiasing);
            int y = 0;
            for (const QPixmap& snap : rowPixmaps) {
                int snapH = snap.height() / snap.devicePixelRatio();
                compPainter.drawPixmap(0, y, snap);
                y += snapH + scaledSpacing;
            }
            compPainter.end();

            m_ghost->setSnapshot(composite);
        } else {
            // Fallback: single row (e.g. descendants not in layout yet)
            sourceWidget->setDragging(false);
            QPixmap snapshot(sourceWidget->size() * sourceWidget->devicePixelRatioF());
            snapshot.setDevicePixelRatio(sourceWidget->devicePixelRatioF());
            snapshot.fill(Qt::transparent);
            sourceWidget->render(&snapshot);
            sourceWidget->setDragging(true);
            m_ghost->setSnapshot(snapshot);
        }
    } else {
        // Single layer (no descendants)
        sourceWidget->setDragging(false);
        QPixmap snapshot(sourceWidget->size() * sourceWidget->devicePixelRatioF());
        snapshot.setDevicePixelRatio(sourceWidget->devicePixelRatioF());
        snapshot.fill(Qt::transparent);
        sourceWidget->render(&snapshot);
        sourceWidget->setDragging(true);
        m_ghost->setSnapshot(snapshot);
    }

    // Calculate offset
    QPoint widgetGlobal = sourceWidget->mapToGlobal(QPoint(0, 0));
    m_dragOffset = widgetGlobal - globalPos;

    // Position ghost
    QPoint ghostPos = mapGhostTargetPos(globalPos);
    m_dragGhostPos = ghostPos;
    m_dragGhostTargetPos = ghostPos;
    m_dragGhostVelocity = QPointF(0.0, 0.0);
    m_ghost->setGhostRotation(0.0);
    m_ghost->move(ghostPos);

    // Frosted-glass backdrop: grab parent window BEFORE showing the ghost so
    // the ghost itself is not part of the captured backdrop. Then blur and
    // fade it in over 1 sec under the snapshot.
    if (topLevel) {
        QPixmap windowGrab = topLevel->grab();
        QPixmap blurred = blurPixmap(windowGrab, 14.0);
        m_ghost->setBlurredBackdrop(blurred);
        m_ghost->setBackdropOpacity(0.0);

        if (m_backdropAnim)
            m_backdropAnim->stop();
        m_backdropAnim = new QPropertyAnimation(m_ghost, "backdropOpacity", m_ghost);
        m_backdropAnim->setDuration(300);
        m_backdropAnim->setStartValue(0.0);
        m_backdropAnim->setEndValue(1.0);
        m_backdropAnim->setEasingCurve(QEasingCurve::OutCubic);
        m_backdropAnim->start(QAbstractAnimation::DeleteWhenStopped);
    }

    m_ghost->show();
    m_ghost->raise();
    startGhostFollow();

    // Drop indicator
    m_indicator = new DropIndicatorWidget(m_viewport->parentWidget());
    m_indicator->setFixedWidth(m_viewport->width());
    m_indicator->hide();
}

// ============================================================================
// Drag Move
// ============================================================================

void ListDragDrop::updateDrag(const QPoint& globalPos)
{
    if (!m_dragging || !m_ghost)
        return;

    m_dragGhostTargetPos = mapGhostTargetPos(globalPos);
    if (!m_dragFollowTimer.isActive()) {
        startGhostFollow();
    }

    // Calculate drop target in viewport coordinates.
    // Throttle updateDropTarget to ~40 Hz — the layout iteration and signal
    // emission it triggers are expensive at stylus input rates (200+ Hz).
    // Ghost animation runs on its own 12ms timer and is not affected.
    constexpr qint64 kDropTargetThrottleMs = 25;
    QPoint viewportPos = m_viewport->mapFromGlobal(globalPos);
    m_pendingDropViewportPos = viewportPos;

    bool throttled = m_hasDropTargetThrottle && m_dropTargetThrottle.isValid()
        && m_dropTargetThrottle.elapsed() < kDropTargetThrottleMs;

    if (!throttled) {
        updateDropTarget(viewportPos);
        m_dropTargetThrottle.start();
        m_hasDropTargetThrottle = true;
    }
}

void ListDragDrop::refreshDropTarget(const QPoint& globalPos)
{
    if (!m_dragging || !m_viewport) {
        return;
    }

    updateDropTarget(m_viewport->mapFromGlobal(globalPos));
    m_dropTargetThrottle.start();
    m_hasDropTargetThrottle = true;
}

QPoint ListDragDrop::mapGhostTargetPos(const QPoint& globalPos) const
{
    if (!m_viewport)
        return globalPos + m_dragOffset;
    QWidget* topLevel = m_viewport->window();
    QPoint contentTargetPos
        = topLevel ? topLevel->mapFromGlobal(globalPos + m_dragOffset) : (globalPos + m_dragOffset);
    if (m_ghost) {
        contentTargetPos -= m_ghost->contentTopLeft();
    }
    return contentTargetPos;
}

void ListDragDrop::startGhostFollow()
{
    m_dragFollowElapsed.start();
    if (!m_dragFollowTimer.isActive()) {
        m_dragFollowTimer.start();
    }
}

void ListDragDrop::stopGhostFollow()
{
    m_dragFollowTimer.stop();
    m_dragFollowElapsed.invalidate();
    m_dragGhostVelocity = QPointF(0.0, 0.0);
}

void ListDragDrop::tickGhostFollow()
{
    if (!m_dragging || !m_ghost) {
        stopGhostFollow();
        return;
    }

    const qreal dt = qBound(0.006, m_dragFollowElapsed.restart() / 1000.0, 0.025);
    const QPointF delta = m_dragGhostTargetPos - m_dragGhostPos;

    // Slightly softer spring-damper follow so the ghost feels smoother.
    const qreal stiffness = 620.0;
    const qreal damping = 32.0;
    const QPointF accel(delta.x() * stiffness - m_dragGhostVelocity.x() * damping,
        delta.y() * stiffness - m_dragGhostVelocity.y() * damping);

    m_dragGhostVelocity += accel * dt;
    m_dragGhostPos += m_dragGhostVelocity * dt;

    if (qAbs(delta.x()) < 0.35 && qAbs(delta.y()) < 0.35 && qAbs(m_dragGhostVelocity.x()) < 6.0
        && qAbs(m_dragGhostVelocity.y()) < 6.0) {
        m_dragGhostPos = m_dragGhostTargetPos;
        m_dragGhostVelocity = QPointF(0.0, 0.0);
    }

    m_ghost->move(qRound(m_dragGhostPos.x()), qRound(m_dragGhostPos.y()));

    const qreal targetTilt = qBound(-7.5, m_dragGhostVelocity.x() * 0.0105, 7.5);
    const qreal smoothedTilt
        = m_ghost->ghostRotation() + (targetTilt - m_ghost->ghostRotation()) * 0.22;
    m_ghost->setGhostRotation(smoothedTilt);
}

void ListDragDrop::updateDropTarget(const QPoint& viewportPos)
{
    if (!m_layout)
        return;

    const int rawIndex = m_layout->dropInsertIndexAtY(viewportPos.y());

    auto& tm = ThemeManager::instance();

    // Suggested nesting depth from the pointer X. Flat lists leave the indent
    // metrics at 0, so this stays 0 and no depth is ever reported.
    int suggestedDepth = 0;
    if (m_indentPerLevel > 0) {
        const int indentPerLevel = tm.scaled(m_indentPerLevel);
        const int basePad = tm.scaled(m_basePad);
        if (indentPerLevel > 0) {
            suggestedDepth
                = qMax(0, (viewportPos.x() - basePad + indentPerLevel / 2) / indentPerLevel);
        }
    }

    // Resolve the raw position into a concrete (index, depth). Tree lists supply
    // a resolver that clamps the index and derives depth from neighbours; flat
    // lists drop at the raw index, depth 0.
    int newIndex = rawIndex;
    int newDepth = 0;
    if (m_dropResolver) {
        const DropResolution r = m_dropResolver(rawIndex, suggestedDepth, m_draggedIds);
        newIndex = r.insertIndex;
        newDepth = r.depth;
    }

    const bool targetChanged = (newIndex != m_dropInsertIndex || newDepth != m_dropTargetDepth);
    m_dropInsertIndex = newIndex;
    m_dropTargetDepth = newDepth;

    // Scrolling moves the content relative to the viewport even when the
    // resolved drop index is unchanged, so refresh the indicator every time.
    if (m_indicator && m_viewport) {
        qreal targetY = m_layout->targetYForIndex(m_dropInsertIndex);
        int indicatorY = qRound(targetY) - tm.scaled(m_layout->rowSpacing()) / 2 - 1;

        QPoint indicatorPos = m_viewport->mapToParent(QPoint(0, indicatorY));
        m_indicator->move(indicatorPos);
        m_indicator->setIndentLevel(m_dropTargetDepth);
        m_indicator->setIndicatorOpacity(1.0);
        m_indicator->show();
        m_indicator->raise();
    }

    if (targetChanged) {
        emit dragMoved(m_dropInsertIndex);
    }
}

// ============================================================================
// Helper: all IDs to exclude from layout
// ============================================================================

QSet<QUuid> ListDragDrop::allExcludeIds() const
{
    QSet<QUuid> ids = m_draggedIds;
    ids.unite(m_descendantIds);
    return ids;
}

// ============================================================================
// Helper: source flat index
// ============================================================================

int ListDragDrop::sourceFlatIndex() const
{
    if (!m_layout)
        return -1;

    // Find source index by checking entries via Y position
    return m_layout->rowIndexAtY(m_sourcePos.y() + 1);
}

int ListDragDrop::draggedGapHeight() const
{
    if (!m_layout) {
        return m_fallbackRowHeight;
    }

    const QSet<QUuid> ids = allExcludeIds();
    int totalHeight = 0;
    int count = 0;
    for (int i = 0; i < m_layout->entryCount(); ++i) {
        auto* row = m_layout->rowWidgetAtIndex(i);
        if (!row || !ids.contains(row->itemId())) {
            continue;
        }
        totalHeight += row->effectiveRowHeight();
        ++count;
    }
    if (count > 1) {
        totalHeight += (count - 1) * m_layout->rowSpacing();
    }
    return totalHeight > 0 ? totalHeight : m_fallbackRowHeight;
}

// ============================================================================
// Helper: calculate correct ghost target Y
// ============================================================================

int ListDragDrop::calculateGhostTargetY() const
{
    if (!m_layout || m_dropInsertIndex < 0)
        return 0;

    // The drop insert index is where the item will be inserted.
    // Account for all excluded rows being removed.
    QSet<QUuid> excludeIds = allExcludeIds();

    // Count excluded entries before drop index
    int excludedBefore = 0;
    for (int i = 0; i < m_dropInsertIndex && i < m_layout->entryCount(); ++i) {
        auto* w = m_layout->rowWidgetAtIndex(i);
        if (w && excludeIds.contains(w->itemId())) {
            excludedBefore++;
        }
    }
    int effectiveIndex = m_dropInsertIndex - excludedBefore;

    // Use layout's per-row height data to calculate position
    auto& tm = ThemeManager::instance();
    int scaledSpacing = tm.scaled(m_layout->rowSpacing());
    int y = 0;
    int virtualIdx = 0;

    for (int i = 0; i < m_layout->entryCount(); ++i) {
        auto* w = m_layout->rowWidgetAtIndex(i);
        if (w && excludeIds.contains(w->itemId()))
            continue; // skip excluded
        if (virtualIdx >= effectiveIndex)
            break;
        y += m_layout->scaledRowHeightAtIndex(i) + scaledSpacing;
        virtualIdx++;
    }

    return y;
}

// ============================================================================
// Drag End
// ============================================================================

void ListDragDrop::endDrag(const QPoint& globalPos)
{
    if (!m_dragging)
        return;

    stopGhostFollow();

    // Always recalculate the drop target from the release position so the
    // drop lands where the pen actually lifted, not at a stale throttled pos.
    if (m_viewport) {
        updateDropTarget(m_viewport->mapFromGlobal(globalPos));
    }
    m_hasDropTargetThrottle = false;

    // Hide indicator immediately
    if (m_indicator) {
        m_indicator->hide();
    }

    // === Multi-drag path ===
    // Ghost stays alive — the view will snapshot rows, collapse them,
    // then call animateMultiGhostSettle() which flies the ghost to
    // the target and morphs into row cards before committing.
    if (m_draggedIds.size() > 1) {
        int insertIdx = m_dropInsertIndex;
        int depth = m_dropTargetDepth;
        QUuid srcId = m_sourceId;
        QSet<QUuid> ids = m_draggedIds;

        if (insertIdx < 0) {
            animateGhostFadeOut();
            return;
        }

        m_dragging = false;
        if (m_copyMode && m_layout) {
            m_layout->applyCopyDragEndState(insertIdx, draggedGapHeight());
        }

        // Do NOT fade/destroy ghost here — the view needs it alive
        // for the settle + morph animation.
        emit multiDragCompleted(srcId, ids, insertIdx, depth);
        return;
    }

    // === Single drag path (unchanged) ===

    // Determine if this is same Y position (accounting for all dragged entries)
    bool sameYPos = false;
    int srcDepth = 0;
    if (m_sourceFlatIndex >= 0) {
        int totalDraggedEntries = allExcludeIds().size();
        sameYPos = (m_dropInsertIndex < 0)
            || (m_dropInsertIndex >= m_sourceFlatIndex
                && m_dropInsertIndex <= m_sourceFlatIndex + totalDraggedEntries);
        auto* srcWidget = m_layout ? m_layout->rowWidgetAtIndex(m_sourceFlatIndex) : nullptr;
        srcDepth = srcWidget ? srcWidget->itemDepth() : 0;
    } else {
        sameYPos = (m_dropInsertIndex < 0);
    }

    bool sameDepth = (m_dropTargetDepth == srcDepth);

    // Path 1: No change at all — fade ghost and cancel
    if (m_copyMode && m_layout && m_dropInsertIndex >= 0) {
        m_layout->applyCopyDragEndState(m_dropInsertIndex, draggedGapHeight());
        m_dragging = false;
        animateGhostToTarget();
        return;
    }

    if (sameYPos && sameDepth) {
        animateGhostFadeOut();
        return;
    }

    // Path 2: Depth-only change (same Y, different depth)
    // Just fade ghost and commit — indent animation handles the visual
    if (sameYPos && !sameDepth) {
        m_dragging = false;

        // Fade and shrink ghost out independently
        if (m_ghost) {
            auto* group = new QParallelAnimationGroup(this);
            auto* fadeAnim = new QPropertyAnimation(m_ghost, "ghostOpacity", group);
            fadeAnim->setDuration(300);
            fadeAnim->setEasingCurve(QEasingCurve::InOutCubic);
            fadeAnim->setStartValue(m_ghost->ghostOpacity());
            fadeAnim->setEndValue(0.0);
            group->addAnimation(fadeAnim);

            auto* backdropAnim = new QPropertyAnimation(m_ghost, "backdropOpacity", group);
            backdropAnim->setDuration(300);
            backdropAnim->setEasingCurve(QEasingCurve::InOutCubic);
            backdropAnim->setStartValue(m_ghost->backdropOpacity());
            backdropAnim->setEndValue(0.0);
            group->addAnimation(backdropAnim);

            auto* scaleAnim = new QPropertyAnimation(m_ghost, "ghostScale", group);
            scaleAnim->setDuration(300);
            scaleAnim->setEasingCurve(QEasingCurve::InOutCubic);
            scaleAnim->setStartValue(m_ghost->ghostScale());
            scaleAnim->setEndValue(0.85);
            group->addAnimation(scaleAnim);

            auto* rotationAnim = new QPropertyAnimation(m_ghost, "ghostRotation", group);
            rotationAnim->setDuration(180);
            rotationAnim->setEasingCurve(QEasingCurve::OutCubic);
            rotationAnim->setStartValue(m_ghost->ghostRotation());
            rotationAnim->setEndValue(0.0);
            group->addAnimation(rotationAnim);

            connect(group, &QParallelAnimationGroup::finished, this, [this]() { destroyGhost(); });
            group->start(QAbstractAnimation::DeleteWhenStopped);
        }

        // Commit the move directly — ghostSettled triggers model update + rebuild
        int insertIdx = m_dropInsertIndex;
        int depth = m_dropTargetDepth;
        QUuid srcId = m_sourceId;
        emit ghostSettled(srcId, insertIdx, depth);
        return;
    }

    // Path 3: Full position change — collapse source, settle ghost, then commit
    if (m_layout && m_dropInsertIndex >= 0) {
        m_layout->applyDragEndState(allExcludeIds(), m_dropInsertIndex);
    }

    // Request source row collapse animation from the view
    emit sourceRowCollapseRequested(m_sourceId);

    // Animate ghost to the gap position + opacity to 1.0
    animateGhostToTarget();
}

void ListDragDrop::cancelDrag()
{
    if (!m_dragging)
        return;
    stopGhostFollow();

    // Hide indicator
    if (m_indicator) {
        m_indicator->hide();
    }

    // Clear state
    m_dropTargetDepth = 0;
    m_copyMode = false;

    // Clear any drag-end state
    if (m_layout) {
        m_layout->clearDragEndState();
    }

    // Animate ghost back to source
    animateGhostToSource();
}

void ListDragDrop::animateGhostFadeOut()
{
    stopGhostFollow();

    // Clean up drag state immediately so the view can restore rows
    m_dragging = false;
    m_dropTargetDepth = 0;
    m_copyMode = false;
    emit dragCancelled();

    if (!m_ghost) {
        destroyGhost();
        return;
    }

    // Ghost fades out and shrinks — purely visual, no state dependencies
    auto* group = new QParallelAnimationGroup(this);
    auto* fadeAnim = new QPropertyAnimation(m_ghost, "ghostOpacity", group);
    fadeAnim->setDuration(300);
    fadeAnim->setEasingCurve(QEasingCurve::InOutCubic);
    fadeAnim->setStartValue(m_ghost->ghostOpacity());
    fadeAnim->setEndValue(0.0);
    group->addAnimation(fadeAnim);

    auto* backdropAnim = new QPropertyAnimation(m_ghost, "backdropOpacity", group);
    backdropAnim->setDuration(300);
    backdropAnim->setEasingCurve(QEasingCurve::InOutCubic);
    backdropAnim->setStartValue(m_ghost->backdropOpacity());
    backdropAnim->setEndValue(0.0);
    group->addAnimation(backdropAnim);

    auto* scaleAnim = new QPropertyAnimation(m_ghost, "ghostScale", group);
    scaleAnim->setDuration(300);
    scaleAnim->setEasingCurve(QEasingCurve::InOutCubic);
    scaleAnim->setStartValue(m_ghost->ghostScale());
    scaleAnim->setEndValue(0.85);
    group->addAnimation(scaleAnim);

    auto* rotationAnim = new QPropertyAnimation(m_ghost, "ghostRotation", group);
    rotationAnim->setDuration(180);
    rotationAnim->setEasingCurve(QEasingCurve::OutCubic);
    rotationAnim->setStartValue(m_ghost->ghostRotation());
    rotationAnim->setEndValue(0.0);
    group->addAnimation(rotationAnim);

    connect(group, &QParallelAnimationGroup::finished, this, [this]() { destroyGhost(); });
    group->start(QAbstractAnimation::DeleteWhenStopped);
}

void ListDragDrop::animateGhostToTarget()
{
    stopGhostFollow();

    if (!m_ghost || !m_layout || !m_viewport) {
        onSettleAnimFinished();
        return;
    }

    // Get the gap Y calculated by applyDragEndState
    qreal gapY = m_layout->dragEndGapY();
    if (gapY < 0) {
        // Fallback
        gapY = calculateGhostTargetY();
    }

    QPoint targetViewport(0, qRound(gapY));
    QPoint targetGlobal = m_viewport->mapToGlobal(targetViewport);
    QWidget* topLevel = m_viewport->window();
    QPoint targetLocal = topLevel->mapFromGlobal(targetGlobal);
    if (m_ghost) {
        targetLocal -= m_ghost->contentTopLeft();
    }

    // Smooth 0.85→1.0 in first 80ms of flight to avoid pop; ghost is fully opaque before arrival
    QParallelAnimationGroup* settleGroup = new QParallelAnimationGroup(this);

    m_settleAnim = new QPropertyAnimation(m_ghost, "pos", settleGroup);
    m_settleAnim->setDuration(300);
    m_settleAnim->setEasingCurve(QEasingCurve::InOutCubic);
    m_settleAnim->setStartValue(m_ghost->pos());
    m_settleAnim->setEndValue(targetLocal);
    settleGroup->addAnimation(m_settleAnim);

    m_ghostOpacityAnim = new QPropertyAnimation(m_ghost, "ghostOpacity", settleGroup);
    m_ghostOpacityAnim->setDuration(80);
    m_ghostOpacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_ghostOpacityAnim->setStartValue(m_ghost->ghostOpacity());
    m_ghostOpacityAnim->setEndValue(1.0);
    settleGroup->addAnimation(m_ghostOpacityAnim);

    // Fade glass backdrop (and its border) out during the settle flight so
    // the rounded outline doesn't pop when the ghost is destroyed.
    auto* settleBackdropAnim = new QPropertyAnimation(m_ghost, "backdropOpacity", settleGroup);
    settleBackdropAnim->setDuration(300);
    settleBackdropAnim->setEasingCurve(QEasingCurve::InOutCubic);
    settleBackdropAnim->setStartValue(m_ghost->backdropOpacity());
    settleBackdropAnim->setEndValue(0.0);
    settleGroup->addAnimation(settleBackdropAnim);

    auto* rotationAnim = new QPropertyAnimation(m_ghost, "ghostRotation", settleGroup);
    rotationAnim->setDuration(160);
    rotationAnim->setEasingCurve(QEasingCurve::OutCubic);
    rotationAnim->setStartValue(m_ghost->ghostRotation());
    rotationAnim->setEndValue(0.0);
    settleGroup->addAnimation(rotationAnim);

    connect(settleGroup, &QParallelAnimationGroup::finished, this, [this]() {
        m_settleAnim = nullptr;
        m_ghostOpacityAnim = nullptr;
        if (m_ghost) {
            m_ghost->setGhostOpacity(1.0); // ensure exact 1.0 before row appears
            m_ghost->setGhostRotation(0.0);
        }
        onSettleAnimFinished();
    });

    settleGroup->start(QAbstractAnimation::DeleteWhenStopped);
}

void ListDragDrop::animateGhostToSource()
{
    stopGhostFollow();

    if (!m_ghost || !m_viewport) {
        m_dragging = false;
        destroyGhost();
        emit dragCancelled();
        return;
    }

    QPoint sourceGlobal = m_viewport->mapToGlobal(m_sourcePos);
    QWidget* topLevel = m_viewport->window();
    QPoint sourceLocal = topLevel->mapFromGlobal(sourceGlobal);
    if (m_ghost) {
        sourceLocal -= m_ghost->contentTopLeft();
    }

    m_settleAnim = new QPropertyAnimation(m_ghost, "pos", this);
    m_settleAnim->setDuration(300);
    m_settleAnim->setEasingCurve(QEasingCurve::InOutCubic);
    m_settleAnim->setStartValue(m_ghost->pos());
    m_settleAnim->setEndValue(sourceLocal);

    // Also fade out
    m_ghostOpacityAnim = new QPropertyAnimation(m_ghost, "ghostOpacity", this);
    m_ghostOpacityAnim->setDuration(300);
    m_ghostOpacityAnim->setStartValue(m_ghost->ghostOpacity());
    m_ghostOpacityAnim->setEndValue(0.0);
    m_ghostOpacityAnim->setEasingCurve(QEasingCurve::InOutCubic);
    m_ghostOpacityAnim->start(QAbstractAnimation::DeleteWhenStopped);

    // Fade glass backdrop in sync with the ghost
    if (m_backdropAnim)
        m_backdropAnim->stop();
    m_backdropAnim = new QPropertyAnimation(m_ghost, "backdropOpacity", this);
    m_backdropAnim->setDuration(300);
    m_backdropAnim->setStartValue(m_ghost->backdropOpacity());
    m_backdropAnim->setEndValue(0.0);
    m_backdropAnim->setEasingCurve(QEasingCurve::InOutCubic);
    m_backdropAnim->start(QAbstractAnimation::DeleteWhenStopped);

    auto* rotationAnim = new QPropertyAnimation(m_ghost, "ghostRotation", this);
    rotationAnim->setDuration(200);
    rotationAnim->setStartValue(m_ghost->ghostRotation());
    rotationAnim->setEndValue(0.0);
    rotationAnim->setEasingCurve(QEasingCurve::OutCubic);
    rotationAnim->start(QAbstractAnimation::DeleteWhenStopped);

    connect(m_settleAnim, &QPropertyAnimation::finished, this, [this]() {
        m_dragging = false;
        destroyGhost();
        emit dragCancelled();
    });

    m_settleAnim->start(QAbstractAnimation::DeleteWhenStopped);
}

void ListDragDrop::onSettleAnimFinished()
{
    int insertIdx = m_dropInsertIndex;
    int targetDepth = m_dropTargetDepth;
    QUuid srcId = m_sourceId;

    m_dragging = false;
    m_copyMode = false;

    if (insertIdx >= 0) {
        emit ghostSettled(srcId, insertIdx, targetDepth);
    } else {
        destroyGhost();
        emit dragCancelled();
    }
}

// ============================================================================
// Multi-Drag Settle
// ============================================================================

void ListDragDrop::feedMultiDragSnapshots(
    const QList<QPixmap>& snapshots, const QList<int>& sourceYs)
{
    m_multiSourceYs = sourceYs;
    if (m_ghost) {
        // targetYs are not known yet — will be set in animateMultiGhostSettle
        m_ghost->setRowSnapshots(snapshots, {});
    }
}

bool ListDragDrop::animateMultiGhostSettle(
    int targetGapY, int rowHeight, int rowSpacing, bool layoutAlreadyApplied)
{
    stopGhostFollow();

    if (!m_ghost || !m_viewport) {
        onSettleAnimFinished();
        return true;
    }

    m_multiSettleRowHeight = rowHeight;
    m_multiSettleRowSpacing = rowSpacing;

    if (!layoutAlreadyApplied && m_layout && m_dropInsertIndex >= 0) {
        m_layout->applyDragEndState(allExcludeIds(), m_dropInsertIndex);
    }

    qreal gapY = m_layout ? m_layout->dragEndGapY() : static_cast<qreal>(targetGapY);
    if (gapY < 0)
        gapY = targetGapY;

    auto& tm = ThemeManager::instance();
    QWidget* topLevel = m_viewport->window();
    QPoint targetViewport(0, qRound(gapY));
    QPoint targetGlobal = m_viewport->mapToGlobal(targetViewport);
    QPoint targetLocal = topLevel->mapFromGlobal(targetGlobal);
    if (m_ghost) {
        targetLocal -= m_ghost->contentTopLeft();
    }

    // Build target Y list for each snapshot in topLevel coordinates, stacked from gapY
    int snapCount = m_ghost->rowSnapshots().size();
    QList<int> targetYsInParent;
    QList<bool> skipPositioning;
    const int posTolerance = 5; // pixels — same as single-layer depth-only check

    for (int i = 0; i < snapCount; ++i) {
        int yInViewport = qRound(gapY) + i * (rowHeight + rowSpacing);
        QPoint inGlobal = m_viewport->mapToGlobal(QPoint(0, yInViewport));
        targetYsInParent.append(topLevel->mapFromGlobal(inGlobal).y());

        // Row already in correct Y position? (depth-only change, like single-layer Path 2)
        int targetYInContainer = qRound(gapY) + i * (rowHeight + rowSpacing);
        bool alreadyInPos = (i < m_multiSourceYs.size()
            && qAbs(m_multiSourceYs[i] - targetYInContainer) <= posTolerance);
        skipPositioning.append(alreadyInPos);
    }
    // Update target Ys and skip flags in ghost (snapshots already stored from
    // feedMultiDragSnapshots)
    m_ghost->setRowSnapshots({}, targetYsInParent);
    m_ghost->setRowSkipPositioning(skipPositioning);

    // Start with compact pill size; width/height will animate to full during morph
    int rowWidth = m_viewport->width();
    int pillW = tm.scaled(160);
    int pillH = tm.scaled(32);
    int totalSpread = (snapCount > 0 && rowHeight > 0)
        ? rowHeight * snapCount + (snapCount > 1 ? rowSpacing * (snapCount - 1) : 0) + 10
        : pillH;
    m_ghost->setVisualContentSize(QSize(pillW, pillH));

    // All layers already in position? No morph — commit immediately, indent animation starts right
    // away
    bool allSkipPositioning = !skipPositioning.isEmpty();
    for (bool s : skipPositioning) {
        if (!s) {
            allSkipPositioning = false;
            break;
        }
    }
    if (allSkipPositioning) {
        destroyGhost();
        onSettleAnimFinished();
        return true; // caller must skip collapse
    }

    // Smooth 0.85→1.0 in first 80ms to avoid pop
    if (m_ghostOpacityAnim)
        m_ghostOpacityAnim->stop();
    m_ghostOpacityAnim = new QPropertyAnimation(m_ghost, "ghostOpacity", this);
    m_ghostOpacityAnim->setDuration(80);
    m_ghostOpacityAnim->setEasingCurve(QEasingCurve::OutCubic);
    m_ghostOpacityAnim->setStartValue(m_ghost->ghostOpacity());
    m_ghostOpacityAnim->setEndValue(1.0);
    m_ghostOpacityAnim->start(QAbstractAnimation::DeleteWhenStopped);

    auto* rotationAnim = new QPropertyAnimation(m_ghost, "ghostRotation", this);
    rotationAnim->setDuration(160);
    rotationAnim->setEasingCurve(QEasingCurve::OutCubic);
    rotationAnim->setStartValue(m_ghost->ghostRotation());
    rotationAnim->setEndValue(0.0);
    rotationAnim->start(QAbstractAnimation::DeleteWhenStopped);

    // Phase 1: fly ghost to gap (300 ms)
    m_settleAnim = new QPropertyAnimation(m_ghost, "pos", this);
    m_settleAnim->setDuration(300);
    m_settleAnim->setEasingCurve(QEasingCurve::InOutCubic);
    m_settleAnim->setStartValue(m_ghost->pos());
    m_settleAnim->setEndValue(targetLocal);
    connect(
        m_settleAnim, &QPropertyAnimation::finished, this, [this]() { m_settleAnim = nullptr; });
    m_settleAnim->start(QAbstractAnimation::DeleteWhenStopped);

    // Phase 2: morph pill → N row cards + size expansion — runs in parallel with fly (300ms)
    QParallelAnimationGroup* morphGroup = new QParallelAnimationGroup(this);

    m_morphAnim = new QPropertyAnimation(m_ghost, "morphProgress", morphGroup);
    m_morphAnim->setDuration(300);
    m_morphAnim->setEasingCurve(QEasingCurve::InOutCubic);
    m_morphAnim->setStartValue(0.0);
    m_morphAnim->setEndValue(1.0);
    morphGroup->addAnimation(m_morphAnim);

    // Fade the glass backdrop + border smoothly so they don't pop on destroy
    auto* multiBackdropAnim = new QPropertyAnimation(m_ghost, "backdropOpacity", morphGroup);
    multiBackdropAnim->setDuration(300);
    multiBackdropAnim->setEasingCurve(QEasingCurve::InOutCubic);
    multiBackdropAnim->setStartValue(m_ghost->backdropOpacity());
    multiBackdropAnim->setEndValue(0.0);
    morphGroup->addAnimation(multiBackdropAnim);

    auto* widthAnim = new QVariantAnimation(morphGroup);
    widthAnim->setDuration(300);
    widthAnim->setEasingCurve(QEasingCurve::InOutCubic);
    widthAnim->setStartValue(pillW);
    widthAnim->setEndValue(rowWidth);
    connect(widthAnim, &QVariantAnimation::valueChanged, this, [this, pillW](const QVariant& v) {
        if (!m_ghost)
            return;
        QSize size = m_ghost->visualContentSize();
        size.setWidth(qMax(pillW, v.toInt()));
        m_ghost->setVisualContentSize(size);
    });
    morphGroup->addAnimation(widthAnim);

    auto* heightAnim = new QVariantAnimation(morphGroup);
    heightAnim->setDuration(300);
    heightAnim->setEasingCurve(QEasingCurve::InOutCubic);
    heightAnim->setStartValue(pillH);
    heightAnim->setEndValue(totalSpread);
    connect(heightAnim, &QVariantAnimation::valueChanged, this, [this, pillH](const QVariant& v) {
        if (!m_ghost)
            return;
        QSize size = m_ghost->visualContentSize();
        size.setHeight(qMax(pillH, v.toInt()));
        m_ghost->setVisualContentSize(size);
    });
    morphGroup->addAnimation(heightAnim);

    connect(morphGroup, &QParallelAnimationGroup::finished, this, [this]() {
        m_morphAnim = nullptr;
        onSettleAnimFinished();
    });
    morphGroup->start(QAbstractAnimation::DeleteWhenStopped);
    return false;
}

void ListDragDrop::destroyGhost()
{
    stopGhostFollow();
    m_dropTargetDepth = 0;
    m_copyMode = false;
    if (m_ghost) {
        m_ghost->deleteLater();
        m_ghost = nullptr;
    }
    if (m_indicator) {
        m_indicator->deleteLater();
        m_indicator = nullptr;
    }
    if (m_settleAnim) {
        m_settleAnim->stop();
        m_settleAnim = nullptr;
    }
    if (m_ghostOpacityAnim) {
        m_ghostOpacityAnim->stop();
        m_ghostOpacityAnim = nullptr;
    }
    if (m_morphAnim) {
        m_morphAnim->stop();
        m_morphAnim = nullptr;
    }
}

} // namespace ruwa::ui::widgets
