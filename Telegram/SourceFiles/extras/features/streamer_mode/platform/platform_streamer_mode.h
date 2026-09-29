#pragma once

#include "base/basic_types.h"

class QWidget;

namespace ExtrasFeatures::StreamerMode::Platform {

void SetWindowCaptureExcluded(
	not_null<QWidget*> widget,
	bool excluded);

} // namespace ExtrasFeatures::StreamerMode::Platform
