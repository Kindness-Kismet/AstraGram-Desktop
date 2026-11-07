#pragma once

#ifdef _DEBUG
#include "extras/debug/debug_commands.h"

namespace ExtrasDebug::Commands {

[[nodiscard]] Result simulateWidget(const QStringList &args);

} // namespace ExtrasDebug::Commands
#endif
