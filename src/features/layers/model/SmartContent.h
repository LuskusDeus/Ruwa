// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   S M A R T   C O N T E N T
// ============================================================================

#ifndef RUWA_CORE_LAYERS_SMARTCONTENT_H
#define RUWA_CORE_LAYERS_SMARTCONTENT_H

#include <QByteArray>
#include <QString>
#include <QUuid>
#include <QtGlobal>
#include <cstring>
#include <memory>

#include "shared/tiles/TileGrid.h"
#include "features/transform/TransformState.h"

namespace ruwa::core::layers {

/**
 * @brief Where a smart layer's content came from.
 *
 * Embedded content lives entirely inside the document. LinkedFile content is
 * mirrored from a file on disk (sourcePath / sourceHash describe it) and can be
 * relinked or refreshed. Only Embedded is produced today; the linked path lands
 * with "Replace Contents" / "Relink to File".
 */
enum class SmartSourceKind : quint8 { Embedded = 0, LinkedFile = 1 };

/**
 * @brief The payload of a smart object: its pixels plus the identity of those
 *        pixels, independent of any layer that shows them.
 *
 * A smart layer owns a `shared_ptr<SmartContent>` rather than a grid, so several
 * layers can later become *instances* of one content while each keeps its own
 * `LayerData::smartTransform`. Everything that identifies the content (its id,
 * its source, its revision) belongs here; everything that describes how one
 * layer *places* it stays on the layer.
 *
 * The content grid is in CONTENT space (the space the pixels were authored in),
 * not document space. Projection into document space is the compositor's job
 * (`OpenGLCanvasWidget::buildSmartProjectedGrid`).
 *
 * Not copyable: content is shared by pointer. Use cloneDetached() when a real
 * duplicate (a new contentId, a private grid) is wanted.
 */
struct SmartContent {
    /// Identity of the pixels. Two layers with the same contentId show the same
    /// content and must stay in sync; the file format writes such content once.
    QUuid contentId = QUuid::createUuid();

    /// Premultiplied pixels in content space. Never null for a live content.
    std::unique_ptr<aether::TileGrid> grid;

    /// Bumped whenever the content is REPLACED wholesale (import, relink,
    /// rasterize-and-restore). Pixel-level edits are tracked by the grid's own
    /// contentVersion(); contentStamp() folds both into one invalidation key.
    quint64 contentRevision = 0;

    // ---- Source (linked content; unused while sourceKind == Embedded) ----
    QString sourcePath;
    SmartSourceKind sourceKind = SmartSourceKind::Embedded;
    QByteArray sourceHash;

    SmartContent()
        : grid(std::make_unique<aether::TileGrid>())
    {
    }

    explicit SmartContent(std::unique_ptr<aether::TileGrid> contentGrid)
        : grid(contentGrid ? std::move(contentGrid) : std::make_unique<aether::TileGrid>())
    {
    }

    SmartContent(const SmartContent&) = delete;
    SmartContent& operator=(const SmartContent&) = delete;

    /**
     * @brief Replace the pixels wholesale and invalidate every derived cache.
     *
     * Passing null installs an empty grid rather than leaving the content
     * grid-less, so `grid` is always dereferenceable.
     */
    void setGrid(std::unique_ptr<aether::TileGrid> newGrid)
    {
        grid = newGrid ? std::move(newGrid) : std::make_unique<aether::TileGrid>();
        markContentChanged();
    }

    /**
     * @brief Signal that the pixels changed outside the grid's own mutation
     *        entry points, or that the whole grid was swapped.
     *
     * Bumps contentRevision, which every consumer keyed on contentStamp() sees.
     */
    void markContentChanged()
    {
        ++contentRevision;
        m_boundsValid = false;
    }

    /**
     * @brief The invalidation key for anything derived from this content.
     *
     * Folds the wholesale-replacement counter with the grid's own per-mutation
     * contentVersion() and tile count, so a cache that stores this value can
     * never be revalidated against content that has since changed — including
     * across a grid swap, where contentVersion() would otherwise restart low.
     */
    quint64 contentStamp() const
    {
        constexpr quint64 kMix = 0x9E3779B97F4A7C15ull;
        quint64 stamp = contentRevision * kMix;
        stamp = mixIn(stamp, grid ? static_cast<quint64>(grid->contentVersion()) : 0ull);
        stamp = mixIn(stamp, grid ? static_cast<quint64>(grid->tileCount()) : 0ull);
        return stamp;
    }

    /**
     * @brief Pixel-tight bounds of the content in content space.
     *
     * `TransformState::computeContentBounds` scans every pixel of every tile, and
     * this is asked for on hit-tests and on every projection rebuild, so the
     * result is cached against contentStamp(). A mutation that bypasses both the
     * grid's mutation entry points and markContentChanged() would keep a stale
     * value — that is the same contract TileGrid::contentVersion() already
     * documents for its own consumers.
     *
     * The cache is not synchronized: like the rest of LayerData, smart content
     * is touched from the GUI thread only.
     */
    aether::Rect nativeBounds() const
    {
        const quint64 stamp = contentStamp();
        if (!m_boundsValid || m_boundsStamp != stamp) {
            m_nativeBounds
                = grid ? aether::TransformState::computeContentBounds(*grid) : aether::Rect {};
            m_boundsStamp = stamp;
            m_boundsValid = true;
        }
        return m_nativeBounds;
    }

    bool isEmpty() const { return !grid || grid->empty(); }

    /**
     * @brief A private duplicate: same pixels and source description, new identity.
     *
     * Used when a smart layer is duplicated as an independent copy rather than as
     * an instance ("New Smart Object via Copy").
     */
    std::shared_ptr<SmartContent> cloneDetached() const
    {
        auto copy = std::make_shared<SmartContent>();
        copy->sourcePath = sourcePath;
        copy->sourceKind = sourceKind;
        copy->sourceHash = sourceHash;
        if (grid) {
            auto* dst = copy->grid.get();
            dst->setFormat(grid->format());
            dst->setDefaultFillPacked(grid->defaultFillPacked());
            const size_t copyBytes = aether::tileByteSize(grid->format());
            for (const auto& [key, tile] : grid->tiles()) {
                auto& dstTile = dst->getOrCreateTile(key);
                if (tile.isSolid()) {
                    dstTile.setSolidPacked(tile.solidColorPacked());
                } else {
                    std::memcpy(dstTile.pixels(), tile.pixels(), copyBytes);
                }
                dstTile.markDirty();
            }
        }
        copy->markContentChanged();
        return copy;
    }

private:
    static quint64 mixIn(quint64 seed, quint64 value)
    {
        return seed ^ (value + 0x9E3779B97F4A7C15ull + (seed << 6) + (seed >> 2));
    }

    mutable aether::Rect m_nativeBounds {};
    mutable quint64 m_boundsStamp = 0;
    mutable bool m_boundsValid = false;
};

} // namespace ruwa::core::layers

#endif // RUWA_CORE_LAYERS_SMARTCONTENT_H
