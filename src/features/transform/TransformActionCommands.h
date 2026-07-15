// SPDX-License-Identifier: MPL-2.0

// ==========================================================================
//   T R A N S F O R M   A C T I O N   C O M M A N D S
// ==========================================================================
// Command-palette commands that activate transform mode (distinct from
// TransformCommand in this folder, which is the undo/redo command).

#ifndef RUWA_CORE_TRANSFORM_TRANSFORMACTIONCOMMANDS_H
#define RUWA_CORE_TRANSFORM_TRANSFORMACTIONCOMMANDS_H

#include "commands/Command.h"

#include <functional>

namespace ruwa::core {

class TransformActionCommand : public Command {
public:
    using TransformActivator = std::function<void()>;

    explicit TransformActionCommand(TransformActivator activator)
        : m_activator(std::move(activator))
    {
    }

    CommandInfo info() const override
    {
        CommandInfo ci;
        ci.id = "edit.transform";
        ci.title = "Free Transform";
        ci.category = "Edit";
        ci.description = "Transform selected layer (move, rotate, resize)";
        ci.aliases = { "transform", "resize", "scale", "rotate" };
        ci.defaultShortcut = QKeySequence(Qt::CTRL | Qt::Key_T);
        return ci;
    }

    bool canExecute(const CommandContext& ctx) const override
    {
        Q_UNUSED(ctx);
        return m_activator != nullptr;
    }

    void execute(const CommandContext& ctx, const QVariantMap& args = {}) override
    {
        Q_UNUSED(ctx);
        Q_UNUSED(args);
        if (m_activator) {
            m_activator();
        }
    }

private:
    TransformActivator m_activator;
};

} // namespace ruwa::core

#endif // RUWA_CORE_TRANSFORM_TRANSFORMACTIONCOMMANDS_H
