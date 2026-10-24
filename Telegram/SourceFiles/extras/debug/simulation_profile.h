#pragma once

#ifdef _DEBUG
#include "extras/debug/debug_commands.h"

namespace ExtrasDebug::Commands {

[[nodiscard]] Result seedProfilePreview(const QStringList &args);

} // namespace ExtrasDebug::Commands
#endif
