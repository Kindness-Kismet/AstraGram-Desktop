#include "extras/extras_state.h"

#include "extras/extras_settings.h"

namespace ExtrasState {

Main::Session *disableGhostModeOnStoryCloseSession = nullptr;

void setDisableGhostModeOnStoryClose(Main::Session *session) {
	disableGhostModeOnStoryCloseSession = session;
}

void disableGhostModeOnStoryClose(Main::Session *session) {
	if (disableGhostModeOnStoryCloseSession != session) {
		return;
	}
	disableGhostModeOnStoryCloseSession = nullptr;
	if (session) {
		ExtrasSettings::ghost(session).setGhostModeEnabled(false);
	}
}

}
