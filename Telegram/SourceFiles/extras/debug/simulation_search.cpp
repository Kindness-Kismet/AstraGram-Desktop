#ifdef _DEBUG
#include "extras/debug/simulation_search.h"

#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "extras/debug/simulation_media.h"
#include "api/api_chat_participants.h"
#include "apiwrap.h"
#include "base/algorithm.h"
#include "base/unixtime.h"
#include "core/file_location.h"
#include "data/components/recent_peers.h"
#include "data/components/top_peers.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "inline_bots/bot_attach_web_view.h"
#include "main/main_session.h"
#include "settings.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtGui/QImage>

namespace ExtrasDebug::Commands {
namespace {

using Type = Storage::SharedMediaType;
base::weak_ptr<Main::Session> SearchSession;
base::flat_map<Type, std::vector<FullMsgId>> SearchMedia;

MTPMessageMedia localMedia(not_null<Main::Session*> session,
		Type type, int id, const QDir &directory, int index) {
	if (type == Type::Photo || type == Type::File) {
		return simulationMedia(session,
			type == Type::Photo ? SimulationMedia::Photo : SimulationMedia::File, id);
	}
	const auto video = type == Type::Video;
	const auto voice = type == Type::RoundVoiceFile;
	const auto file = QFileInfo(directory.filePath(video
		? u"search-video.mp4"_q : voice ? u"search-voice.ogg"_q : u"search-music.mp3"_q));
	auto attributes = QVector<MTPDocumentAttribute>{
		MTP_documentAttributeFilename(MTP_string(file.fileName())),
	};
	if (video) {
		attributes.push_back(MTP_documentAttributeVideo(
			MTP_flags(MTPDdocumentAttributeVideo::Flag::f_supports_streaming),
			MTP_double(3.), MTP_int(480), MTP_int(300), MTPint(), MTPdouble(), MTPstring()));
	} else {
		using Flag = MTPDdocumentAttributeAudio::Flag;
		attributes.push_back(MTP_documentAttributeAudio(
			MTP_flags(voice ? Flag::f_voice : Flag::f_title | Flag::f_performer),
			MTP_int(3), MTP_string(u"搜索样本 %1"_q.arg(index + 1)),
			MTP_string("本地演示"), MTPstring()));
	}
	const auto document = MTP_document(MTP_flags(0), MTP_long(9600000000LL + id),
		MTPlong(), MTP_bytes(), MTP_int(base::unixtime::now()),
		MTP_string(video ? "video/mp4" : voice ? "audio/ogg" : "audio/mpeg"),
		MTP_long(file.size()), MTPVector<MTPPhotoSize>(), MTPVector<MTPVideoSize>(),
		MTP_int(0), MTP_vector<MTPDocumentAttribute>(attributes));
	const auto data = session->data().processDocument(document);
	data->setLocation(Core::FileLocation(file.absoluteFilePath()));
	if (video) {
		auto &view = *session->lifetime().make_state<std::shared_ptr<Data::DocumentMedia>>(
			data->createMediaView());
		auto thumbnail = QImage(480, 300, QImage::Format_RGB32);
		thumbnail.fill(QColor(0x70, 0x8b, 0x9e));
		view->setThumbnail(std::move(thumbnail));
	}
	return MTP_messageMediaDocument(MTP_flags(MTPDmessageMediaDocument::Flag::f_document),
		document, MTPVector<MTPDocument>(), MTPPhoto(), MTPint(), MTPint());
}

}

Result seedSearchPreview(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: simulation.search <fixture-directory>"_q);
	}
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto directory = QDir(args.front());
	for (const auto name : { u"search-video.mp4", u"search-voice.ogg", u"search-music.mp3" }) {
		if (!QFileInfo(directory.filePath(QString::fromUtf16(name))).isFile()) {
			return Result::Err(u"missing search media fixture"_q);
		}
	}
	if (SearchSession.get() == session) {
		return Result::Ok(u"search preview is already seeded"_q);
	}
	SearchSession = base::make_weak(session);
	SearchMedia.clear();
	const auto peer = [&](const QString &key) -> not_null<PeerData*> {
		return simulationPeer(session, key);
	};
	session->topPeers().setForDebug({ peer(u"private"_q), peer(u"draft"_q),
		peer(u"contact"_q), peer(u"business"_q), peer(u"paid"_q), peer(u"sending"_q) });
	for (const auto key : { u"private", u"group", u"channel", u"saved" }) {
		session->recentPeers().bump(peer(QString::fromUtf16(key)));
	}
	session->api().chatParticipants().setRecommendationsForDebug({
		peer(u"join"_q), peer(u"translate"_q), peer(u"protected-channel"_q) });
	auto recentApps = std::vector<not_null<PeerData*>>();
	auto popularApps = std::vector<not_null<UserData*>>();
	for (auto i = 0; i != 6; ++i) {
		const auto bot = session->data().user(UserId(820000100 + i));
		bot->setName(u"本地应用 %1"_q.arg(i + 1), {}, {}, {});
		bot->setAccessHash(0);
		bot->setFlags(UserDataFlag::MessageMoneyRestrictionsKnown);
		bot->setLoadedStatus(PeerData::LoadedStatus::Full);
		bot->setBotInfoVersion(1);
		bot->botInfo->inited = true;
		bot->botInfo->hasMainApp = true;
		bot->botInfo->activeUsers = 1200 + i * 370;
		if (i < 3) {
			recentApps.push_back(bot);
		} else {
			popularApps.push_back(bot);
		}
	}
	session->topBotApps().setForDebug(recentApps);
	session->attachWebView().setPopularAppBotsForDebug(std::move(popularApps));
	const auto history = session->data().history(session->userPeerId());
	auto id = 2900000;
	for (const auto type : { Type::Photo, Type::Video, Type::RoundVoiceFile,
			Type::MusicFile, Type::File, Type::Link }) {
		for (auto i = 0; i != 3; ++i) {
			const auto text = type == Type::Link
				? u"设计参考 %1 https://example.com/layout/%1"_q.arg(i + 1)
				: u"搜索预览样本 %1"_q.arg(i + 1);
			const auto message = type == Type::Link
				? simulationTextMessage(history->peer, session->userPeerId(), id, text)
				: simulationMediaMessage(history->peer, session->userPeerId(), id, text,
					localMedia(session, type, id, directory, i));
			const auto item = session->data().addNewMessage(
				message, MessageFlags(), NewMessageType::Existing);
			Expects(item != nullptr);
			SearchMedia[type].push_back(item->fullId());
			++id;
		}
	}
	return Result::Ok(Compact(nlohmann::json{
		{ "contacts", 6 }, { "recentChats", 4 }, { "recommendations", 3 },
		{ "recentApps", 3 }, { "popularApps", 3 }, { "mediaTabs", 6 }, { "itemsPerMediaTab", 3 },
	}));
}

std::optional<Api::GlobalMediaResult> searchPreviewMedia(
		not_null<Main::Session*> session, Type type, const QString &query) {
	if (SearchSession.get() != session.get()) {
		return std::nullopt;
	}
	auto result = Api::GlobalMediaResult();
	for (const auto id : SearchMedia[type]) {
		const auto item = session->data().message(id);
		if (item && (query.isEmpty() || item->originalText().text.contains(query, Qt::CaseInsensitive))) {
			result.messageIds.push_back(item->position());
		}
	}
	ranges::sort(result.messageIds, std::greater<>());
	result.fullCount = int(result.messageIds.size());
	if (!result.messageIds.empty()) {
		result.offsetPosition = result.messageIds.back();
	}
	return result;
}

}
#endif
