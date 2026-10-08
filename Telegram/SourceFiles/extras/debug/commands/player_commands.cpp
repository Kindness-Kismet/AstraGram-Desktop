#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"

namespace ExtrasDebug::Commands {
namespace {

Result controlPlayer(const QStringList &args) {
	if (args.isEmpty() || args.size() > 2) {
		return Result::Err(u"usage: player.control <play|pause|toggle|stop> [song|voice]"_q);
	}
	if (args.size() == 2 && args[1] != u"song"_q && args[1] != u"voice"_q) {
		return Result::Err(u"unknown player type"_q);
	}
	const auto player = Media::Player::instance();
	const auto type = args.size() == 1 ? player->getActiveType()
		: args[1] == u"song"_q ? AudioMsgId::Type::Song : AudioMsgId::Type::Voice;
	if (!player->current(type).audio()) return Result::Err(u"no current media"_q);
	if (args[0] == u"play"_q) player->play(type);
	else if (args[0] == u"pause"_q) player->pause(type);
	else if (args[0] == u"toggle"_q) player->playPause(type);
	else if (args[0] == u"stop"_q) player->stop(type);
	else return Result::Err(u"unknown player action"_q);
	return Result::Ok();
}

Result playerState(const QStringList &args) {
	if (args.size() > 1 || (!args.empty() && args[0] != u"song"_q && args[0] != u"voice"_q)) {
		return Result::Err(u"usage: player.state [song|voice]"_q);
	}
	const auto player = Media::Player::instance();
	const auto type = args.empty() ? player->getActiveType()
		: args[0] == u"song"_q ? AudioMsgId::Type::Song : AudioMsgId::Type::Voice;
	const auto state = player->getState(type);
	const auto current = player->current(type);
	const auto document = current.audio();
	return Result::Ok(Compact(nlohmann::json{
		{ "type", type == AudioMsgId::Type::Song ? "song" : "voice" },
		{ "video", document && document->isVideoMessage() },
		{ "hasTrack", document != nullptr },
		{ "messageId", current.contextId().msg.bare },
		{ "state", int(state.state) },
		{ "playing", Media::Player::ShowPauseIcon(state.state) },
		{ "position", state.position },
		{ "length", state.length },
		{ "frequency", state.frequency },
		{ "repeat", int(Core::App().settings().playerRepeatMode()) },
		{ "filename", document ? document->filename().toStdString() : "" },
	}));
}

} // namespace

const HandlerMap &playerHandlers() {
	static const auto handlers = HandlerMap{
		{ u"player.control"_q, &controlPlayer },
		{ u"player.state"_q, &playerState },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
