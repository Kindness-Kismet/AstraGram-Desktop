#ifdef _DEBUG
#include "extras/debug/simulation_profile.h"

#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "extras/debug/simulation_media.h"
#include "base/unixtime.h"
#include "data/data_changes.h"
#include "data/data_channel.h"
#include "data/data_community.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_saved_music.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "info/info_memento.h"
#include "info/stories/info_stories_widget.h"
#include "main/main_session.h"
#include "settings.h"
#include "window/window_session_controller.h"

#include <QtGui/QPainter>

namespace ExtrasDebug::Commands {
namespace {

constexpr auto kUserId = UserId(870000101);
constexpr auto kChannelId = ChannelId(870000102);
constexpr auto kCommunityId = ChannelId(870000103);
constexpr auto kGroupId = ChannelId(870000104);
constexpr auto kPostId = 2700100;

QImage coverImage() {
	auto image = QImage(160, 160, QImage::Format_RGB32);
	image.fill(QColor(43, 77, 70));
	auto p = QPainter(&image);
	p.setRenderHint(QPainter::Antialiasing);
	p.setPen(Qt::NoPen);
	p.setBrush(QColor(115, 149, 119));
	p.drawEllipse(QRect(-65, 83, 210, 170));
	p.setBrush(QColor(229, 204, 157));
	p.drawEllipse(QRect(79, 25, 54, 54));
	return image;
}

std::vector<not_null<DocumentData*>> songs(
		not_null<Main::Session*> session, bool longTitle) {
	auto result = std::vector<not_null<DocumentData*>>();
	for (auto i = 0; i != 3; ++i) {
		const auto titles = std::array{
			u"十年"_q, u"好久不见"_q, u"陪你度过漫长岁月"_q,
		};
		const auto title = longTitle && i == 0
			? u"给每一个在深夜独自赶路的人，写一首还没有名字的歌"_q
			: titles[i];
		const auto artist = longTitle
			? u"林间来信与山海之间乐队 · 现场特别录制"_q : u"陈奕迅"_q;
		const auto document = MTP_document(
			MTP_flags(0), MTP_long(9700000100LL + i + (longTitle ? 10 : 0)),
			MTPlong(), MTP_bytes(), MTP_int(base::unixtime::now()),
			MTP_string("audio/mpeg"), MTP_long(1000000),
			MTPVector<MTPPhotoSize>(), MTPVector<MTPVideoSize>(), MTP_int(0),
			MTP_vector<MTPDocumentAttribute>({
				MTP_documentAttributeFilename(MTP_string(title + u".mp3"_q)),
				MTP_documentAttributeAudio(
					MTP_flags(MTPDdocumentAttributeAudio::Flag::f_title
						| MTPDdocumentAttributeAudio::Flag::f_performer),
					MTP_int(205 + i * 20), MTP_string(title), MTP_string(artist), MTPstring()),
			}));
		const auto data = session->data().processDocument(document);
		if (!longTitle) {
			auto &media = *session->lifetime().make_state<std::shared_ptr<Data::DocumentMedia>>(
				data->createMediaView());
			media->setThumbnail(coverImage());
		}
		result.push_back(data);
	}
	return result;
}

void prepareChannel(not_null<ChannelData*> channel, bool group, const QString &name) {
	channel->setName(name, {});
	channel->setFlags(group ? ChannelDataFlag::Megagroup : ChannelDataFlag::Broadcast);
	channel->setLoadedStatus(PeerData::LoadedStatus::Full);
	channel->setMembersCount(group ? 186 : 1284);
	channel->setAbout(group
		? u"一起分享周末路线、沿途照片，以及下一次出发的计划。"_q
		: u"用影像记录四季，用文字保存旅途。"_q);
	channel->fullUpdated();
}

} // namespace

Result seedProfilePreview(const QStringList &args) {
	const auto scenarios = QStringList{
		u"music"_q, u"combined"_q, u"channel-only"_q, u"long"_q,
		u"warning"_q, u"community"_q, u"hidden"_q, u"empty"_q,
	};
	if (args.empty() || args.size() > 2 || !scenarios.contains(args.front())
		|| (args.size() == 2 && args[1] != u"--side"_q
			&& args[1] != u"--stories"_q && args[1] != u"--keep"_q)) {
		return Result::Err(u"usage: simulation.profile <music|combined|channel-only|long|warning|community|hidden|empty> [--side|--stories|--keep]"_q);
	}
	const auto session = ActiveSession();
	const auto window = session ? session->tryResolveWindow() : nullptr;
	if (!cDebugProfile() || !window || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile with a window is required"_q);
	}
	const auto scene = args.front();
	const auto stories = args.contains(u"--stories"_q);
	const auto user = stories ? session->user() : session->data().user(kUserId);
	const auto channel = session->data().channel(kChannelId);
	const auto community = session->data().channel(kCommunityId);
	const auto group = session->data().channel(kGroupId);
	prepareChannel(channel, false, u"山海之间"_q);
	prepareChannel(group, true, u"周末山行"_q);
	prepareChannel(community, true, u"山海创作社区"_q);
	community->setFlags(ChannelDataFlag::Community | ChannelDataFlag::Megagroup);
	const auto inCommunity = scene == u"community"_q || scene == u"hidden"_q;
	channel->setLinkedCommunityId(inCommunity ? kCommunityId : ChannelId());
	group->setLinkedCommunityId(inCommunity ? kCommunityId : ChannelId());
	auto links = QVector<MTPCommunityPeer>();
	for (auto i = 0; i != 8; ++i) {
		const auto linked = i == 0 ? channel : i == 1 ? group
			: session->data().channel(ChannelId(870000110 + i));
		if (i > 1) {
			prepareChannel(linked, true, u"山海交流 %1"_q.arg(i));
		}
		links.push_back(MTP_communityPeer(
			MTP_flags(MTPDcommunityPeer::Flag::f_visible | MTPDcommunityPeer::Flag::f_can_view_history),
			MTP_bool(!(i == 1 && scene == u"hidden"_q)), peerToMTP(linked->id)));
	}
	community->ensuredCommunityInfo()->applyLinkedPeers(links);
	user->setName(u"林间来信"_q, {}, {}, u"forestletter"_q);
	user->setAccessHash(0);
	user->setLoadedStatus(PeerData::LoadedStatus::Full);
	user->setFlags(UserDataFlag::Contact | UserDataFlag::HasPhoneCalls
		| UserDataFlag::MessageMoneyRestrictionsKnown | UserDataFlag::Verified
		| (stories ? UserDataFlag::Self : UserDataFlag())
		| (scene == u"warning"_q ? UserDataFlag::UnofficialSecurityRisk : UserDataFlag()));
	user->setAbout(u"记录日常，也收藏一些值得反复听的声音。"_q);
	const auto hasMusic = scene == u"music"_q || scene == u"combined"_q
		|| scene == u"long"_q || scene == u"warning"_q;
	const auto hasChannel = scene == u"combined"_q || scene == u"channel-only"_q
		|| scene == u"warning"_q;
	const auto post = hasChannel && scene != u"channel-only"_q;
	if (post && !session->data().message(FullMsgId(channel->id, kPostId))) {
		const auto message = simulationMediaMessage(channel, channel->id, kPostId,
			u"周末去山里，收集一点不一样的风景。"_q,
			simulationMedia(session, SimulationMedia::Photo, kPostId));
		const auto history = session->data().history(channel);
		history->clearFolder();
		history->addOlderSlice({});
		history->addNewerSlice({ message });
		history->addNewerSlice({});
		history->setUnreadCount(0);
	}
	user->setPersonalChannel(hasChannel ? kChannelId : ChannelId(), post ? MsgId(kPostId) : MsgId());
	session->data().savedMusic().setForDebug(user->id, hasMusic
		? songs(session, scene == u"long"_q) : std::vector<not_null<DocumentData*>>());
	user->fullUpdated();
	channel->fullUpdated();
	group->fullUpdated();
	community->fullUpdated();
	for (const auto updated : { static_cast<PeerData*>(user.get()),
			static_cast<PeerData*>(channel.get()), static_cast<PeerData*>(group.get()),
			static_cast<PeerData*>(community.get()) }) {
		session->changes().peerUpdated(updated, Data::PeerUpdate::Flag::FullInfo);
	}
	const auto peer = scene == u"community"_q ? static_cast<PeerData*>(channel.get())
		: scene == u"hidden"_q ? static_cast<PeerData*>(group.get()) : static_cast<PeerData*>(user.get());
	if (!args.contains(u"--keep"_q)) {
		auto memento = std::make_shared<Info::Memento>(peer);
		if (stories) {
			auto content = std::make_shared<Info::Stories::Memento>(peer, 0, 0);
			content->setMyProfile(true);
			memento = std::make_shared<Info::Memento>(
				std::vector<std::shared_ptr<Info::ContentMemento>>{ content });
		}
		const auto params = Window::SectionShow(
			Window::SectionShow::Way::Forward, anim::type::instant);
		window->showSection(memento, args.contains(u"--side"_q)
			? params.withThirdColumn() : params);
	}
	return Result::Ok(Compact(nlohmann::json{
		{ "scene", scene.toStdString() }, { "peerId", peer->id.value },
		{ "musicCount", session->data().savedMusic().count(user->id) },
		{ "personalChannelId", hasChannel ? channel->id.value : 0 },
		{ "postId", post ? kPostId : 0 }, { "warning", user->unofficialSecurityRisk() },
		{ "communityId", inCommunity ? community->id.value : 0 },
		{ "hidden", community->communityInfo()->isHidden(peer) },
		{ "stories", stories }, { "side", args.contains(u"--side"_q) },
	}));
}

} // namespace ExtrasDebug::Commands
#endif
