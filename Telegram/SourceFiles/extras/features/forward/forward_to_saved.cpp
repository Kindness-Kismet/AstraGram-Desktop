#include "extras/features/forward/forward_to_saved.h"

#include "api/api_sending.h"
#include "apiwrap.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "extras/features/forward/extras_forward.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_helpers.h"
#include "history/view/controls/history_view_forward_panel.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/toast/toast.h"

namespace ExtrasForward {
namespace {

HistoryItemsList resolveItems(
		not_null<Main::Session*> session,
		const MessageIdsList &ids) {
	auto items = session->data().idsToItems(ids);
	if (items.empty() || items.size() != ids.size()) {
		return {};
	}
	for (const auto item : items) {
		if (!IsServerMsgId(item->id)
			|| !item->allowsForward()
			|| IsAnchoredEphemeral(item)) {
			return {};
		}
	}
	return items;
}

std::vector<Data::ForwardOptions> availableOptions(
		not_null<Main::Session*> session,
		const HistoryItemsList &items) {
	if (items.empty()) {
		return {};
	}
	using Options = Data::ForwardOptions;
	auto result = std::vector<Options>();
	const auto copies = isExtrasForwardNeeded(items)
		|| ranges::any_of(items, [](const auto item) {
			return isFullExtrasForwardNeeded(item);
		});
	if (!copies) {
		result.push_back(Options::PreserveInfo);
	}
	if (HistoryView::Controls::CanHideForwardAuthor(session, items)) {
		result.push_back(Options::NoSenderNames);
		if (ItemsForwardCaptionsCount(items) > 0) {
			result.push_back(Options::NoNamesAndCaptions);
		}
	}
	return result;
}

} // namespace

std::vector<Data::ForwardOptions> savedForwardOptions(
		not_null<Main::Session*> session,
		const MessageIdsList &ids) {
	return availableOptions(session, resolveItems(session, ids));
}

SavedForwardError forwardToSaved(
		not_null<Main::Session*> session,
		const MessageIdsList &ids,
		Data::ForwardOptions options) {
	auto items = resolveItems(session, ids);
	if (items.empty()) {
		return SavedForwardError::Unavailable;
	}
	if (!ranges::contains(availableOptions(session, items), options)) {
		return SavedForwardError::UnsupportedMode;
	}
	const auto history = session->data().history(session->user());
	auto action = Api::SendAction(history);
	action.clearDraft = false;
	action.generateLocal = false;
	// 直接使用选定模式，避免接收人选择流程忽略快捷转发预设。
	session->api().forwardMessages({ std::move(items), options }, action, [] {
		Ui::Toast::Show(tr::extras_ForwardSavedDone(tr::now));
	});
	return SavedForwardError::None;
}

} // namespace ExtrasForward
