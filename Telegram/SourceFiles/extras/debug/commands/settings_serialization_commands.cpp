#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "chat_helpers/spellchecker_locale_ids.h"
#include "core/core_settings.h"
#include "extras/debug/debug_login.h"
#include "main/main_session_settings.h"
#include "settings.h"
#include "spellcheck/spellcheck_types.h"

#include <QDataStream>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;

constexpr auto kOldVersion = 7003000;
constexpr auto kAmbiguousVersion = 7003001;
constexpr auto kSamplePref = "serialization-check";
constexpr auto kSampleVolume = 0.37;

struct Fixture {
	QByteArray body;
	QByteArray tail;
};

struct Languages {
	std::vector<int> dictionaries;
	std::vector<int> skipped;
	int target = 0;
};

[[nodiscard]] QByteArray languageTail(
		const QStringList &skipped,
		const QString &target,
		qint32 format = 1) {
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream << format << qint32(skipped.size());
	for (const auto &code : skipped) {
		stream << code;
	}
	stream << target;
	return result;
}

// 样本主体由正式接口生成，只替换可选语言尾部模拟旧版与损坏数据。
[[nodiscard]] std::optional<Fixture> makeFixture(
		const std::vector<int> &languages,
		int dictionaryScheme = 0) {
	auto settings = Core::Settings();
	settings.setSongVolume(kSampleVolume);
	settings.setDownloadPath(u"settings-check/downloads/"_q);
	settings.setDisableOpenGL(true);
	settings.setNotificationsVolume(42);
	settings.setPullToNextChannel(false);
	settings.setChatFiltersTabsMode(Ui::ChatsFiltersTabsMode::IconsOnly);
	settings.writePref<int>(kSamplePref, 314);
	settings.setDictionariesEnabled(languages);
	if (dictionaryScheme) {
		settings.writePref<int>(
			Spellchecker::kDictionariesIdSchemePref,
			dictionaryScheme);
	}
	auto skipped = std::vector<LanguageId>();
	auto codes = QStringList();
	for (const auto raw : languages) {
		const auto language = QLocale::Language(raw);
		skipped.push_back({ language });
		codes.push_back(QLocale::languageToCode(language));
	}
	settings.setSkipTranslationLanguages(std::move(skipped));
	settings.setTranslateTo({ QLocale::Language(languages.front()) });
	auto serialized = settings.serialize();
	auto tail = languageTail(codes, codes.front());
	if (!serialized.startsWith("ASG1") || !serialized.endsWith(tail)) {
		return std::nullopt;
	}
	serialized.chop(tail.size());
	return Fixture{ std::move(serialized), std::move(tail) };
}

[[nodiscard]] Json describe(Core::Settings &settings) {
	auto skipped = std::vector<int>();
	for (const auto language : settings.skipTranslationLanguages()) {
		skipped.push_back(int(language.value));
	}
	return {
		{ "dictionaries", settings.dictionariesEnabled() },
		{ "skipped", skipped },
		{ "target", int(settings.translateTo().value) },
		{ "dictionaryScheme", settings.readPref<int>(
			Spellchecker::kDictionariesIdSchemePref) },
		{ "songVolume", settings.songVolume() },
		{ "downloadPath", settings.downloadPath().toStdString() },
		{ "disableOpenGL", settings.disableOpenGL() },
		{ "notificationsVolume", settings.notificationsVolume() },
		{ "pullToNextChannel", settings.pullToNextChannel() },
		{ "tabsMode", int(settings.chatFiltersTabsMode()) },
		{ "samplePref", settings.readPref<int>(kSamplePref) },
	};
}

[[nodiscard]] Json expectedState(const Languages &languages) {
	return {
		{ "dictionaries", languages.dictionaries },
		{ "skipped", languages.skipped },
		{ "target", languages.target },
		{ "dictionaryScheme", Spellchecker::LocalIdScheme() },
		{ "songVolume", kSampleVolume },
		{ "downloadPath", "settings-check/downloads/" },
		{ "disableOpenGL", true },
		{ "notificationsVolume", 42 },
		{ "pullToNextChannel", false },
		{ "tabsMode", int(Ui::ChatsFiltersTabsMode::IconsOnly) },
		{ "samplePref", 314 },
	};
}

void verifyCase(
		Json &cases,
		const char *name,
		const QByteArray &serialized,
		int writerVersion,
		const Languages &languages) {
	auto settings = Core::Settings();
	const auto parsed = settings.addFromSerialized(serialized, writerVersion);
	const auto actual = describe(settings);
	const auto expected = expectedState(languages);
	auto row = Json{
		{ "name", name },
		{ "writerVersion", writerVersion },
		{ "parsed", parsed },
		{ "actual", actual },
		{ "expected", expected },
		{ "passed", false },
	};
	if (!parsed || actual != expected) {
		cases.push_back(std::move(row));
		return;
	}
	// 沿用旧版本号重载，验证保存的编号方案与稳定代码不会二次转换。
	auto reloaded = Core::Settings();
	const auto reloadedOk = reloaded.addFromSerialized(
		settings.serialize(),
		writerVersion);
	const auto roundTrip = describe(reloaded);
	row["roundTrip"] = roundTrip;
	row["roundTripParsed"] = reloadedOk;
	row["passed"] = reloadedOk && roundTrip == expected;
	cases.push_back(std::move(row));
}

void verifyRejectedCase(
		Json &cases,
		const char *name,
		const QByteArray &serialized) {
	auto settings = Core::Settings();
	const auto before = settings.serialize();
	const auto expected = describe(settings);
	const auto parsed = settings.addFromSerialized(serialized, kAmbiguousVersion);
	const auto unchanged = settings.serialize() == before;
	cases.push_back(Json{
		{ "name", name },
		{ "parsed", parsed },
		{ "actual", describe(settings) },
		{ "expected", expected },
		{ "unchanged", unchanged },
		{ "passed", !parsed && unchanged },
	});
}

// 旧账号样本在词典设置后结束，后续字段均为可选；当前保存格式不再写入这些应用字段。
[[nodiscard]] QByteArray legacySessionFixture(const std::vector<int> &languages) {
	auto session = Main::SessionSettings();
	auto app = Core::Settings();
	auto result = QByteArray();
	auto stream = QDataStream(&result, QIODevice::WriteOnly);
	stream.setVersion(QDataStream::Qt_5_1);
	stream
		<< qint32(-1) << qint32(1) << qint32(session.selectorTab())
		<< qint32(1) << qint32(1) << qint32(0) << qint32(0)
		<< qint32(app.floatPlayerColumn()) << qint32(app.floatPlayerCorner())
		<< qint32(0) << qint32(0) << qint32(0)
		<< qint32(400000) << qint32(444) << qint32(31)
		<< qint32(app.sendFilesWay().serialize()) << qint32(0)
		<< qint32(app.sendSubmitWay()) << qint32(session.supportSwitch())
		<< qint32(1) << qint32(0) << qint32(777)
		<< qint32(1) << qint32(0) << qint32(1)
		<< session.autoDownload().serialize()
		<< qint32(1) << qint32(1) << qint32(0) << qint32(1) << qint32(0)
		<< qint32(1) << qint32(1) << qint32(1)
		<< qint32(1) << qint32(1) << qint32(1)
		<< qint32(0) << qint32(0)
		<< qint32(app.videoPlaybackSpeedSerialized()) << QByteArray()
		<< qint32(languages.size());
	for (const auto id : languages) {
		stream << qint64(id);
	}
	stream << qint32(0);
	return result;
}

[[nodiscard]] Json describeSession(
		const Main::SessionSettings &session,
		const Core::Settings &app) {
	return {
		{ "dictionaries", app.dictionariesEnabled() },
		{ "spellcheckerEnabled", app.spellcheckerEnabled() },
		{ "autoDownloadDictionaries", app.autoDownloadDictionaries() },
		{ "lastSeenWarningSeen", app.lastSeenWarningSeen() },
		{ "thirdColumnWidth", app.thirdColumnWidth() },
		{ "supportChatsTimeSlice", session.supportChatsTimeSlice() },
		{ "supportFixChatsOrder", session.supportFixChatsOrder() },
		{ "supportTemplatesAutocomplete", session.supportTemplatesAutocomplete() },
		{ "supportAllSearchResults", session.supportAllSearchResults() },
		{ "archiveCollapsed", session.archiveCollapsed() },
		{ "archiveInMainMenu", session.archiveInMainMenu() },
	};
}

void verifySessionCase(
		Json &cases,
		const char *name,
		const QByteArray &serialized,
		int writerVersion,
		const std::vector<int> &dictionaries) {
	auto session = Main::SessionSettings();
	auto app = Core::Settings();
	session.addFromSerialized(serialized, writerVersion, app);
	const auto actual = describeSession(session, app);
	const auto expected = Json{
		{ "dictionaries", dictionaries },
		{ "spellcheckerEnabled", false },
		{ "autoDownloadDictionaries", false },
		{ "lastSeenWarningSeen", true },
		{ "thirdColumnWidth", 444 },
		{ "supportChatsTimeSlice", 777 },
		{ "supportFixChatsOrder", true },
		{ "supportTemplatesAutocomplete", false },
		{ "supportAllSearchResults", true },
		{ "archiveCollapsed", true },
		{ "archiveInMainMenu", true },
	};
	auto row = Json{
		{ "name", name },
		{ "writerVersion", writerVersion },
		{ "actual", actual },
		{ "expected", expected },
		{ "passed", false },
	};
	if (actual != expected) {
		cases.push_back(std::move(row));
		return;
	}
	auto reloadedApp = Core::Settings();
	const auto appParsed = reloadedApp.addFromSerialized(
		app.serialize(),
		kAmbiguousVersion);
	const auto appBefore = reloadedApp.serialize();
	auto reloadedSession = Main::SessionSettings();
	reloadedSession.addFromSerialized(
		session.serialize(),
		kAmbiguousVersion,
		reloadedApp);
	const auto roundTrip = describeSession(reloadedSession, reloadedApp);
	const auto appUnchanged = reloadedApp.serialize() == appBefore;
	row["roundTrip"] = roundTrip;
	row["roundTripAppParsed"] = appParsed;
	row["currentSessionPreservesApp"] = appUnchanged;
	row["passed"] = appParsed && appUnchanged && roundTrip == expected;
	cases.push_back(std::move(row));
}

void verifyRejectedSessionCase(Json &cases, const QByteArray &serialized) {
	auto session = Main::SessionSettings();
	auto app = Core::Settings();
	const auto sessionBefore = session.serialize();
	const auto appBefore = app.serialize();
	const auto expected = describeSession(session, app);
	session.addFromSerialized(serialized, kOldVersion, app);
	const auto unchanged = session.serialize() == sessionBefore
		&& app.serialize() == appBefore;
	cases.push_back(Json{
		{ "name", "truncated-legacy-session" },
		{ "actual", describeSession(session, app) },
		{ "expected", expected },
		{ "unchanged", unchanged },
		{ "passed", unchanged },
	});
}

} // namespace

Result verifySettingsSerialization(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: settings.verify-serialization"_q);
	}
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto modern = std::vector<int>{
		int(QLocale::English),
		int(QLocale::German),
	};
#if defined Q_OS_MAC || defined Q_PROCESSOR_ARM
	const auto legacy = modern;
#else
	const auto legacy = std::vector<int>{ 31, 42 };
#endif
	const auto oldFixture = makeFixture(legacy);
	const auto modernFixture = makeFixture(modern);
	const auto markedFixture = makeFixture({ 31, 42 }, 5);
	if (!oldFixture || !modernFixture || !markedFixture) {
		return Result::Err(u"serialized settings layout no longer matches the fixtures"_q);
	}
	const auto normal = Languages{ modern, modern, modern.front() };
	auto cases = Json::array();
	verifyCase(cases, "legacy-version", oldFixture->body, kOldVersion, normal);
	verifyCase(cases, "ambiguous-version", modernFixture->body,
		kAmbiguousVersion, normal);
	verifyCase(cases, "stored-dictionary-scheme",
		markedFixture->body + modernFixture->tail, kAmbiguousVersion, normal);
	const auto preferred = Languages{
		modern,
		{ int(QLocale::German), int(QLocale::English) },
		int(QLocale::German),
	};
	const auto preferredTail = languageTail(
		{ u"de"_q, u"en"_q, u"de"_q, u"unknown-language"_q },
		u"de"_q);
	verifyCase(cases, "stable-codes-priority",
		oldFixture->body + preferredTail, kOldVersion, preferred);
	verifyCase(cases, "unknown-tail-format", oldFixture->body
		+ languageTail({ u"de"_q }, u"de"_q, 2), kOldVersion, normal);
	verifyCase(cases, "negative-tail-count", oldFixture->body
		+ QByteArray::fromHex("00000001ffffffff"), kOldVersion, normal);
	verifyCase(cases, "excessive-tail-count", oldFixture->body
		+ QByteArray::fromHex("0000000100002710"), kOldVersion, normal);
	verifyCase(cases, "truncated-tail-header", oldFixture->body
		+ preferredTail.left(3), kOldVersion, normal);
	verifyCase(cases, "truncated-tail-language", oldFixture->body
		+ preferredTail.left(9), kOldVersion, normal);
	verifyCase(cases, "truncated-tail-target", oldFixture->body
		+ preferredTail.chopped(1), kOldVersion, normal);
	verifyRejectedCase(cases, "empty-body", QByteArray());
	verifyRejectedCase(cases, "truncated-body", modernFixture->body.left(9));
	const auto oldSession = legacySessionFixture(legacy);
	verifySessionCase(cases, "legacy-session-version", oldSession,
		kOldVersion, modern);
	verifySessionCase(cases, "ambiguous-session-version",
		legacySessionFixture(modern), kAmbiguousVersion, modern);
	verifyRejectedSessionCase(cases, oldSession.chopped(1));
	const auto passed = ranges::all_of(cases, [](const Json &row) {
		return row["passed"].get<bool>();
	});
	const auto result = Compact(Json{
		{ "passed", passed },
		{ "localScheme", Spellchecker::LocalIdScheme() },
		{ "cases", std::move(cases) },
	});
	return passed ? Result::Ok(result) : Result::Err(result);
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
