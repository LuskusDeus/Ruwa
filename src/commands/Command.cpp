// SPDX-License-Identifier: MPL-2.0

// ======================================================================================
//   R U W A   E N G I N E   |   C O M M A N D   S Y S T E M
// ======================================================================================

#include "commands/Command.h"
#include "commands/CommandContext.h"

namespace ruwa::core {

bool Command::canExecute(const CommandContext& ctx) const
{
    Q_UNUSED(ctx);
    return true;
}

} // namespace ruwa::core
