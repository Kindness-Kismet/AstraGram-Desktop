#include "extras/extras_worker.h"

#include "apiwrap.h"
#include "extras_settings.h"
#include "base/timer.h"
#include "base/unixtime.h"
#include "core/application.h"
#include "data/data_user.h"
#include "data/entities.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"

namespace ExtrasWorker {

void runOnce();

std::unordered_map<ID, bool> state;

base::Timer &workerTimer() {
	static base::Timer timer([] {
		runOnce();
	});
	return timer;
}

void markAsOnline(not_null<Main::Session*> session) {
	state[session->userId().bare] = true;
	workerTimer().cancel();
	workerTimer().callEach(3000);
}

void lateInit() {
	for (const auto &[index, account] : Core::App().domain().accounts()) {
		if (const auto session = account->maybeSession()) {
			const auto id = session->userId().bare;
			state[id] = true;
		}
	}
}

void runOnce() {
	if (!Core::IsAppLaunched() || !Core::App().domain().started() || Core::Quitting()) {
		return;
	}

	if (state.empty()) {
		lateInit();
	}

	const auto t = base::unixtime::now();

	for (const auto &[index, account] : Core::App().domain().accounts()) {
		const auto session = account ? account->maybeSession() : nullptr;
		if (!session) {
			continue;
		}
		const auto id = session->userId().bare;
		if (!state.contains(id)) {
			state[id] = true;
		}

		const auto &ghost = ExtrasSettings::ghost(session);
		if (!ghost.sendOfflinePacketAfterOnline()) {
			continue;
		}
		if (!state[id] && !session->user()->lastseen().isOnline(t)) {
			continue;
		}
		session->api().request(MTPaccount_UpdateStatus(
			MTP_bool(true)
		)).send();
		state[id] = false;

		DEBUG_LOG(("[AyuGram] Sent offline for account with id %1").arg(id));
	}
}

void initialize() {
	workerTimer().callEach(3000);
}

}
