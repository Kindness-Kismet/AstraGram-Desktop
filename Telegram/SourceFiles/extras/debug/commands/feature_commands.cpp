#ifdef _DEBUG
#include "extras/debug/commands/settings_registry.h"

#include "extras/extras_settings.h"
#include "extras/debug/debug_login.h"
#include "extras/features/auto_space/auto_space.h"
#include "extras/features/emoji_packs/emoji_packs.h"
#include "extras/features/translator/extras_translator.h"
#include "extras/features/window_material/window_material.h"
#include "extras/features/forward/extras_forward.h"
#include "extras/utils/telegram_helpers.h"
#include "core/application.h"
#include "main/main_session.h"
#include "ui/emoji_config.h"
#include "ui/text/text_entity.h"
#include "window/window_controller.h"

#include <QFileInfo>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;
std::map<uint64, Json> Jobs;
uint64 NextJob = 0;

} // namespace

uint64 beginJob(const char *kind) {
	const auto id = ++NextJob;
	Jobs[id] = {{"id", id}, {"kind", kind}, {"state", "running"}};
	return id;
}

void finishJob(uint64 id, bool ok, Json result) {
	const auto i = Jobs.find(id);
	if (i == Jobs.end() || i->second["state"] != "running") return;
	i->second["state"] = ok ? "succeeded" : "failed";
	i->second[ok ? "result" : "error"] = std::move(result);
}

Result jobStarted(uint64 id) {
	return Result::Ok(Compact(Json{{"jobId", id}}));
}

namespace {

Result jobStatus(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: job.status <id>"_q);
	auto ok = false;
	const auto id = args[0].toULongLong(&ok);
	const auto i = Jobs.find(id);
	return ok && i != Jobs.end() ? Result::Ok(Compact(i->second)) : Result::Err(u"job not found"_q);
}

Result forgetJob(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: job.forget <id>"_q);
	auto ok = false;
	const auto id = args[0].toULongLong(&ok);
	const auto i = Jobs.find(id);
	if (!ok || i == Jobs.end()) return Result::Err(u"job not found"_q);
	if (i->second["state"] == "running") return Result::Err(u"job is still running"_q);
	Jobs.erase(i);
	return Result::Ok();
}

Result processText(const QStringList &args) {
	if (args.size() != 2 && args.size() != 3) return Result::Err(u"usage: text.process <send|edit|receive|auto-space|zalgo> <text> [entitiesJson]"_q);
	const auto mode = args[0];
	if (mode != u"send"_q && mode != u"edit"_q && mode != u"receive"_q
		&& mode != u"auto-space"_q && mode != u"zalgo"_q) return Result::Err(u"unknown text processing mode"_q);
	TextWithEntities text{args[1]};
	if (args.size() == 3) {
		const auto entities = Json::parse(args[2].toStdString(), nullptr, false);
		if (!entities.is_array()) return Result::Err(u"expected an entity array"_q);
		for (const auto &entity : entities) {
			int offset = 0;
			int length = 0;
			int type = 0;
			if (const auto error = readSetting(entity.at("offset"), offset); !error.isEmpty()) return Result::Err(error);
			if (const auto error = readSetting(entity.at("length"), length); !error.isEmpty()) return Result::Err(error);
			if (const auto error = readSetting(entity.at("type"), type); !error.isEmpty()) return Result::Err(error);
			if (offset < 0 || length < 0 || offset > text.text.size()
				|| length > text.text.size() - offset || type < 0 || type > int(EntityType::CustomEmoji)) {
				return Result::Err(u"invalid entity type or UTF-16 range"_q);
			}
			text.entities.push_back(EntityInText(EntityType(type), offset, length,
				QString::fromStdString(entity.value("data", std::string()))));
		}
	}
	const auto &settings = ExtrasSettings::getInstance();
	if (mode == u"zalgo"_q || (mode == u"receive"_q && settings.filterZalgo())) {
		text.text = filterZalgo(text.text);
	}
	if (mode == u"auto-space"_q || (mode == u"send"_q && settings.autoSpaceSending())
		|| (mode == u"edit"_q && settings.autoSpaceEditing())
		|| (mode == u"receive"_q && settings.autoSpaceReceiving())) {
		Extras::AutoSpace::processText(text);
	}
	auto entities = Json::array();
	for (const auto &entity : text.entities) {
		entities.push_back({{"type", int(entity.type())}, {"offset", entity.offset()},
			{"length", entity.length()}, {"data", entity.data().toStdString()}});
	}
	return Result::Ok(Compact(Json{{"text", text.text.toStdString()}, {"entities", entities}}));
}

Result translateText(const QStringList &args) {
	if (args.size() != 2) return Result::Err(u"usage: translate.start <language> <text>"_q);
	const auto session = ActiveSession();
	const auto manager = Extras::Translator::TranslateManager::currentInstance();
	if (!session || !manager) return Result::Err(u"translation requires an active session"_q);
	const auto provider = ExtrasSettings::getInstance().translationProvider();
	if (isSimulationSession(session) && provider == TranslationProvider::Telegram) {
		return Result::Err(u"Telegram translation requires an authenticated session"_q);
	}
	const auto id = beginJob("translation");
	session->lifetime().add([id] { finishJob(id, false, "session closed"); });
	manager->request(session, MTP_flags(MTPmessages_TranslateText::Flag::f_text),
		MTP_inputPeerEmpty(), MTPVector<MTPint>(),
		MTP_vector<MTPTextWithEntities>({MTP_textWithEntities(MTP_string(args[1]), MTPVector<MTPMessageEntity>())}),
		MTP_string(args[0]), provider
	).done([id](const MTPmessages_TranslatedText &result) {
		auto texts = Json::array();
		for (const auto &text : result.data().vresult().v) texts.push_back(qs(text.data().vtext()).toStdString());
		finishJob(id, true, texts);
	}).fail([id](const MTP::Error &error) {
		finishJob(id, false, error.type().toStdString());
	}).send();
	return jobStarted(id);
}

Result clearTranslationCache(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: translate.clear-cache"_q);
	const auto manager = Extras::Translator::TranslateManager::currentInstance();
	if (!manager) return Result::Err(u"translation manager is unavailable"_q);
	manager->resetCache();
	return Result::Ok();
}

Result emojiList(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: emoji.list"_q);
	auto installed = Json::array();
	auto presets = Json::array();
	for (const auto &pack : Extras::EmojiPacks::installed()) {
		installed.push_back({{"id", pack.id}, {"name", pack.name.toStdString()}});
	}
	for (const auto &preset : Extras::EmojiPacks::presets()) {
		presets.push_back({{"id", preset.id.toStdString()}, {"name", preset.name.toStdString()}, {"bytes", preset.size}});
	}
	return Result::Ok(Compact(Json{{"current", Ui::Emoji::CurrentSetId()}, {"installed", installed}, {"presets", presets}}));
}

void finishEmoji(uint64 id, Extras::EmojiPacks::ImportResult result) {
	if (result.error != Extras::EmojiPacks::ImportError::None) {
		finishJob(id, false, Json{{"importError", int(result.error)}});
		return;
	}
	finishJob(id, true, Json{{"id", result.pack.id}, {"name", result.pack.name.toStdString()}});
}

Result importEmoji(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: emoji.import <fontPath>"_q);
	if (!QFileInfo(args[0]).isFile()) return Result::Err(u"font file not found"_q);
	const auto id = beginJob("emoji-import");
	Extras::EmojiPacks::importFont(args[0], [id](auto result) { finishEmoji(id, result); });
	return jobStarted(id);
}

Result installEmoji(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: emoji.install <presetId>"_q);
	for (const auto &preset : Extras::EmojiPacks::presets()) {
		if (preset.id != args[0]) continue;
		const auto id = beginJob("emoji-install");
		Extras::EmojiPacks::installPreset(preset, [id](auto progress) {
			Jobs[id]["downloaded"] = progress.already;
			Jobs[id]["total"] = progress.total;
		}, [id](auto result) { finishEmoji(id, result); });
		return jobStarted(id);
	}
	return Result::Err(u"preset not found"_q);
}

Result selectEmoji(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: emoji.select <id>"_q);
	auto ok = false;
	const auto set = args[0].toInt(&ok);
	if (!ok || set < 0 || set > std::numeric_limits<uchar>::max()) return Result::Err(u"emoji set id out of range"_q);
	if (!Ui::Emoji::SetIsReady(set)) return Result::Err(u"emoji set is not installed"_q);
	const auto id = beginJob("emoji-select");
	Ui::Emoji::SwitchToSet(set, [id, set](bool success) {
		finishJob(id, success, success ? Json(set) : Json("failed to switch emoji set"));
	});
	return jobStarted(id);
}

Result cancelEmoji(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: emoji.cancel <presetId>"_q);
	Extras::EmojiPacks::cancelPreset(args[0]);
	return Result::Ok();
}

Result featureStatus(const QStringList &args) {
	if (!args.empty()) return Result::Err(u"usage: feature.status"_q);
	const auto window = Core::App().activeWindow();
	auto modes = Json::array();
	for (const auto mode : ExtrasFeatures::WindowMaterial::availableModes()) modes.push_back(int(mode));
	return Result::Ok(Compact(Json{
		{"streamerMode", ExtrasSettings::getInstance().streamerMode()},
		{"windowMaterialActive", window && ExtrasFeatures::WindowMaterial::isActive(window->widget())},
		{"windowMaterialModes", modes}, {"emojiSet", Ui::Emoji::CurrentSetId()},
		{"translationProvider", ExtrasSettings::getInstance().translationProvider()},
	}));
}

Result forwardStatus(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: forward.status <peerId>"_q);
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	const auto active = ExtrasForward::isForwarding(peer->id);
	const auto state = active ? ExtrasForward::stateName(peer->id) : std::pair<QString, QString>();
	return Result::Ok(Compact(Json{{"active", active}, {"state", state.first.toStdString()}, {"progress", state.second.toStdString()}}));
}

Result cancelForward(const QStringList &args) {
	if (args.size() != 1) return Result::Err(u"usage: forward.cancel <peerId>"_q);
	const auto peer = findPeer(args[0]);
	if (!peer) return Result::Err(u"peer not found"_q);
	if (!ExtrasForward::isForwarding(peer->id)) return Result::Err(u"no active forwarding task"_q);
	ExtrasForward::cancelForward(peer->id, peer->session());
	return Result::Ok();
}

} // namespace

const HandlerMap &FeatureHandlers() {
	static const auto result = HandlerMap{
		{u"job.status"_q, &jobStatus}, {u"job.forget"_q, &forgetJob},
		{u"text.process"_q, &processText}, {u"translate.start"_q, &translateText},
		{u"translate.clear-cache"_q, &clearTranslationCache},
		{u"emoji.list"_q, &emojiList}, {u"emoji.import"_q, &importEmoji},
		{u"emoji.install"_q, &installEmoji}, {u"emoji.select"_q, &selectEmoji},
		{u"emoji.cancel"_q, &cancelEmoji},
		{u"feature.status"_q, &featureStatus}, {u"forward.status"_q, &forwardStatus},
		{u"forward.cancel"_q, &cancelForward},
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif
