#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"

namespace ExtrasDebug::Commands {
namespace {

Result playerState(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: player.state"_q);
	}
	const auto player = Media::Player::instance();
	const auto state = player->getState(AudioMsgId::Type::Song);
	const auto current = player->current(AudioMsgId::Type::Song);
	const auto document = current.audio();
	return Result::Ok(Compact(nlohmann::json{
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
		{ u"player.state"_q, &playerState },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
