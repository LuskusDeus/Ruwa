// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   O V E R V I E W   C A C H E
// ==========================================================================

#include "OverviewCache.h"

#include "shared/tiles/TileTypes.h"

#include <algorithm>
#include <cmath>

namespace ruwa::ui::workspace {

void OverviewCache::clear()
{
    m_worldFrame = {};
    m_overviewSize = {};
    m_tiles.clear();
    m_dirtyTiles.clear();
    m_transitionTiles.clear();
    m_transitionClock.invalidate();
    m_previousOverview = {};
    m_previousWorldFrame = {};
    m_frameTransitionStart = {};
    m_frameTransitionActive = false;
}

bool OverviewCache::configure(const QRect& worldFrame, const QSize& overviewSize)
{
    const QRect normalizedFrame = worldFrame.normalized();
    if (m_worldFrame == normalizedFrame && m_overviewSize == overviewSize) {
        return false;
    }

    // Flatten the currently presented generations before changing coordinates.
    // Keeping this image in world space makes an interrupted resize continue from
    // its current visual state instead of flashing or restarting from an old frame.
    const QImage previousOverview = renderCurrentComposition();
    const QRect previousWorldFrame = m_worldFrame;
    const QRectF transitionStart = presentedWorldFrame();

    m_tiles.clear();
    m_dirtyTiles.clear();
    m_transitionTiles.clear();
    m_worldFrame = normalizedFrame;
    m_overviewSize = overviewSize;

    m_previousOverview = previousOverview;
    m_previousWorldFrame = previousWorldFrame;
    m_frameTransitionStart = transitionStart;
    m_frameTransitionActive = !m_previousOverview.isNull() && transitionStart.isValid()
        && !transitionStart.isEmpty() && transitionStart != QRectF(m_worldFrame);

    m_transitionClock.invalidate();
    if (!m_previousOverview.isNull()) {
        m_transitionClock.start();
    }
    return true;
}

bool OverviewCache::isValid() const
{
    return m_worldFrame.isValid() && !m_worldFrame.isEmpty() && m_overviewSize.isValid()
        && !m_overviewSize.isEmpty();
}

QRectF OverviewCache::presentedWorldFrame() const
{
    if (!m_frameTransitionActive || !m_transitionClock.isValid()
        || !m_frameTransitionStart.isValid() || m_frameTransitionStart.isEmpty()) {
        return QRectF(m_worldFrame);
    }

    const qreal progress = frameTransitionProgress();
    const auto interpolate = [progress](qreal from, qreal to) {
        return from + (to - from) * progress;
    };
    const QRectF target(m_worldFrame);
    return QRectF(interpolate(m_frameTransitionStart.x(), target.x()),
        interpolate(m_frameTransitionStart.y(), target.y()),
        interpolate(m_frameTransitionStart.width(), target.width()),
        interpolate(m_frameTransitionStart.height(), target.height()));
}

void OverviewCache::invalidateAll()
{
    if (!isValid()) {
        return;
    }

    const int tilesX = (m_overviewSize.width() + TileSize - 1) / TileSize;
    const int tilesY = (m_overviewSize.height() + TileSize - 1) / TileSize;
    for (int ty = 0; ty < tilesY; ++ty) {
        for (int tx = 0; tx < tilesX; ++tx) {
            markTileDirty(QPoint(tx, ty));
        }
    }
}

void OverviewCache::invalidateWorldRect(const QRect& worldRect)
{
    const QRect pixelRect = overviewPixelRectForWorldRect(worldRect);
    if (!pixelRect.isValid() || pixelRect.isEmpty()) {
        return;
    }

    const int minTileX = pixelRect.left() / TileSize;
    const int minTileY = pixelRect.top() / TileSize;
    const int maxTileX = pixelRect.right() / TileSize;
    const int maxTileY = pixelRect.bottom() / TileSize;
    for (int ty = minTileY; ty <= maxTileY; ++ty) {
        for (int tx = minTileX; tx <= maxTileX; ++tx) {
            markTileDirty(QPoint(tx, ty));
        }
    }
}

void OverviewCache::invalidateCompositionTiles(const QList<QPoint>& tilePositions)
{
    const int tileSize = static_cast<int>(aether::TILE_SIZE);
    for (const QPoint& tilePos : tilePositions) {
        invalidateWorldRect(
            QRect(tilePos.x() * tileSize, tilePos.y() * tileSize, tileSize, tileSize));
    }
}

QList<QPoint> OverviewCache::dirtyTiles() const
{
    QList<QPoint> result;
    result.reserve(m_dirtyTiles.size());
    for (auto it = m_dirtyTiles.constBegin(); it != m_dirtyTiles.constEnd(); ++it) {
        // If the tile changed again during its fade, finish the current transition
        // first. This avoids snapping its opacity back to zero mid-animation.
        if (!m_transitionTiles.contains(it.key())) {
            result.append(it.key());
        }
    }
    return result;
}

QRect OverviewCache::overviewTilePixelRect(const QPoint& tileCoord) const
{
    if (!isValid()) {
        return {};
    }

    const int x = tileCoord.x() * TileSize;
    const int y = tileCoord.y() * TileSize;
    if (x >= m_overviewSize.width() || y >= m_overviewSize.height()) {
        return {};
    }

    return QRect(x, y, std::min(TileSize, m_overviewSize.width() - x),
        std::min(TileSize, m_overviewSize.height() - y));
}

QRect OverviewCache::worldRectForOverviewPixelRect(const QRect& pixelRect) const
{
    if (!isValid()) {
        return {};
    }

    const QRect clipped = pixelRect.intersected(QRect(QPoint(0, 0), m_overviewSize));
    if (!clipped.isValid() || clipped.isEmpty()) {
        return {};
    }

    const qreal scaleX
        = static_cast<qreal>(m_worldFrame.width()) / static_cast<qreal>(m_overviewSize.width());
    const qreal scaleY
        = static_cast<qreal>(m_worldFrame.height()) / static_cast<qreal>(m_overviewSize.height());
    const int left = m_worldFrame.left() + static_cast<int>(std::floor(clipped.left() * scaleX));
    const int top = m_worldFrame.top() + static_cast<int>(std::floor(clipped.top() * scaleY));
    const int right
        = m_worldFrame.left() + static_cast<int>(std::ceil((clipped.right() + 1) * scaleX)) - 1;
    const int bottom
        = m_worldFrame.top() + static_cast<int>(std::ceil((clipped.bottom() + 1) * scaleY)) - 1;
    return QRect(left, top, right - left + 1, bottom - top + 1);
}

void OverviewCache::storeTile(const QPoint& tileCoord, const QImage& image)
{
    if (image.isNull()) {
        return;
    }

    m_dirtyTiles.remove(tileCoord);
    if (!m_tiles.contains(tileCoord)) {
        if (m_previousOverview.isNull()) {
            // There is no previous presentation to preserve during initial population.
            m_tiles.insert(tileCoord, image);
            return;
        }
    } else if (m_transitionTiles.contains(tileCoord)) {
        m_transitionTiles.remove(tileCoord);
    }

    if (!m_transitionClock.isValid()) {
        m_transitionClock.start();
    }
    m_transitionTiles.insert(
        tileCoord, TransitionTile { image, m_transitionClock.elapsed() });
}

bool OverviewCache::hasActiveTransitions() const
{
    return m_frameTransitionActive || !m_transitionTiles.isEmpty();
}

void OverviewCache::advanceTransitions()
{
    if (!m_transitionClock.isValid()) {
        return;
    }

    const qint64 now = m_transitionClock.elapsed();
    for (auto it = m_transitionTiles.begin(); it != m_transitionTiles.end();) {
        if (now - it->startedAtMs < TileFadeDurationMs) {
            ++it;
            continue;
        }

        m_tiles.insert(it.key(), it->image);
        it = m_transitionTiles.erase(it);
    }

    if (m_frameTransitionActive && now >= FrameTransitionDurationMs) {
        m_frameTransitionActive = false;
        m_frameTransitionStart = QRectF(m_worldFrame);
    }

    if (!m_frameTransitionActive && m_transitionTiles.isEmpty()) {
        m_transitionClock.invalidate();
        if (m_dirtyTiles.isEmpty()) {
            m_previousOverview = {};
            m_previousWorldFrame = {};
        }
    }
}

void OverviewCache::draw(QPainter& painter, const QRectF& displayRect) const
{
    if (!isValid() || displayRect.isEmpty()) {
        return;
    }

    drawComposition(painter, displayRect, presentedWorldFrame());
}

QImage OverviewCache::renderCurrentComposition() const
{
    if (!isValid()) {
        return {};
    }

    QImage image(m_overviewSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);

    QPainter painter(&image);
    painter.setRenderHint(QPainter::SmoothPixmapTransform);
    drawComposition(
        painter, QRectF(QPointF(0.0, 0.0), QSizeF(m_overviewSize)), QRectF(m_worldFrame));
    return image;
}

void OverviewCache::drawComposition(
    QPainter& painter, const QRectF& displayRect, const QRectF& presentedFrame) const
{
    if (!isValid() || displayRect.isEmpty() || !presentedFrame.isValid()
        || presentedFrame.isEmpty()) {
        return;
    }

    painter.save();
    painter.setClipRect(displayRect);

    if (!m_previousOverview.isNull() && m_previousWorldFrame.isValid()
        && !m_previousWorldFrame.isEmpty()) {
        // The retained generation follows the same interpolated world transform as
        // the new tiles, so the old content resizes continuously until replacement
        // tiles have faded in.
        const QRectF previousDisplayRect = mapWorldFrameToDisplayRect(
            QRectF(m_previousWorldFrame), presentedFrame, displayRect);
        painter.drawImage(previousDisplayRect, m_previousOverview);
    }

    const QRectF currentDisplayRect
        = mapWorldFrameToDisplayRect(QRectF(m_worldFrame), presentedFrame, displayRect);
    for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it) {
        const QRect pixelRect = overviewTilePixelRect(it.key());
        if (!pixelRect.isValid() || pixelRect.isEmpty()) {
            continue;
        }
        painter.drawImage(
            mapOverviewPixelRectToDisplayRect(pixelRect, currentDisplayRect), it.value());
    }

    if (m_transitionTiles.isEmpty() || !m_transitionClock.isValid()) {
        painter.restore();
        return;
    }

    const qreal originalOpacity = painter.opacity();
    const qint64 now = m_transitionClock.elapsed();
    for (auto it = m_transitionTiles.constBegin(); it != m_transitionTiles.constEnd(); ++it) {
        const QRect pixelRect = overviewTilePixelRect(it.key());
        if (!pixelRect.isValid() || pixelRect.isEmpty()) {
            continue;
        }

        const qreal fadeProgress = qBound<qreal>(0.0,
            static_cast<qreal>(now - it->startedAtMs)
                / static_cast<qreal>(TileFadeDurationMs),
            1.0);
        if (fadeProgress <= 0.0) {
            continue;
        }

        painter.setOpacity(originalOpacity * fadeProgress);
        painter.drawImage(mapOverviewPixelRectToDisplayRect(pixelRect, currentDisplayRect),
            it->image);
    }
    painter.setOpacity(originalOpacity);
    painter.restore();
}

QRectF OverviewCache::mapWorldFrameToDisplayRect(const QRectF& worldFrame,
    const QRectF& presentedFrame, const QRectF& displayRect) const
{
    if (!worldFrame.isValid() || worldFrame.isEmpty() || !presentedFrame.isValid()
        || presentedFrame.isEmpty() || displayRect.isEmpty()) {
        return {};
    }

    const qreal scaleX = displayRect.width() / presentedFrame.width();
    const qreal scaleY = displayRect.height() / presentedFrame.height();
    return QRectF(displayRect.left() + (worldFrame.left() - presentedFrame.left()) * scaleX,
        displayRect.top() + (worldFrame.top() - presentedFrame.top()) * scaleY,
        worldFrame.width() * scaleX, worldFrame.height() * scaleY);
}

void OverviewCache::markTileDirty(const QPoint& tileCoord)
{
    if (tileCoord.x() < 0 || tileCoord.y() < 0) {
        return;
    }
    if (!overviewTilePixelRect(tileCoord).isValid()) {
        return;
    }
    m_dirtyTiles.insert(tileCoord, true);
}

QRect OverviewCache::overviewPixelRectForWorldRect(const QRect& worldRect) const
{
    if (!isValid()) {
        return {};
    }

    const QRect clipped = worldRect.normalized().intersected(m_worldFrame);
    if (!clipped.isValid() || clipped.isEmpty()) {
        return {};
    }

    const qreal scaleX
        = static_cast<qreal>(m_overviewSize.width()) / static_cast<qreal>(m_worldFrame.width());
    const qreal scaleY
        = static_cast<qreal>(m_overviewSize.height()) / static_cast<qreal>(m_worldFrame.height());
    const int left
        = qBound(0, static_cast<int>(std::floor((clipped.left() - m_worldFrame.left()) * scaleX)),
            m_overviewSize.width() - 1);
    const int top
        = qBound(0, static_cast<int>(std::floor((clipped.top() - m_worldFrame.top()) * scaleY)),
            m_overviewSize.height() - 1);
    const int right = qBound(left + 1,
        static_cast<int>(std::ceil((clipped.right() - m_worldFrame.left() + 1) * scaleX)),
        m_overviewSize.width());
    const int bottom = qBound(top + 1,
        static_cast<int>(std::ceil((clipped.bottom() - m_worldFrame.top() + 1) * scaleY)),
        m_overviewSize.height());
    return QRect(left, top, right - left, bottom - top);
}

QRectF OverviewCache::mapOverviewPixelRectToDisplayRect(
    const QRect& pixelRect, const QRectF& displayRect) const
{
    const qreal scaleX = displayRect.width() / static_cast<qreal>(m_overviewSize.width());
    const qreal scaleY = displayRect.height() / static_cast<qreal>(m_overviewSize.height());
    return QRectF(displayRect.left() + pixelRect.left() * scaleX,
        displayRect.top() + pixelRect.top() * scaleY, pixelRect.width() * scaleX,
        pixelRect.height() * scaleY);
}

qreal OverviewCache::frameTransitionProgress() const
{
    if (!m_frameTransitionActive || !m_transitionClock.isValid()) {
        return 1.0;
    }

    const qreal linearProgress = qBound<qreal>(0.0,
        static_cast<qreal>(m_transitionClock.elapsed())
            / static_cast<qreal>(FrameTransitionDurationMs),
        1.0);
    static const QEasingCurve easing(QEasingCurve::InOutCubic);
    return easing.valueForProgress(linearProgress);
}

} // namespace ruwa::ui::workspace
