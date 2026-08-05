// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   S M A R T   C O N T E N T   S W A P
// ============================================================================

#include "features/canvas/undo/SmartContentSwapCommand.h"

#include "features/layers/smart/SmartDocument.h"
#include "shared/tiles/TileGrid.h"

#include <algorithm>
#include <utility>

namespace aether {
namespace {

qint64 gridMemorySize(const TileGrid* grid)
{
    if (!grid) {
        return 0;
    }
    return static_cast<qint64>(grid->tiles().size())
        * static_cast<qint64>(tileByteSize(grid->format()) + sizeof(TileKey) + 64);
}

qint64 documentMemorySize(const ruwa::core::layers::SmartDocument* document)
{
    return document ? document->approximateMemoryBytes() : qint64 { 0 };
}

} // namespace

SmartContentSwapCommand::SmartContentSwapCommand(
    std::shared_ptr<ruwa::core::layers::SmartContent> content, SmartContentState replacedState,
    QString commandText, OnContentChangedFn onContentChanged)
    : m_content(std::move(content))
    , m_inactiveState(std::move(replacedState))
    , m_text(std::move(commandText))
    , m_onContentChanged(std::move(onContentChanged))
{
    // The two states trade places on every undo/redo, so the footprint this
    // command is responsible for is the larger of them — and a document-backed
    // content parks its nested layers alongside the composite, which can dwarf it.
    const qint64 appliedMemory = m_content
        ? gridMemorySize(m_content->grid.get()) + documentMemorySize(m_content->document.get())
        : qint64 { 0 };
    const qint64 storedMemory = gridMemorySize(m_inactiveState.grid.get())
        + documentMemorySize(m_inactiveState.document.get());
    m_stateMemoryUpperBound = std::max(storedMemory, appliedMemory);
}

void SmartContentSwapCommand::undo()
{
    swapWithContent();
}

void SmartContentSwapCommand::redo()
{
    swapWithContent();
}

void SmartContentSwapCommand::swapWithContent()
{
    if (!m_content) {
        return;
    }

    // Park the live pixels, install the stored ones. setGrid() rather than a
    // plain member swap: it is what bumps contentRevision, and every cache keyed
    // on contentStamp() (projections, effects, the bounds cache) depends on that
    // bump to notice the swap.
    std::unique_ptr<TileGrid> incoming = std::move(m_inactiveState.grid);
    auto incomingDocument = std::move(m_inactiveState.document);
    const quint64 incomingCompositeRevision = m_inactiveState.compositeRevision;

    m_inactiveState.grid = std::move(m_content->grid);
    m_inactiveState.document = m_content->document;
    m_inactiveState.compositeRevision = m_content->compositeRevision;

    m_content->setGrid(std::move(incoming));
    // setGrid() dropped whatever document was there (its pixels are parked
    // above); put back the one that belongs to the pixels just installed.
    m_content->adoptDocument(std::move(incomingDocument), incomingCompositeRevision);

    using std::swap;
    swap(m_content->sourcePath, m_inactiveState.sourcePath);
    swap(m_content->sourceKind, m_inactiveState.sourceKind);
    swap(m_content->sourceHash, m_inactiveState.sourceHash);

    if (m_onContentChanged) {
        m_onContentChanged(m_content->contentId);
    }
}

QString SmartContentSwapCommand::text() const
{
    return m_text;
}

qint64 SmartContentSwapCommand::memorySize() const
{
    return sizeof(SmartContentSwapCommand) + m_stateMemoryUpperBound
        + static_cast<qint64>(m_inactiveState.sourcePath.capacity()) * sizeof(QChar)
        + static_cast<qint64>(m_inactiveState.sourceHash.capacity());
}

} // namespace aether
