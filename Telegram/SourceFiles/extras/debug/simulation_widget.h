#pragma once

#ifdef _DEBUG
#include "extras/debug/debug_commands.h"

class HistoryItem;

namespace ExtrasDebug::Commands {

[[nodiscard]] Result simulateWidget(const QStringList &args);
[[nodiscard]] Result verifyWidget(not_null<HistoryItem*> item);

} // namespace ExtrasDebug::Commands
#endif
