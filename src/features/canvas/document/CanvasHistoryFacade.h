// SPDX-License-Identifier: MPL-2.0

#ifndef RUWA_FEATURES_CANVAS_DOCUMENT_CANVASHISTORYFACADE_H
#define RUWA_FEATURES_CANVAS_DOCUMENT_CANVASHISTORYFACADE_H

#include <QString>
#include <QtGlobal>

#include <memory>

namespace aether {
class IUndoCommand;
class UndoManager;
} // namespace aether

namespace ruwa::ui::workspace {

/// TRANSITIONAL (plan 7.30.1): application-owned history facade.
///
/// Undo/redo state, memory limit, transactions and legacy command push for one
/// canvas. This is deliberately NOT a CanvasEngineSession capability: history
/// ownership moves to an application document subsystem in a later stage, and
/// the facade keeps that unresolved ownership question out of the renderer
/// contract. Exposed by CanvasEngineQtBinding::history().
///
/// `pushLegacyCommand` is intentionally ugly: it documents remaining Aether
/// command ownership instead of laundering it behind a neutral permanent name.
class CanvasHistoryFacade {
public:
    virtual ~CanvasHistoryFacade() = default;

    virtual bool canUndo() const = 0;
    virtual bool canRedo() const = 0;
    virtual void undo() = 0;
    virtual void redo() = 0;
    virtual void setMemoryLimit(qint64 bytes) = 0;
    virtual void beginTransaction(const QString& text) = 0;
    virtual void endTransaction() = 0;

    /// Explicit quarantine until command ownership is redesigned (plan 7.30.1).
    virtual void pushLegacyCommand(std::unique_ptr<aether::IUndoCommand> command) = 0;

    /// TRANSITIONAL QUARANTINE: raw legacy manager for the few remaining
    /// internal call sites; every use is enumerated in
    /// docs/renderer-boundary-quarantine.md. Callers never route
    /// active-history selection through this pointer themselves.
    virtual aether::UndoManager* legacyUndoManager() = 0;
};

} // namespace ruwa::ui::workspace

#endif // RUWA_FEATURES_CANVAS_DOCUMENT_CANVASHISTORYFACADE_H
