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
}

bool OverviewCache::configure(const QRect& worldFrame, const QSize& overviewSize)
{
    const QRect normalizedFrame = worldFrame.normalized();
    if (m_worldFrame == normalizedFrame && m_overviewSize == overviewSize) {
        return false;
    }

    clear();
    m_worldFrame = normalizedFrame;
    m_overviewSize = overviewSize;
    return true;
}

bool OverviewCache::isValid() const
{
    return m_worldFrame.isValid() && !m_worldFrame.isEmpty() && m_overviewSize.isValid()
        && !m_overviewSize.isEmpty();
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
    return m_dirtyTiles.keys();
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

    m_tiles.insert(tileCoord, image);
    m_dirtyTiles.remove(tileCoord);
}

void OverviewCache::draw(QPainter& painter, const QRectF& displayRect) const
{
    if (!isValid() || displayRect.isEmpty()) {
        return;
    }

    for (auto it = m_tiles.constBegin(); it != m_tiles.constEnd(); ++it) {
        const QRect pixelRect = overviewTilePixelRect(it.key());
        if (!pixelRect.isValid() || pixelRect.isEmpty()) {
            continue;
        }
        painter.drawImage(mapOverviewPixelRectToDisplayRect(pixelRect, displayRect), it.value());
    }
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

} // namespace ruwa::ui::workspace
