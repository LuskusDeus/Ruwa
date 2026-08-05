// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   S M A R T   D O C U M E N T
// ============================================================================

#include "features/layers/smart/SmartDocument.h"

#include "features/layers/model/LayerModel.h"
#include "shared/tiles/TileGrid.h"
#include "shared/tiles/TileGridClone.h"

#include <QHash>

namespace ruwa::core::layers {
namespace {

qint64 layerMemoryBytes(const LayerData* layer)
{
    if (!layer) {
        return 0;
    }

    qint64 bytes = 0;
    if (const auto* grid = layer->pixelGrid()) {
        bytes += static_cast<qint64>(grid->tiles().size())
            * static_cast<qint64>(aether::tileByteSize(grid->format()));
    }
    if (const auto* mask = layer->maskTileGrid()) {
        bytes += static_cast<qint64>(mask->tiles().size())
            * static_cast<qint64>(aether::TILE_BYTE_SIZE);
    }
    for (const auto& child : layer->children) {
        bytes += layerMemoryBytes(child.get());
    }
    return bytes;
}

/**
 * Give every smart layer in a freshly copied stack its own content.
 *
 * One new content per distinct SOURCE content, so two instances inside the stack
 * stay instances of each other — a per-layer detach would quietly turn them into
 * unrelated objects.
 */
void detachNestedContents(LayerData* layer, QHash<QUuid, std::shared_ptr<SmartContent>>& copies)
{
    if (!layer) {
        return;
    }
    if (layer->smartContent) {
        const QUuid sourceId = layer->smartContent->contentId;
        auto it = copies.find(sourceId);
        if (it == copies.end()) {
            it = copies.insert(sourceId, layer->smartContent->cloneDetached());
        }
        layer->smartContent = it.value();
    }
    for (const auto& child : layer->children) {
        detachNestedContents(child.get(), copies);
    }
}

} // namespace

QList<std::shared_ptr<LayerData>> SmartDocument::cloneRootsForSeparateOwner(
    const QList<std::shared_ptr<LayerData>>& roots)
{
    QList<std::shared_ptr<LayerData>> copies;
    copies.reserve(roots.size());

    QHash<QUuid, std::shared_ptr<SmartContent>> contentCopies;
    for (const auto& root : roots) {
        auto layerClone = LayerModel::cloneLayerTree(root.get(), /*preserveIds=*/true);
        if (!layerClone) {
            continue;
        }
        // cloneLayerTree SHARES smart content by design (that is what makes a
        // duplicate an instance). Here the copy belongs to somebody else, so the
        // sharing has to be cut — otherwise editing a nested object in a contents
        // tab would reach the document that tab has not committed to yet.
        detachNestedContents(layerClone.get(), contentCopies);
        copies.append(std::move(layerClone));
    }
    return copies;
}

std::shared_ptr<SmartDocument> SmartDocument::clone() const
{
    auto copy = std::make_shared<SmartDocument>();
    copy->size = size;
    copy->format = format;
    copy->revision = revision;
    copy->roots.reserve(roots.size());
    for (const auto& root : roots) {
        if (auto layerClone = LayerModel::cloneLayerTree(root.get(), /*preserveIds=*/true)) {
            copy->roots.append(std::move(layerClone));
        }
    }
    return copy;
}

qint64 SmartDocument::approximateMemoryBytes() const
{
    // Deliberately does NOT descend into a nested document of its own (a smart
    // object inside a smart object): those pixels are accounted for by their own
    // content, and an unguarded walk would loop on a cyclic reference — the cycle
    // guard belongs with smart-in-smart itself.
    qint64 bytes = 0;
    for (const auto& root : roots) {
        bytes += layerMemoryBytes(root.get());
    }
    return bytes;
}

std::shared_ptr<SmartDocument> SmartDocument::fromGrid(
    const aether::TileGrid& grid, const QSize& documentSize, const QString& layerName)
{
    auto document = std::make_shared<SmartDocument>();
    document->size = documentSize;
    document->format = grid.format();

    auto layer = LayerData::create(LayerType::Raster, layerName);
    layer->tileGrid = aether::cloneGridWithSolids(grid);
    document->roots.append(std::move(layer));
    return document;
}

} // namespace ruwa::core::layers
