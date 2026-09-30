#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "base/unixtime.h"
#include "ui/text/text_entity.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace ExtrasDebug::Commands {
namespace {

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

Result fakePlayer(const QStringList &args) {
	if (args.size() != 3 || (args[0] != u"song"_q
		&& args[0] != u"voice"_q && args[0] != u"video"_q)) {
		return Result::Err(u"usage: player.fake <song|voice|video> <path> <seconds>"_q);
	}
	const auto session = ActiveSession();
	if (!session || !isFakeSession(session)) {
		return Result::Err(u"an in-process fake session is required"_q);
	}
	const auto duration = args[2].toInt();
	if (duration <= 0 || duration > 600) {
		return Result::Err(u"duration must be between 1 and 600 seconds"_q);
	}
	auto file = QFile(args[1]);
	if (!file.open(QIODevice::ReadOnly) || file.size() <= 0 || file.size() > 16 * 1024 * 1024) {
		return Result::Err(u"expected a readable media file up to 16 MiB"_q);
	}
	const auto bytes = file.readAll();
	if (bytes.size() != file.size()) {
		return Result::Err(u"could not read the complete media file"_q);
	}
	const auto video = args[0] == u"video"_q;
	const auto voice = args[0] == u"voice"_q;
	const auto filename = QFileInfo(file).fileName();
	auto attributes = QVector<MTPDocumentAttribute>{
		MTP_documentAttributeFilename(MTP_string(filename)),
	};
	if (video) {
		attributes.push_back(MTP_documentAttributeVideo(
			MTP_flags(MTPDdocumentAttributeVideo::Flag::f_round_message
				| MTPDdocumentAttributeVideo::Flag::f_supports_streaming),
			MTP_double(duration), MTP_int(320), MTP_int(320),
			MTPint(), MTPdouble(), MTPstring()));
	} else {
		const auto flags = voice ? MTPDdocumentAttributeAudio::Flag::f_voice
			: (MTPDdocumentAttributeAudio::Flag::f_title
				| MTPDdocumentAttributeAudio::Flag::f_performer);
		attributes.push_back(MTP_documentAttributeAudio(MTP_flags(flags),
			MTP_int(duration), MTP_string(filename),
			MTP_string("AstraGram"), MTP_bytes()));
	}
	static auto messageId = int32(1700000000);
	const auto id = ++messageId;
	const auto document = MTP_document(MTP_flags(0),
		MTP_long(9000000000LL + id), MTP_long(0), MTP_bytes(),
		MTP_int(base::unixtime::now()), MTP_string(video ? "video/mp4" : "audio/ogg"),
		MTP_long(bytes.size()), MTP_vector<MTPPhotoSize>(), MTPVector<MTPVideoSize>(),
		MTP_int(0), MTP_vector<MTPDocumentAttribute>(attributes));
	const auto data = session->data().processDocument(document);
	auto mediaView = data->createMediaView();
	mediaView->setBytes(bytes);
	session->data().keepAlive(std::move(mediaView));
	const auto peer = session->userPeerId();
	// 仅向假会话注入内存消息，本地文件通过正式流媒体播放器播放。
	const auto message = MTP_message(
		MTP_flags(MTPDmessage::Flag::f_from_id | MTPDmessage::Flag::f_out
			| MTPDmessage::Flag::f_media),
		MTP_int(id), peerToMTP(peer), MTPint(), MTPstring(), peerToMTP(peer),
		MTPPeer(), MTPMessageFwdHeader(), MTPlong(), MTPlong(), MTPPeer(),
		MTPMessageReplyHeader(), MTP_int(base::unixtime::now()), MTP_string(""),
		MTP_messageMediaDocument(MTP_flags(MTPDmessageMediaDocument::Flag::f_document),
			document, MTPVector<MTPDocument>(), MTPPhoto(), MTPint(), MTPint()),
		MTPReplyMarkup(), MTPVector<MTPMessageEntity>(), MTPint(), MTPint(),
		MTPMessageReplies(), MTPint(), MTPstring(), MTPlong(), MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>(), MTPint(), MTPint(), MTPlong(), MTPFactCheck(),
		MTPint(), MTPlong(), MTPSuggestedPost(), MTPint(), MTPstring(), MTPRichMessage());
	const auto item = session->data().addNewMessage(message, MessageFlags(), NewMessageType::Unread);
	if (!item) {
		return Result::Err(u"could not create local media message"_q);
	}
	Media::Player::instance()->play(AudioMsgId(data, item->fullId()));
	return Result::Ok(Compact(nlohmann::json{
		{ "messageId", item->id.bare }, { "peerId", peer.value },
		{ "kind", args[0].toStdString() }, { "localOnly", true },
	}));
}

} // namespace

const HandlerMap &playerHandlers() {
	static const auto handlers = HandlerMap{
		{ u"player.state"_q, &playerState },
		{ u"player.fake"_q, &fakePlayer },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
