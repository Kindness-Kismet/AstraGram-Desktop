#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "apiwrap.h"
#include "boxes/filters/edit_filter_box.h"
#include "data/data_chat_filters.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "dialogs/dialogs_indexed_list.h"
#include "dialogs/dialogs_main_list.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;

[[nodiscard]] const Data::ChatFilter *findFilter(FilterId id) {
	const auto session = ActiveSession();
	if (!session) {
		return nullptr;
	}
	const auto &list = session->data().chatsFilters().list();
	const auto i = ranges::find(list, id, &Data::ChatFilter::id);
	return i == end(list) ? nullptr : &*i;
}

[[nodiscard]] Result listFilters(const QStringList &args) {
	const auto session = ActiveSession();
	if (!args.empty() || !session) {
		return Result::Err(u"usage: chat-filter.list (requires a session)"_q);
	}
	auto result = Json::array();
	for (const auto &filter : session->data().chatsFilters().list()) {
		auto always = Json::array();
		auto never = Json::array();
		for (const auto history : filter.always()) {
			always.push_back(history->peer->id.value);
		}
		for (const auto history : filter.never()) {
			never.push_back(history->peer->id.value);
		}
		result.push_back({
			{ "id", filter.id() },
			{ "title", filter.titleText().text.toStdString() },
			{ "flags", filter.flags().value() },
			{ "always", std::move(always) },
			{ "never", std::move(never) },
			{ "include", ExtrasChatFilters::rulesToText(filter.localRules().include).toStdString() },
			{ "exclude", ExtrasChatFilters::rulesToText(filter.localRules().exclude).toStdString() },
			{ "rows", filter.id() ? session->data().chatsFilters().chatsList(filter.id())->indexed()->size() : 0 },
		});
	}
	return Result::Ok(Compact(result));
}

[[nodiscard]] Result localRules(const QStringList &args) {
	if (args.size() != 1 && args.size() != 3) {
		return Result::Err(u"usage: chat-filter.rules <id> [<include> <exclude>]"_q);
	}
	auto ok = false;
	const auto id = args[0].toInt(&ok);
	const auto filter = ok && id > 0 ? findFilter(id) : nullptr;
	if (!filter) {
		return Result::Err(u"chat filter not found"_q);
	}
	if (args.size() == 3) {
		ActiveSession()->data().chatsFilters().setLocalRules(id, {
			ExtrasChatFilters::parseRules(args[1]),
			ExtrasChatFilters::parseRules(args[2]),
		});
	}
	return Result::Ok(Compact(Json{
		{ "include", ExtrasChatFilters::rulesToText(filter->localRules().include).toStdString() },
		{ "exclude", ExtrasChatFilters::rulesToText(filter->localRules().exclude).toStdString() },
	}));
}

[[nodiscard]] Result checkFilter(const QStringList &args) {
	if (args.size() != 2) {
		return Result::Err(u"usage: chat-filter.check <id> <peerId>"_q);
	}
	auto ok = false;
	const auto id = args[0].toInt(&ok);
	const auto filter = ok && id > 0 ? findFilter(id) : nullptr;
	const auto peer = findPeer(args[1]);
	const auto history = peer ? peer->owner().historyLoaded(peer) : nullptr;
	if (!filter || !history) {
		return Result::Err(u"chat filter or loaded history not found"_q);
	}
	const auto &rules = filter->localRules();
	return Result::Ok(Compact(Json{
		{ "contains", filter->contains(history) },
		{ "inChatList", history->inChatList(id) },
		{ "includeMatches", ExtrasChatFilters::matchesHistory(rules.include, history) },
		{ "excludeMatches", ExtrasChatFilters::matchesHistory(rules.exclude, history) },
		{ "lastMessageId", history->lastMessage() ? history->lastMessage()->id.bare : 0 },
		{ "lastMessageDate", history->lastMessage() ? history->lastMessage()->date() : 0 },
	}));
}

[[nodiscard]] Result saveFilter(const QStringList &args) {
	const auto session = ActiveSession();
	if (args.size() != 1 || !session) {
		return Result::Err(u"usage: chat-filter.save <JSON> (requires a session)"_q);
	}
	const auto data = Json::parse(args[0].toStdString(), nullptr, false);
	if (!data.is_object() || !data.contains("title") || !data["title"].is_string()) {
		return Result::Err(u"expected an object with a string title"_q);
	}
	for (const auto key : { "id", "flags" }) {
		if (data.contains(key) && !data[key].is_number_integer()) {
			return Result::Err(u"id and flags must be integers"_q);
		}
	}
	for (const auto key : { "include", "exclude" }) {
		if (data.contains(key) && !data[key].is_string()) {
			return Result::Err(u"include and exclude must be strings"_q);
		}
	}
	auto id = data.value("id", 0);
	auto flags = data.value("flags", 0);
	const auto title = QString::fromStdString(data["title"].get<std::string>());
	if (id < 0 || flags < 0 || flags > 255 || title.isEmpty()) {
		return Result::Err(u"invalid id, flags or title"_q);
	}
	if (!id) {
		id = 2;
		while (findFilter(id)) {
			++id;
		}
	}
	auto always = base::flat_set<not_null<History*>>();
	for (const auto key : { "always", "never" }) {
		if (data.contains(key) && !data[key].is_array()) {
			return Result::Err(u"always and never must be peer id arrays"_q);
		}
	}
	auto never = base::flat_set<not_null<History*>>();
	for (const auto key : { "always", "never" }) {
		for (const auto &value : data.value(key, Json::array())) {
			if (!value.is_number_unsigned() && !value.is_number_integer()) {
				return Result::Err(u"peer ids must be integers"_q);
			}
			const auto peer = session->data().peerLoaded(PeerId(value.get<uint64>()));
			if (!peer) {
				return Result::Err(u"peer is not loaded"_q);
			}
			(key == std::string_view("always") ? always : never).insert(session->data().history(peer));
		}
	}
	const auto rules = Data::ChatFilterLocalRules{
		ExtrasChatFilters::parseRules(QString::fromStdString(data.value("include", std::string()))),
		ExtrasChatFilters::parseRules(QString::fromStdString(data.value("exclude", std::string()))),
	};
	if (!(flags & 31) && always.empty() && !rules.empty()) {
		flags |= 31;
	}
	const auto filter = Data::ChatFilter(id, { TextWithEntities{ title }, false }, {}, {},
		Data::ChatFilter::Flags::from_raw(ushort(flags)), std::move(always), {}, std::move(never)
	).withLocalRules(rules);
	const auto job = beginJob("chat-filter.save");
	session->api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(MTPmessages_UpdateDialogFilter::Flag::f_filter),
		MTP_int(id), filter.tl()
	)).done([=] {
		auto &filters = session->data().chatsFilters();
		filters.setLocalRules(id, rules);
		filters.set(filter);
		finishJob(job, true, Json{ { "id", id }, { "state", "saved" } });
	}).fail([=](const MTP::Error &error) {
		finishJob(job, false, Json{ { "error", error.type().toStdString() } });
	}).send();
	return jobStarted(job);
}

[[nodiscard]] Result removeFilter(const QStringList &args) {
	if (args.size() != 1 || !ActiveSession()) {
		return Result::Err(u"usage: chat-filter.remove <id>"_q);
	}
	auto ok = false;
	const auto id = args[0].toInt(&ok);
	if (!ok || id <= 0 || !findFilter(id)) {
		return Result::Err(u"chat filter not found"_q);
	}
	const auto session = ActiveSession();
	const auto job = beginJob("chat-filter.remove");
	session->api().request(MTPmessages_UpdateDialogFilter(
		MTP_flags(0), MTP_int(id), MTPDialogFilter()
	)).done([=] {
		session->data().chatsFilters().remove(id);
		finishJob(job, true, Json{ { "id", id }, { "state", "removed" } });
	}).fail([=](const MTP::Error &error) {
		finishJob(job, false, Json{ { "error", error.type().toStdString() } });
	}).send();
	return jobStarted(job);
}

[[nodiscard]] Result editFilter(const QStringList &args) {
	if (args.size() != 1 || !ActiveSession()) {
		return Result::Err(u"usage: chat-filter.edit <id>"_q);
	}
	auto ok = false;
	const auto id = args[0].toInt(&ok);
	const auto controller = ActiveSession()->tryResolveWindow();
	if (!ok || id <= 0 || !findFilter(id) || !controller) {
		return Result::Err(u"chat filter or window not found"_q);
	}
	EditExistingFilter(controller, id);
	return Result::Ok();
}

[[nodiscard]] Result reloadFilters(const QStringList &args) {
	if (!args.empty() || !ActiveSession()) {
		return Result::Err(u"usage: chat-filter.reload"_q);
	}
	ActiveSession()->data().chatsFilters().reload();
	return Result::Ok(u"reload requested"_q);
}

} // namespace

const HandlerMap &chatFilterHandlers() {
	static const auto result = HandlerMap{
		{ u"chat-filter.list"_q, &listFilters },
		{ u"chat-filter.rules"_q, &localRules },
		{ u"chat-filter.check"_q, &checkFilter },
		{ u"chat-filter.save"_q, &saveFilter },
		{ u"chat-filter.remove"_q, &removeFilter },
		{ u"chat-filter.edit"_q, &editFilter },
		{ u"chat-filter.reload"_q, &reloadFilters },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif
