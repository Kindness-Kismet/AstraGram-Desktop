#pragma once

#ifdef _DEBUG
#include "extras/debug/debug_commands.h"

#include <QtCore/QStringList>

namespace ExtrasDebug::Commands {

[[nodiscard]] Result verifyMessageArchive(const QStringList &args);

}
#endif
