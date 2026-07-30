// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   O V E R V I E W   C A C H E
// ==========================================================================

#ifndef RUWA_UI_WORKSPACE_OVERVIEWCACHE_H
#define RUWA_UI_WORKSPACE_OVERVIEWCACHE_H

#include <QElapsedTimer>
#include <QEasingCurve>
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
    static constexpr int TileFadeDurationMs = 200;
    static constexpr int FrameTransitionDurationMs = 250;

    void clear();
    bool configure(const QRect& worldFrame, const QSize& overviewSize);

    bool isValid() const;
    QRect worldFrame() const { return m_worldFrame; }
    QRectF presentedWorldFrame() const;
    QSize overviewSize() const { return m_overviewSize; }

    void invalidateAll();
    void invalidateWorldRect(const QRect& worldRect);
    void invalidateCompositionTiles(const QList<QPoint>& tilePositions);

    QList<QPoint> dirtyTiles() const;
    bool hasDirtyTiles() const { return !m_dirtyTiles.isEmpty(); }
    bool hasActiveTransitions() const;
    void advanceTransitions();

    QRect overviewTilePixelRect(const QPoint& tileCoord) const;
    QRect worldRectForOverviewPixelRect(const QRect& pixelRect) const;

    void storeTile(const QPoint& tileCoord, const QImage& image);
    void draw(QPainter& painter, const QRectF& displayRect) const;

private:
    struct TransitionTile {
        QImage image;
        qint64 startedAtMs = 0;
    };

    void markTileDirty(const QPoint& tileCoord);
    QImage renderCurrentComposition() const;
    void drawComposition(
        QPainter& painter, const QRectF& displayRect, const QRectF& presentedFrame) const;
    QRectF mapWorldFrameToDisplayRect(const QRectF& worldFrame,
        const QRectF& presentedFrame, const QRectF& displayRect) const;
    QRect overviewPixelRectForWorldRect(const QRect& worldRect) const;
    QRectF mapOverviewPixelRectToDisplayRect(
        const QRect& pixelRect, const QRectF& displayRect) const;
    qreal frameTransitionProgress() const;

private:
    QRect m_worldFrame;
    QSize m_overviewSize;
    QHash<QPoint, QImage> m_tiles;
    QHash<QPoint, bool> m_dirtyTiles;
    QHash<QPoint, TransitionTile> m_transitionTiles;
    QElapsedTimer m_transitionClock;
    QImage m_previousOverview;
    QRect m_previousWorldFrame;
    QRectF m_frameTransitionStart;
    bool m_frameTransitionActive = false;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_UI_WORKSPACE_OVERVIEWCACHE_H
