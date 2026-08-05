// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   S M A R T   D O C U M E N T
// ============================================================================

#ifndef RUWA_CORE_LAYERS_SMARTDOCUMENT_H
#define RUWA_CORE_LAYERS_SMARTDOCUMENT_H

#include <QList>
#include <QSize>
#include <QString>
#include <QtGlobal>
#include <memory>

#include "features/layers/model/LayerData.h"
#include "shared/tiles/TileFormat.h"

namespace aether {
class TileGrid;
}

namespace ruwa::core::layers {

/**
 * @brief The layer stack living INSIDE a smart object — a whole document in
 *        content space.
 *
 * A smart content used to be a single grid of pixels. From stage 2 on it can
 * instead be a document: its own layers, its own size, its own storage format.
 * The content's `grid` then stops being the truth and becomes a CACHE of this
 * document composited down (see `SmartContent::compositeRevision`); everything
 * downstream — projection, effects, masks, thumbnails — keeps reading that grid
 * and neither knows nor cares where it came from.
 *
 * `revision` is what tells a cache it is stale. Every edit to `roots` (or to the
 * pixels inside them) must go through `markChanged()`, exactly the way pixel
 * edits go through `TileGrid::contentVersion()`.
 *
 * The document is owned by the SmartContent, so all INSTANCES of that content
 * share it: editing the contents of one instance changes them all, which is the
 * whole point of an instance.
 *
 * @note `roots` follows `LayerModel::rootLayers()` order — index 0 is the
 *       TOPMOST layer, and a compositor walks it backwards. Keeping the same
 *       convention is what lets a nested document be loaded into a real
 *       `LayerModel` (the contents tab) and handed back without reordering.
 *
 * @note Content space is not canvas space: a canvas resize must NEVER walk into
 *       a nested document (see `SmartContentSwapCommand`'s note on
 *       `remapForCanvasResize`).
 */
struct SmartDocument {
    /// The nested layer stack, top-first (see class note).
    QList<std::shared_ptr<LayerData>> roots;

    /// Size of the nested canvas, in content-space pixels. The origin is always
    /// (0,0) — the projection into the parent document depends on that, so a
    /// content document is never re-centred on its pixels.
    QSize size;

    /// Storage format of the nested document. Independent of the parent's: the
    /// pixels keep the precision they were authored in across a round trip.
    aether::TilePixelFormat format = aether::TilePixelFormat::RGBA8;

    /// Bumped on every structural or pixel change; the invalidation key for the
    /// composited cache and for anything folded into `SmartContent::contentStamp()`.
    ///
    /// Starts at 1, never 0: `SmartContent::compositeRevision` starts at 0 and
    /// means "these pixels are not a composite of anything", so a freshly
    /// installed document must not accidentally compare equal to it and be
    /// mistaken for already-composited.
    quint64 revision = 1;

    bool isEmpty() const { return roots.isEmpty(); }

    /// Signal that something inside the document changed. Cheap, and the only
    /// correct way to invalidate the composite.
    void markChanged() { ++revision; }

    /**
     * @brief A deep copy of the whole nested stack.
     *
     * Layer ids are PRESERVED: they only have to be unique inside one document,
     * and keeping them means undo snapshots and open contents tabs still address
     * the same layers after a copy.
     *
     * `revision` is copied too, so a `SmartContent` that clones both the document
     * and its composited grid keeps that cache valid instead of forcing a
     * needless recomposite.
     */
    std::shared_ptr<SmartDocument> clone() const;

    /**
     * @brief Deep-copy a nested layer stack for a DIFFERENT owner of it — the
     *        contents tab that edits a document, and the document a contents tab
     *        commits back.
     *
     * Like `clone()`, but nested smart layers get their own content instead of
     * sharing it: `LayerModel::cloneLayerTree` shares `smartContent` on purpose
     * (a duplicate is an instance), and across an ownership boundary that would
     * mean editing a smart object INSIDE a contents tab reached the parent
     * document before anything was committed. Exactly one new content per
     * distinct source content, so instances inside the stack stay instances.
     *
     * Layer ids are preserved (same reasoning as `clone()`); content ids are not
     * — a copy that nobody has committed yet is a different object, and reusing
     * the id would make the two collide in the contents-session registry.
     *
     * Only the first level is detached: a content's own nested document is
     * cloned by `SmartContent::cloneDetached()` and still shares deeper contents.
     * That is safe because reaching a deeper level to EDIT it means opening its
     * contents tab, which detaches again at that level.
     */
    static QList<std::shared_ptr<LayerData>> cloneRootsForSeparateOwner(
        const QList<std::shared_ptr<LayerData>>& roots);

    /// Rough footprint of the nested pixels, for undo-memory accounting.
    qint64 approximateMemoryBytes() const;

    /**
     * @brief Wrap a flat content grid into a one-layer document.
     *
     * The migration path for every smart object authored before stage 2 (and for
     * imported images, which are flat by nature): the pixels become a single
     * raster layer, so the contents tab has something real to edit.
     *
     * The grid is COPIED (solids preserved) — the document owns its pixels, and
     * the caller's grid usually goes on living as the composited cache.
     */
    static std::shared_ptr<SmartDocument> fromGrid(const aether::TileGrid& grid,
        const QSize& documentSize, const QString& layerName = QStringLiteral("Contents"));
};

} // namespace ruwa::core::layers

#endif // RUWA_CORE_LAYERS_SMARTDOCUMENT_H
