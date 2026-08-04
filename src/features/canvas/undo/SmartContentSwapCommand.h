// SPDX-License-Identifier: MPL-2.0

// ============================================================================
//   R U W A   |   C O R E   |   S M A R T   C O N T E N T   S W A P
// ============================================================================

#ifndef RUWA_CORE_UNDO_SMARTCONTENTSWAPCOMMAND_H
#define RUWA_CORE_UNDO_SMARTCONTENTSWAPCOMMAND_H

#include "features/layers/model/SmartContent.h"
#include "shared/undo/UndoManager.h"

#include <QByteArray>
#include <QString>
#include <QUuid>
#include <functional>
#include <memory>

namespace aether {

/**
 * Everything a smart content holds ABOUT ITS PIXELS, detached from the content.
 *
 * Deliberately not the whole SmartContent: contentId must not move, because it
 * is what ties the instances (and, from v29 on, the file's shared content
 * records) together. Replacing an object's contents changes what the pixels are,
 * not which object they belong to.
 */
struct SmartContentState {
    std::unique_ptr<TileGrid> grid;
    QString sourcePath;
    ruwa::core::layers::SmartSourceKind sourceKind = ruwa::core::layers::SmartSourceKind::Embedded;
    QByteArray sourceHash;
};

/**
 * @brief Swaps a smart content's pixels (and its source description) with a
 *        stored set — the undo half of "Replace Contents".
 *
 * This is a CONTENT-level command on purpose. Replacing an object's contents
 * means every instance of it shows the new thing, so the command holds the
 * shared content itself rather than a layer id, and undo reaches every instance
 * the same way the replacement did. (LayerContentSwapCommand is the layer-level
 * sibling: it swaps which content a single layer points at, which is what a
 * rasterize/convert does.)
 *
 * Undo and redo are the identical swap, so one command serves both directions.
 *
 * remapForCanvasResize is deliberately NOT overridden: these pixels live in
 * CONTENT space, which a canvas resize does not move. What does move is each
 * layer's smartTransform, and that is layer state.
 */
class SmartContentSwapCommand final : public IUndoCommand {
public:
    using OnContentChangedFn = std::function<void(const QUuid& contentId)>;

    SmartContentSwapCommand(std::shared_ptr<ruwa::core::layers::SmartContent> content,
        SmartContentState replacedState, QString commandText,
        OnContentChangedFn onContentChanged);

    void undo() override;
    void redo() override;
    QString text() const override;
    qint64 memorySize() const override;

private:
    void swapWithContent();

    std::shared_ptr<ruwa::core::layers::SmartContent> m_content;
    SmartContentState m_inactiveState;
    QString m_text;
    OnContentChangedFn m_onContentChanged;
    qint64 m_stateMemoryUpperBound = 0;
};

} // namespace aether

#endif // RUWA_CORE_UNDO_SMARTCONTENTSWAPCOMMAND_H
