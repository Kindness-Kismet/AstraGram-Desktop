#pragma once

#include "window/window_session_controller.h"

namespace ExtrasWorker {

void markAsOnline(not_null<Main::Session*> session);
void initialize();

}
