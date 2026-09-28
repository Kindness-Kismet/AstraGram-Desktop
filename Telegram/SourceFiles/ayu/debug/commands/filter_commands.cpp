#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "ayu/ayu_settings.h"
#include "ayu/data/ayu_database.h"
#include "ayu/features/filters/filters_controller.h"
#include "ayu/features/filters/filters_cache_controller.h"
#include "ayu/utils/telegram_helpers.h"
#include "history/history_item.h"
#include "history/history.h"
#include <QUuid>

namespace AyuDebug::Commands {
namespace {

using Json = nlohmann::json;

QString filterId(const std::vector<char> &id) {
	return QString::fromLatin1(QByteArray(id.data(), int(id.size())).toHex());
}

Json describeFilter(const RegexFilter &filter) {
	return {{"id", filterId(filter.id).toStdString()}, {"text", filter.text},
		{"enabled", filter.enabled}, {"reversed", filter.reversed},
		{"caseInsensitive", filter.caseInsensitive},
		{"dialogId", filter.dialogId ? Json(*filter.dialogId) : Json(nullptr)}};
}

std::optional<RegexFilter> findFilter(const QString &id) {
	for (const auto &filter : AyuDatabase::getAllRegexFilters()) {
		if (filterId(filter.id) == id) return filter;
	}
	return std::nullopt;
}

void refreshFilters() {
	FiltersCacheController::rebuildCache();
	FiltersCacheController::fireUpdate();
}

Result listFilters(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: filter.list"_q);
	auto list = Json::array();
	for (const auto &filter : AyuDatabase::getAllRegexFilters()) list.push_back(describeFilter(filter));
	return Result::Ok(Compact(list));
}

Result putFilter(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: filter.put <json>"_q);
	const auto data = Json::parse(args[0].toStdString(), nullptr, false);
	if (!data.is_object()) return Result::Err(u"expected a JSON object"_q);
	const auto existing = data.contains("id")
		? findFilter(QString::fromStdString(data.at("id").get<std::string>()))
		: std::nullopt;
	if (data.contains("id") && !existing) return Result::Err(u"filter not found"_q);
	auto filter = existing.value_or(RegexFilter{});
	const auto text = data.value("text", filter.text);
	if (text.empty()) return Result::Err(u"filter text must not be empty"_q);
	auto status = U_ZERO_ERROR;
	const auto insensitive = data.value("caseInsensitive", existing ? filter.caseInsensitive : false);
	const auto pattern = std::unique_ptr<icu::RegexPattern>(icu::RegexPattern::compile(
		icu::UnicodeString::fromUTF8(text), insensitive ? UREGEX_CASE_INSENSITIVE : 0, status));
	if (U_FAILURE(status)) return Result::Err(QString::fromLatin1(u_errorName(status)));
	for (const auto &[key, value] : data.items()) {
		if (key != "id" && key != "text" && key != "enabled" && key != "reversed"
			&& key != "caseInsensitive" && key != "dialogId") {
			return Result::Err(u"unknown filter field: "_q + QString::fromStdString(key));
		}
	}
	filter.text = text;
	filter.caseInsensitive = insensitive;
	filter.enabled = data.value("enabled", existing ? filter.enabled : true);
	filter.reversed = data.value("reversed", existing ? filter.reversed : false);
	if (data.contains("dialogId")) {
		filter.dialogId = data["dialogId"].is_null()
			? std::nullopt : std::optional<ID>(data["dialogId"].get<ID>());
	}
	if (existing) {
		AyuDatabase::updateRegexFilter(filter);
	} else {
		const auto id = QUuid::createUuid().toRfc4122();
		filter.id.assign(id.begin(), id.end());
		AyuDatabase::addRegexFilter(filter);
	}
	refreshFilters();
	const auto saved = findFilter(filterId(filter.id));
	return saved && *saved == filter ? Result::Ok(Compact(describeFilter(*saved)))
		: Result::Err(u"filter was not saved; check the application log"_q);
}

Result removeFilter(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: filter.remove <id>"_q);
	const auto filter = findFilter(args[0]);
	if (!filter) return Result::Err(u"filter not found"_q);
	AyuDatabase::deleteFilter(filter->id);
	AyuDatabase::deleteExclusionsByFilterId(filter->id);
	refreshFilters();
	return findFilter(args[0]) ? Result::Err(u"filter was not removed"_q) : Result::Ok();
}

Result exclusions(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: filter.exclusions"_q);
	auto list = Json::array();
	for (const auto &exclusion : AyuDatabase::getAllFiltersExclusions()) {
		list.push_back({{"dialogId", exclusion.dialogId}, {"filterId", filterId(exclusion.filterId).toStdString()}});
	}
	return Result::Ok(Compact(list));
}

Result excludeFilter(const QStringList &args) {
	if (args.size() != 3 || (args[2] != u"true"_q && args[2] != u"false"_q)) {
		return Result::Err(u"usage: filter.exclude <id> <peerId> <true|false>"_q);
	}
	const auto filter = findFilter(args[0]);
	const auto peer = findPeer(args[1]);
	if (!filter || !peer) return Result::Err(u"filter or peer not found"_q);
	if (filter->dialogId) return Result::Err(u"only global filters support exclusions"_q);
	const auto dialog = getDialogIdFromPeer(peer);
	AyuDatabase::deleteExclusion(dialog, filter->id);
	if (args[2] == u"true"_q) {
		auto exclusion = RegexFilterGlobalExclusion{};
		exclusion.dialogId = dialog;
		exclusion.filterId = filter->id;
		AyuDatabase::addRegexExclusion(exclusion);
	}
	refreshFilters();
	return Result::Ok();
}

Result checkFilter(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: filter.check <peerId> <messageId>"_q);
	const auto item = findMessage(args[0], args[1]);
	if (!item) return Result::Err(u"message not found"_q);
	return Result::Ok(Compact(Json{{"enabled", FiltersController::isEnabled(item->history()->peer)},
		{"blocked", FiltersController::isBlocked(item)}, {"filtered", FiltersController::filtered(item)}}));
}

Result filterVisibility(const QStringList &args) {
	if (args.empty() || args.size() > 2) return Result::Err(u"usage: filter.visible <peerId> [true|false]"_q);
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	if (args.size() == 2) {
		if (args[1] != u"true"_q && args[1] != u"false"_q) return Result::Err(u"expected true or false"_q);
		if (FiltersController::filteredMessagesShown(peer).value_or(false) != (args[1] == u"true"_q)) {
			FiltersController::toggleFilteredMessagesShown(peer);
		}
	}
	return Result::Ok(Compact(FiltersController::filteredMessagesShown(peer).value_or(false)));
}

} // namespace

const HandlerMap &FilterHandlers() {
	static const auto result = HandlerMap{
		{u"filter.list"_q, &listFilters}, {u"filter.put"_q, &putFilter},
		{u"filter.remove"_q, &removeFilter}, {u"filter.exclusions"_q, &exclusions},
		{u"filter.exclude"_q, &excludeFilter}, {u"filter.check"_q, &checkFilter},
		{u"filter.visible"_q, &filterVisibility},
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif
