#include "extras/features/delete_messages/delete_own_messages.h"

#include "base/flat_map.h"
#include "base/timer.h"
#include "data/data_peer.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "styles/style_chat.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"

#ifdef _DEBUG
#include "extras/debug/debug_login.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#endif

namespace ExtrasDeleteMessages {
namespace {

constexpr auto kUndoDuration = crl::time(5000);

struct PendingToast {
	base::weak_ptr<Ui::Toast::Instance> toast;
	Fn<void()> cancel;
};

base::flat_map<Main::Session*, PendingToast> PendingToasts;

struct PendingDeletion {
	rpl::variable<int> seconds = 5;
	base::Timer timer;
	bool pending = true;
};

#ifdef _DEBUG
void deleteFakeOwnMessages(not_null<PeerData*> peer) {
	const auto session = &peer->session();
	const auto history = session->data().historyLoaded(peer);
	if (!history) {
		return;
	}
	const auto ids = history->collectMessagesFromParticipantToDelete(session->user());
	auto items = std::vector<not_null<HistoryItem*>>();
	for (const auto id : ids) {
		if (const auto item = session->data().message(peer->id, id)) {
			items.push_back(item);
		}
	}
	session->data().destroyMessagesWithCacheCleanup(items);
	session->data().sendHistoryChangeNotifications();
}
#endif

}

void scheduleDeleteOwnMessages(
		not_null<Window::SessionController*> controller,
		not_null<PeerData*> peer,
		Fn<void()> deleteMessages) {
	const auto session = &peer->session();
	if (const auto i = PendingToasts.find(session); i != end(PendingToasts)) {
		i->second.cancel();
	}

	const auto &st = st::historyPremiumToast;
	const auto undoText = tr::extras_DeleteOwnMessagesUndo(tr::now, lt_seconds, u"5"_q);
	const auto buttonWidth = st::historyPremiumViewSet.style.font->width(undoText)
		- st::historyPremiumViewSet.width;
	const auto toast = controller->showToast({
		.text = tr::extras_DeleteOwnMessagesPending(
			tr::now, lt_chat, tr::bold(peer->name()), tr::rich),
		.padding = rpl::single(QMargins(0, 0, buttonWidth - st.padding.right(), 0)),
		.st = &st,
		.attach = RectPart::Bottom,
		.acceptinput = true,
		.infinite = true,
	});
	const auto strong = toast.get();
	if (!strong) {
		return;
	}
	const auto widget = strong->widget();
	widget->setObjectName(u"deleteOwnMessagesToast"_q);
	widget->lifetime().add([=] {
		const auto i = PendingToasts.find(session);
		if (i != end(PendingToasts) && i->second.toast.get() == toast.get()) {
			PendingToasts.erase(i);
		}
	});
	const auto state = widget->lifetime().make_state<PendingDeletion>();
	const auto finish = crl::now() + kUndoDuration;
	const auto hide = [=] {
		state->pending = false;
		state->timer.cancel();
		if (const auto strong = toast.get()) {
			strong->hideAnimated();
		}
	};
	PendingToasts[session] = { toast, hide };
	const auto button = Ui::CreateChild<Ui::RoundButton>(
		widget,
		tr::extras_DeleteOwnMessagesUndo(
			lt_seconds,
			state->seconds.value() | rpl::map([](int seconds) {
				return QString::number(seconds);
			})),
		st::historyPremiumViewSet);
	button->setObjectName(u"deleteOwnMessagesUndo"_q);
	button->setClickedCallback(hide);
	button->show();
	rpl::combine(widget->sizeValue(), button->sizeValue())
		| rpl::on_next([=](QSize outer, QSize inner) {
			button->moveToRight(0, (outer.height() - inner.height()) / 2, outer.width());
		}, widget->lifetime());

	const auto weakSession = base::make_weak(session);
	state->timer.setCallback([=] {
		const auto left = finish - crl::now();
		if (left > 0) {
			state->seconds = int((left + 999) / 1000);
			state->timer.callOnce((left - 1) % 1000 + 1, Qt::PreciseTimer);
			return;
		}
		if (!state->pending) {
			return;
		}
		hide();
		button->setDisabled(true);
		if (!weakSession) {
			return;
		}
#ifdef _DEBUG
		if (ExtrasDebug::isFakeSession(session)) {
			deleteFakeOwnMessages(peer);
			return;
		}
#endif
		deleteMessages();
	});
	state->timer.callOnce(1000, Qt::PreciseTimer);
}

}
