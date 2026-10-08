#pragma once

namespace Main {
class Session;
} // namespace Main

namespace ExtrasState {

void setDisableGhostModeOnStoryClose(Main::Session *session);
void disableGhostModeOnStoryClose(Main::Session *session);

}
