// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   O V E R V I E W   C A C H E
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_OVERVIEWCACHE_H
#define RUWA_UI_WORKSPACE_OVERVIEWCACHE_H

#include <QHash>
#include <QImage>
#include <QList>
#include <QPainter>
#include <QPoint>
#include <QRect>
#include <QRectF>
#include <QSize>

namespace ruwa::ui::workspace {

class OverviewCache {
public:
    static constexpr int TileSize = 128;

    void clear();
    bool configure(const QRect& worldFrame, const QSize& overviewSize);

    bool isValid() const;
    QRect worldFrame() const { return m_worldFrame; }
    QSize overviewSize() const { return m_overviewSize; }

    void invalidateAll();
    void invalidateWorldRect(const QRect& worldRect);
    void invalidateCompositionTiles(const QList<QPoint>& tilePositions);

    QList<QPoint> dirtyTiles() const;
    bool hasDirtyTiles() const { return !m_dirtyTiles.isEmpty(); }

    QRect overviewTilePixelRect(const QPoint& tileCoord) const;
    QRect worldRectForOverviewPixelRect(const QRect& pixelRect) const;

    void storeTile(const QPoint& tileCoord, const QImage& image);
    void draw(QPainter& painter, const QRectF& displayRect) const;

private:
    void markTileDirty(const QPoint& tileCoord);
    QRect overviewPixelRectForWorldRect(const QRect& worldRect) const;
    QRectF mapOverviewPixelRectToDisplayRect(
        const QRect& pixelRect, const QRectF& displayRect) const;

private:
    QRect m_worldFrame;
    QSize m_overviewSize;
    QHash<QPoint, QImage> m_tiles;
    QHash<QPoint, bool> m_dirtyTiles;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_OVERVIEWCACHE_H
