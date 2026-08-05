// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   S M A R T   C O N T E N T
// ============================================================================

#include "features/layers/model/SmartContent.h"

#include "features/layers/smart/SmartDocument.h"

#include <cmath>
#include <cstring>
#include <utility>

namespace ruwa::core::layers {

SmartContent::~SmartContent() = default;

void SmartContent::setGrid(std::unique_ptr<aether::TileGrid> newGrid)
{
    grid = newGrid ? std::move(newGrid) : std::make_unique<aether::TileGrid>();
    // These pixels are the content now; the layers that used to produce them are
    // not. See the header for why they are dropped rather than kept.
    document.reset();
    compositeRevision = 0;
    markContentChanged();
}

quint64 SmartContent::documentRevision() const
{
    return document ? document->revision : 0ull;
}

bool SmartContent::compositeUpToDate() const
{
    return !document || compositeRevision == document->revision;
}

void SmartContent::setDocument(
    std::shared_ptr<SmartDocument> newDocument, quint64 compositeRevisionForCurrentGrid)
{
    document = std::move(newDocument);
    compositeRevision = document ? compositeRevisionForCurrentGrid : 0ull;
    markContentChanged();
}

SmartDocument* SmartContent::wrapGridInDocument(const QString& layerName)
{
    if (document) {
        return document.get();
    }
    if (!grid) {
        grid = std::make_unique<aether::TileGrid>();
    }

    const QString name = layerName.isEmpty() ? QStringLiteral("Contents") : layerName;
    document = SmartDocument::fromGrid(*grid, contentSpaceSize(), name);
    // The grid the document was built from is, by construction, that document
    // composited — so the cache starts valid and nothing has to be recomposited
    // just because the content grew layers.
    compositeRevision = document->revision;
    // No markContentChanged(): not one pixel moved, and bumping would drop every
    // projection and effect cache of every instance for nothing.
    return document.get();
}

void SmartContent::markDocumentChanged()
{
    if (!document) {
        return;
    }
    document->markChanged();
    // The bounds cache validates against contentStamp(), which folds the
    // document revision — but it is invalidated here anyway so the invariant
    // "the pixels this describes may have changed" holds without relying on the
    // stamp mix.
    m_boundsValid = false;
}

void SmartContent::setCompositeGrid(
    std::unique_ptr<aether::TileGrid> composited, quint64 forRevision)
{
    grid = composited ? std::move(composited) : std::make_unique<aether::TileGrid>();
    compositeRevision = forRevision;
    m_boundsValid = false;
}

void SmartContent::markCompositeCurrent()
{
    if (document) {
        compositeRevision = document->revision;
    }
}

void SmartContent::adoptDocument(
    std::shared_ptr<SmartDocument> newDocument, quint64 compositeRevisionValue)
{
    document = std::move(newDocument);
    compositeRevision = document ? compositeRevisionValue : 0ull;
    m_boundsValid = false;
}

QSize SmartContent::contentSpaceSize() const
{
    if (document && document->size.isValid() && !document->size.isEmpty()) {
        return document->size;
    }

    const aether::Rect bounds = nativeBounds();
    const QSize size(static_cast<int>(std::ceil(bounds.x + bounds.width)),
        static_cast<int>(std::ceil(bounds.y + bounds.height)));
    if (size.width() <= 0 || size.height() <= 0) {
        // Nothing painted yet: there is no size to derive from empty pixels.
        return QSize(512, 512);
    }
    return size;
}

aether::TilePixelFormat SmartContent::storageFormat() const
{
    if (document) {
        return document->format;
    }
    return grid ? grid->format() : aether::kDefaultTileFormat;
}

std::shared_ptr<SmartContent> SmartContent::cloneDetached() const
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
    if (document) {
        // The clone keeps the document's revision, and the pixels above are the
        // same composite, so the copy's cache is valid from the start.
        copy->document = document->clone();
        copy->compositeRevision = compositeRevision;
    }
    return copy;
}

} // namespace ruwa::core::layers
