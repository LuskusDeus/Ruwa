// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   R U W A   |   C O R E   |   T R A N S F O R M   S E S S I O N   U N D O
// ==========================================================================

#ifndef RUWA_CORE_TRANSFORM_TRANSFORMSESSIONCOMMAND_H
#define RUWA_CORE_TRANSFORM_TRANSFORMSESSIONCOMMAND_H

#include "features/transform/TransformController.h"
#include "shared/undo/UndoManager.h"

#include <QString>

#include <functional>
#include <utility>

namespace aether {

class TransformSessionCommand : public IUndoCommand {
public:
    using StateRestoredCallback = std::function<void()>;

    TransformSessionCommand(TransformController* controller, TransformState before,
        TransformInteractionMode beforeMode, TransformState after,
        TransformInteractionMode afterMode, StateRestoredCallback onStateRestored)
        : m_controller(controller)
        , m_before(std::move(before))
        , m_beforeMode(beforeMode)
        , m_after(std::move(after))
        , m_afterMode(afterMode)
        , m_onStateRestored(std::move(onStateRestored))
    {
    }

    void undo() override { restore(m_before, m_beforeMode); }

    void redo() override { restore(m_after, m_afterMode); }

    QString text() const override { return QStringLiteral("Transform Step"); }

    qint64 memorySize() const override
    {
        return static_cast<qint64>(sizeof(*this)) + deformMeshMemory(m_before)
            + deformMeshMemory(m_after);
    }

private:
    static qint64 deformMeshMemory(const TransformState& state)
    {
        if (!state.deformMesh.has_value()) {
            return 0;
        }
        return static_cast<qint64>(
            state.deformMesh->vertices.capacity() * sizeof(TransformState::DeformVertex));
    }

    void restore(const TransformState& state, TransformInteractionMode mode)
    {
        if (!m_controller || !m_controller->isActive()) {
            return;
        }
        m_controller->restoreStateForUndo(state, mode);
        if (m_onStateRestored) {
            m_onStateRestored();
        }
    }

    TransformController* m_controller = nullptr;
    TransformState m_before;
    TransformInteractionMode m_beforeMode = TransformInteractionMode::Classic;
    TransformState m_after;
    TransformInteractionMode m_afterMode = TransformInteractionMode::Classic;
    StateRestoredCallback m_onStateRestored;
};

} // namespace aether

#endif // RUWA_CORE_TRANSFORM_TRANSFORMSESSIONCOMMAND_H
