#pragma once

#include "base/qt/qt_key_modifiers.h"

namespace base {

[[nodiscard]] inline bool IsExtendedContextMenuModifierPressed() {
	return IsShiftPressed() || IsCtrlPressed();
}

} // namespace base
