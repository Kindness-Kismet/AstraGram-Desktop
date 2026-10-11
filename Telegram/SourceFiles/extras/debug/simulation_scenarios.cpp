#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/debug/debug_login.h"
#include "extras/debug/simulation_scenarios.h"
#include "extras/debug/simulation_media.h"
#include "extras/extras_settings.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/data_chat_filters.h"
#include "data/business/data_business_info.h"
#include "data/business/data_shortcut_messages.h"
#include "data/components/sponsored_messages.h"
#include "data/components/scheduled_messages.h"
#include "data/data_drafts.h"
#include "data/data_forum.h"
#include "data/data_folder.h"
#include "data/data_forum_topic.h"
#include "data/data_peer_bot_command.h"
#include "data/data_replies_list.h"
#include "data/data_session.h"
#include "data/data_saved_messages.h"
#include "data/data_saved_sublist.h"
#include "lang/lang_keys.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/admin_log/history_admin_log_section.h"
#include "history/view/history_view_chat_section.h"
#include "history/view/history_view_pinned_section.h"
#include "history/view/history_view_scheduled_section.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "main/session/send_as_peers.h"
#include "spellcheck/spellcheck_types.h"
#include "settings/business/settings_shortcut_messages.h"
#include "storage/storage_facade.h"
#include "storage/storage_shared_media.h"
#include "window/window_session_controller.h"

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;
using Category = SimulationCategory;
enum class Kind {
	Private,
	Draft,
	Contact,
	Blocked,
	DeletedAccount,
	Business,
	Paid,
	Group,
	Restricted,
	Requests,
	Slow,
	ProtectedGroup,
	Broadcast,
	Discussion,
	Join,
	Translate,
	ProtectedChannel,
	Bot,
	Keyboard,
	InlineBot,
	Sponsored,
	Topic,
	Saved,
	ArchivedPrivate,
	ArchivedGroup,
	ArchivedChannel,
	SendAs,
	VoiceRestricted,
	TopicTabs,
};
struct Scenario {
	const char *key;
	const char16_t *name;
	Kind kind;
	Category category;
	const char *features;
};
constexpr auto kScenarios = std::array{
	Scenario{ "private", u"私聊 · 消息状态", Kind::Private, Category::Private, "reply,edited,deleted,ttl,protected,read,pinned-dialog" },
	Scenario{ "draft", u"私聊 · 未读与草稿", Kind::Draft, Category::Private, "unread,draft,muted" },
	Scenario{ "contact", u"私聊 · 陌生人", Kind::Contact, Category::Private, "contact-prompt" },
	Scenario{ "blocked", u"私聊 · 已屏蔽", Kind::Blocked, Category::Private, "blocked" },
	Scenario{ "deleted-account", u"私聊 · 已注销", Kind::DeletedAccount, Category::Private, "deleted-account" },
	Scenario{ "business", u"私聊 · 商业联系人", Kind::Business, Category::Private, "business" },
	Scenario{ "paid", u"私聊 · 付费消息", Kind::Paid, Category::Private, "paid" },
	Scenario{ "group", u"群组 · 讨论与置顶", Kind::Group, Category::Groups, "pinned,reply,edited,deleted,ttl,photo,album,file,contact,poll,sticker" },
	Scenario{ "restricted", u"群组 · 发送限制", Kind::Restricted, Category::Groups, "restricted" },
	Scenario{ "requests", u"群组 · 管理与申请", Kind::Requests, Category::Groups, "requests,pinned" },
	Scenario{ "slow", u"群组 · 慢速模式", Kind::Slow, Category::Groups, "slowmode" },
	Scenario{ "protected-group", u"群组 · 内容保护", Kind::ProtectedGroup, Category::Groups, "protected,pinned" },
	Scenario{ "channel", u"频道 · 公告与置顶", Kind::Broadcast, Category::Channels, "pinned,views" },
	Scenario{ "discussion", u"频道 · 评论讨论", Kind::Discussion, Category::Channels, "discussion,pinned" },
	Scenario{ "join", u"频道 · 尚未加入", Kind::Join, Category::Channels, "join,pinned-dialog" },
	Scenario{ "translate", u"频道 · 翻译内容", Kind::Translate, Category::Channels, "translation,pinned" },
	Scenario{ "protected-channel", u"频道 · 内容保护", Kind::ProtectedChannel, Category::Channels, "protected" },
	Scenario{ "bot", u"机器人 · 启动", Kind::Bot, Category::Bots, "bot-start" },
	Scenario{ "keyboard", u"机器人 · 回复键盘", Kind::Keyboard, Category::Bots, "keyboard,commands" },
	Scenario{ "inline-bot", u"机器人 · 内联按钮", Kind::InlineBot, Category::Bots, "inline-keyboard" },
	Scenario{ "sponsored", u"机器人 · 广告", Kind::Sponsored, Category::Bots, "sponsored" },
	Scenario{ "topic", u"话题 · 常规、未读与关闭", Kind::Topic, Category::Topics, "topics,pinned,unread,closed,draft" },
	Scenario{ "saved", u"个人收藏夹", Kind::Saved, Category::Saved, "saved,sources,pinned,reminders,photo,album,file,contact,poll,sticker" },
	Scenario{ "archived-private", u"归档 · 私聊", Kind::ArchivedPrivate, Category::Archive, "archived,unread" },
	Scenario{ "archived-group", u"归档 · 群组", Kind::ArchivedGroup, Category::Archive, "archived,muted" },
	Scenario{ "archived-channel", u"归档 · 频道", Kind::ArchivedChannel, Category::Archive, "archived,pinned" },
	Scenario{ "send-as", u"群组 · 频道身份", Kind::SendAs, Category::Groups, "send-as,identity-switcher" },
	Scenario{ "voice-restricted", u"群组 · 禁止语音视频", Kind::VoiceRestricted, Category::Groups, "restricted-voice,restricted-round" },
	Scenario{ "topic-tabs", u"话题 · 标签布局", Kind::TopicTabs, Category::Topics, "tabs,general,colors,long-title,pinned,unread,closed,translate,requests" },
};
constexpr auto kFirstPeerId = uint64(810000001);
constexpr auto kFirstMessageId = 2000001;
constexpr auto kTopicRootId = 500;
// 标签布局场景的话题，首项为常规话题；view 是可直接打开的视图名。
struct TabTopic {
	const char16_t *title;
	int32 colorId = 0x6FB9F0;
	const char *view = nullptr;
	bool pinned = false;
	bool unread = false;
	bool closed = false;
	bool french = false;
};
constexpr auto kTabTopics = std::array{
	TabTopic{ .title = u"General", .view = "topic-general", .pinned = true },
	TabTopic{ .title = u"公告与置顶", .view = "topic-pinned", .pinned = true },
	TabTopic{ .title = u"设计讨论", .colorId = 0xFFD67E, .view = "topic-unread", .unread = true },
	TabTopic{ .title = u"Release Notes", .colorId = 0xCB86DB },
	TabTopic{ .title = u"这是一个非常长的话题标题，用来检查标签栏与顶栏的截断和省略效果", .colorId = 0x8EEE98, .view = "topic-long" },
	TabTopic{ .title = u"问题反馈", .colorId = 0xFF93B2 },
	TabTopic{ .title = u"已关闭的旧话题", .colorId = 0xFB6F5F, .view = "topic-closed", .closed = true },
	TabTopic{ .title = u"Français", .colorId = 0xCB86DB, .view = "topic-french", .french = true },
	TabTopic{ .title = u"测试" },
	TabTopic{ .title = u"资源分享", .colorId = 0x8EEE98 },
	TabTopic{ .title = u"闲聊", .colorId = 0xFF93B2 },
	TabTopic{ .title = u"周报", .colorId = 0xFFD67E },
};
// 每个话题的消息超过一屏，用于滚动检查浮动卡片；编号间隔须大于消息数。
constexpr auto kTabTopicMessages = 40;
constexpr auto kTabTopicStride = 100;
// 下标与 HistoryView::SubsectionTabsMode 的取值一致。
constexpr auto kTabsModes = std::array{ "top", "left", "bottom" };
base::weak_ptr<Main::Session> SeededSession;

[[nodiscard]] bool isUser(Kind kind) {
	return kind == Kind::Private || kind == Kind::Draft
		|| kind == Kind::Contact || kind == Kind::Blocked
		|| kind == Kind::DeletedAccount || kind == Kind::Business || kind == Kind::Paid
		|| kind == Kind::Bot || kind == Kind::Keyboard || kind == Kind::InlineBot
		|| kind == Kind::Sponsored || kind == Kind::Saved || kind == Kind::ArchivedPrivate;
}
[[nodiscard]] PeerId scenarioPeerId(int index) {
	if (kScenarios[index].kind == Kind::Saved) {
		return SeededSession ? SeededSession->userPeerId() : peerFromUser(UserId(999999999));
	}
	const auto id = kFirstPeerId + index;
	return isUser(kScenarios[index].kind)
		? peerFromUser(UserId(id)) : peerFromChannel(ChannelId(id));
}
[[nodiscard]] int findScenario(const QString &key) {
	for (auto i = 0; i != kScenarios.size(); ++i) {
		if (key == QLatin1String(kScenarios[i].key)) {
			return i;
		}
	}
	return -1;
}
// 定时消息和快捷回复页的样本超过一屏，用于滚动检查浮动输入区。
constexpr auto kListSampleCount = 30;
// 快捷回复默认上限 20 条，达到上限会隐藏输入框，样本留出余量。
constexpr auto kShortcutSampleCount = 18;
constexpr auto kShortcutId = 1;
constexpr auto kShortcutMessageId = kFirstMessageId + 2000;
[[nodiscard]] QString listSampleText(int index) {
	const auto texts = std::array{
		u"一小时后提醒检查模拟场景。",
		u"多行样本的第一行。\n第二行滚到输入框下方时应透出模糊内容。\n第三行。",
		u"这条样本比较长，用来检查气泡换行，以及滚动经过磨砂输入框时的模糊效果。",
		u"短样本。",
		u"Mixed 中英文 sample 与数字 123。",
	};
	return u"%1. %2"_q.arg(index + 1).arg(
		QString::fromUtf16(texts[index % texts.size()]));
}
[[nodiscard]] MsgId tabTopicRoot(int index) {
	return MsgId(index ? (kTopicRootId + 100 * index) : Data::ForumTopic::kGeneralId);
}
[[nodiscard]] std::vector<MsgId> scenarioTopicRoots(Kind kind) {
	auto result = std::vector<MsgId>();
	const auto tabs = (kind == Kind::TopicTabs);
	const auto count = tabs ? int(kTabTopics.size()) : (kind == Kind::Topic) ? 3 : 0;
	for (auto i = 0; i != count; ++i) {
		result.push_back(tabs ? tabTopicRoot(i) : MsgId(kTopicRootId + 100 * i));
	}
	return result;
}
// 视图对应的话题根消息；0 表示该视图不进入话题。
[[nodiscard]] MsgId topicViewRoot(Kind kind, const QString &view) {
	if (kind == Kind::Topic) {
		return (view == u"main"_q) ? MsgId(kTopicRootId)
			: (view == u"topic-unread"_q) ? MsgId(kTopicRootId + 100)
			: (view == u"topic-closed"_q) ? MsgId(kTopicRootId + 200)
			: MsgId();
	}
	if (kind != Kind::TopicTabs) {
		return MsgId();
	}
	for (auto i = 0; i != kTabTopics.size(); ++i) {
		if (kTabTopics[i].view && view == QLatin1String(kTabTopics[i].view)) {
			return tabTopicRoot(i);
		}
	}
	return MsgId();
}
void initialiseUser(
		not_null<UserData*> user,
		const QString &name) {
	user->setName(name, {}, {}, {});
	user->setAccessHash(0);
	user->setFlags(UserDataFlag::MessageMoneyRestrictionsKnown);
	user->setBarSettings(PeerBarSettings());
	user->setLoadedStatus(PeerData::LoadedStatus::Full);
}

[[nodiscard]] MTPMessage makeMessage(
		not_null<PeerData*> peer,
		PeerId sender,
		int id,
		const QString &text,
		bool pinned,
		bool topic,
		int shortcutId = 0,
		bool keyboard = false,
		bool edited = false,
		bool noForwards = false,
		MsgId replyId = 0,
		MsgId topicRoot = kTopicRootId,
		PeerId savedPeer = {},
		bool inlineKeyboard = false,
		std::optional<MTPMessageMedia> media = std::nullopt,
		int64 groupedId = 0,
		TimeId date = 0) {
	using Flag = MTPDmessage::Flag;
	using ReplyFlag = MTPDmessageReplyHeader::Flag;
	const auto discussion = peer->isBroadcast() ? peer->asChannel()->discussionLink() : nullptr;
	auto entities = QVector<MTPMessageEntity>();
	const auto urlOffset = text.indexOf(u"https://"_q);
	if (urlOffset >= 0) {
		entities.push_back(MTP_messageEntityUrl(MTP_int(urlOffset),
			MTP_int(text.mid(urlOffset).section(' ', 0, 0).size())));
	}
	const auto boldOffset = text.indexOf(u"粗体"_q);
	if (boldOffset >= 0) {
		entities.push_back(MTP_messageEntityBold(MTP_int(boldOffset), MTP_int(2)));
	}
	const auto reply = (topic || replyId) ? MTP_messageReplyHeader(
		MTP_flags(ReplyFlag::f_reply_to_msg_id
			| (topic ? ReplyFlag::f_forum_topic : ReplyFlag())),
		MTP_int(replyId ? replyId.bare : topicRoot.bare),
		MTPPeer(), MTPMessageFwdHeader(), MTPMessageMedia(),
		MTPint(), MTPstring(), MTPVector<MTPMessageEntity>(),
		MTPint(), MTPint(), MTPstring()) : MTPMessageReplyHeader();
	const auto button = [](const QString &text) {
		return MTP_keyboardButton(MTP_flags(0), MTPKeyboardButtonStyle(),
			MTP_string(text), MTP_buttonTypeDefault());
	};
	const auto markup = inlineKeyboard ? MTP_replyInlineMarkup(MTP_flags(0),
		MTP_vector<MTPKeyboardInlineButtonRow>({ MTP_keyboardInlineButtonRow(
			MTP_vector<MTPKeyboardInlineButton>({ MTP_keyboardInlineButton(
				MTP_flags(0), MTPKeyboardButtonStyle(), MTP_string("复制本地样本"),
				MTP_inlineButtonTypeCopy(MTP_string("模拟按钮内容"))),
				MTP_keyboardInlineButton(MTP_flags(0), MTPKeyboardButtonStyle(),
					MTP_string("暂不可用"), MTP_inlineButtonTypeDisabled()) })) })) : keyboard ? MTP_replyKeyboardMarkup(
		MTP_flags(MTPDreplyKeyboardMarkup::Flag::f_resize),
		MTP_vector<MTPKeyboardButtonRow>({
			MTP_keyboardButtonRow(MTP_vector<MTPKeyboardButton>({
				button(u"菜单一"_q), button(u"菜单二"_q),
			})),
			MTP_keyboardButtonRow(MTP_vector<MTPKeyboardButton>({
				button(u"较长的按钮文字布局样本"_q),
			})),
		}), MTPstring()) : MTPReplyMarkup();
	return MTP_message(
		MTP_flags(Flag::f_from_id
			| ((sender == peer->session().userPeerId()) ? Flag::f_out : Flag())
			| (pinned ? Flag::f_pinned : Flag())
			| ((topic || replyId) ? Flag::f_reply_to : Flag())
			| (shortcutId ? Flag::f_quick_reply_shortcut_id : Flag())
			| ((keyboard || inlineKeyboard) ? Flag::f_reply_markup : Flag())
			| (savedPeer ? Flag::f_fwd_from : Flag())
			| ((savedPeer && peer->isSelf()) ? Flag::f_saved_peer_id : Flag())
			| (media ? Flag::f_media : Flag())
			| (discussion ? Flag::f_replies : Flag())
			| (groupedId ? Flag::f_grouped_id : Flag())
			| (!entities.empty() ? Flag::f_entities : Flag())
			| (peer->isBroadcast() ? Flag::f_views | Flag::f_forwards | Flag::f_post_author : Flag())
			| (edited ? Flag::f_edit_date : Flag())
			| (noForwards ? Flag::f_noforwards : Flag())
			| (peer->isBroadcast() ? Flag::f_post : Flag())),
		MTP_int(id), peerToMTP(sender), MTPint(), MTPstring(),
		peerToMTP(peer->id), savedPeer ? peerToMTP(savedPeer) : MTPPeer(),
		savedPeer ? MTP_messageFwdHeader(
			MTP_flags(MTPDmessageFwdHeader::Flag::f_from_id),
			peerToMTP(savedPeer), MTPstring(), MTP_int(base::unixtime::now() - 600),
			MTPint(), MTPstring(), MTPPeer(), MTPint(), MTPPeer(), MTPstring(),
			MTPint(), MTPstring()) : MTPMessageFwdHeader(),
		MTPlong(), MTPlong(), MTPPeer(), reply,
		MTP_int(date ? date : base::unixtime::now() - 300 + (id % 100)),
		MTP_string(text), media.value_or(MTPMessageMedia()), markup,
		MTP_vector<MTPMessageEntity>(entities), MTP_int(128), MTP_int(3),
		discussion ? MTP_messageReplies(
			MTP_flags(MTPDmessageReplies::Flag::f_comments), MTP_int(3), MTP_int(1),
			MTPVector<MTPPeer>(), MTP_long(peerToChannel(discussion->id).bare),
			MTPint(), MTPint()) : MTPMessageReplies(),
		MTP_int(edited ? base::unixtime::now() : 0),
		MTP_string("模拟作者"), MTP_long(groupedId),
		MTPMessageReactions(), MTPVector<MTPRestrictionReason>(),
		MTPint(), MTP_int(shortcutId), MTPlong(), MTPFactCheck(), MTPint(),
		MTPlong(), MTPSuggestedPost(), MTPint(), MTPstring(),
		MTPRichMessage());
}

// 话题创建服务消息即话题的根消息，服务器把它计入话题消息总数。
[[nodiscard]] MTPMessage makeTopicCreated(
		not_null<PeerData*> peer,
		PeerId sender,
		MsgId rootId,
		const QString &title,
		int32 colorId,
		TimeId date) {
	using Flag = MTPDmessageService::Flag;
	return MTP_messageService(
		MTP_flags(Flag::f_from_id
			| ((sender == peer->session().userPeerId()) ? Flag::f_out : Flag())),
		MTP_int(rootId.bare), peerToMTP(sender), peerToMTP(peer->id), MTPPeer(),
		MTPMessageReplyHeader(), MTP_int(date),
		MTP_messageActionTopicCreate(MTP_flags(0), MTP_string(title),
			MTP_int(colorId), MTPlong()),
		MTPMessageReactions(), MTPint());
}

// 话题置顶栏只读话题维度的存储，本地插入的消息只写入整个聊天维度。
void addTopicPinned(not_null<PeerData*> peer, MsgId rootId, std::vector<MsgId> ids) {
	const auto count = int(ids.size());
	peer->session().storage().add(Storage::SharedMediaAddSlice(
		peer->id, rootId, PeerId(), Storage::SharedMediaType::Pinned,
		std::move(ids), { 0, ServerMaxMsgId }, count));
}

// 标签布局下整个聊天的已读位置会同步到各话题，未读话题的消息编号排在最后。
void fillTabTopics(
		not_null<Main::Session*> session,
		not_null<History*> history,
		int baseId) {
	const auto peer = history->peer;
	const auto forum = peer->forum();
	const auto now = base::unixtime::now();
	const auto texts = std::array{
		u"大家好，这是话题里的第一条消息。",
		u"补充一下上面的内容，检查连续消息的分组。",
		u"这条消息比较长，用来检查话题内气泡的换行效果，以及顶栏下方第一条消息是否被遮挡。",
		u"收到。",
		u"Mixed 中英文 text 与数字 123。",
		u"多行消息的第一行。\n第二行滚动到置顶条下方时，文字不应透过卡片。\n第三行。",
		u"好的，我晚点再看。",
		u"这一段文字故意写得很长，滚动时可以清楚看到消息经过顶栏、标签栏、置顶条和翻译条下方时的遮挡效果，检查卡片是否保持不透明。",
	};
	const auto frenchTexts = std::array{
		u"Bonjour, voici un message pour vérifier la traduction.",
		u"Merci, je regarde cela plus tard dans la journée.",
		u"Ce paragraphe est volontairement long pour vérifier le défilement sous la barre de traduction.",
	};
	auto order = ranges::views::iota(0, int(kTabTopics.size())) | ranges::to_vector;
	ranges::stable_partition(order, [](int index) { return !kTabTopics[index].unread; });
	auto messages = QVector<MTPMessage>();
	auto topicIds = std::vector<std::vector<MsgId>>(kTabTopics.size());
	for (auto slot = 0; slot != order.size(); ++slot) {
		const auto index = order[slot];
		const auto &spec = kTabTopics[index];
		for (auto i = 0; i != kTabTopicMessages; ++i) {
			const auto id = baseId + kTabTopicStride * slot + i;
			// 每两条换一个发送者，用来检查连续消息的分组。
			const auto sender = (i % 5 == 3)
				? session->userPeerId()
				: peerFromUser(UserId(kFirstPeerId + (i / 2) % 3));
			const auto body = spec.french
				? frenchTexts[i % frenchTexts.size()]
				: texts[i % texts.size()];
			const auto text = u"%1. %2"_q.arg(i + 1).arg(QString::fromUtf16(body));
			const auto date = now - 10800 + 15 * (slot * kTabTopicMessages + i);
			messages.prepend(makeMessage(peer, sender, id, text, spec.pinned && i < 2,
				index != 0, 0, false, false, false, MsgId(), tabTopicRoot(index), {}, false,
				std::nullopt, 0, date));
			topicIds[index].push_back(MsgId(id));
		}
	}
	for (auto index = int(kTabTopics.size()) - 1; index > 0; --index) {
		const auto &spec = kTabTopics[index];
		messages.push_back(makeTopicCreated(peer, session->userPeerId(), tabTopicRoot(index),
			QString::fromUtf16(spec.title), spec.colorId, now - 11000 + 10 * index));
	}
	history->addNewerSlice(messages);
	history->addNewerSlice({});
	auto unread = 0;
	for (auto index = 0; index != kTabTopics.size(); ++index) {
		const auto &spec = kTabTopics[index];
		const auto &ids = topicIds[index];
		const auto root = tabTopicRoot(index);
		const auto topic = forum->topicFor(root);
		auto list = ids;
		if (index) {
			list.push_back(root);
		}
		const auto topicUnread = spec.unread ? int(ids.size()) - 1 : 0;
		topic->replies()->setLocalMessagesForDebug(std::move(list),
			spec.unread ? ids.front() : ids.back(), topicUnread);
		topic->applyMaybeLast(session->data().message(peer->id, ids.back()));
		if (spec.pinned) {
			addTopicPinned(peer, root, { ids[0], ids[1] });
		}
		unread += topicUnread;
	}
	session->data().setPinnedFromEntryList(forum->topicFor(tabTopicRoot(1)), true);
	forum->topicsList()->setLoaded();
	const auto lastId = baseId
		+ kTabTopicStride * (int(order.size()) - 1)
		+ kTabTopicMessages - 1;
	history->setUnreadCount(unread);
	history->setInboxReadTill(lastId - unread);
	history->outboxRead(lastId);
}


void fillHistory(not_null<Main::Session*> session, not_null<PeerData*> peer, int index) {
	const auto &spec = kScenarios[index];
	const auto kind = spec.kind;
	const auto history = session->data().history(peer);
	if (spec.category == Category::Archive) {
		history->setFolder(session->data().folder(Data::Folder::kId));
	} else {
		history->clearFolder();
	}
	history->addOlderSlice({});
	if (kind == Kind::TopicTabs) {
		fillTabTopics(session, history, kFirstMessageId + 100 * index);
		history->setChatListTimeId(base::unixtime::now() - index);
		history->updateChatListExistence();
		return;
	}
	const auto rich = kind == Kind::Private || kind == Kind::Group || kind == Kind::Saved;
	const auto pinned = spec.category == Category::Groups
		|| kind == Kind::Broadcast || kind == Kind::Discussion
		|| kind == Kind::Translate || kind == Kind::Topic || kind == Kind::Saved
		|| kind == Kind::ArchivedChannel;
	const auto unread = kind == Kind::Draft || kind == Kind::ArchivedPrivate;
	const auto protectedContent = kind == Kind::ProtectedGroup || kind == Kind::ProtectedChannel;
	const auto topicCount = (kind == Kind::Topic) ? 3 : 1;
	auto messages = QVector<MTPMessage>();
	auto topicIds = std::array<std::vector<MsgId>, 3>();
	auto savedIds = std::array<std::vector<MsgId>, 3>();
	const auto baseId = kFirstMessageId + 100 * index;
	const auto withMedia = kind == Kind::Group || kind == Kind::Saved;
	const auto count = withMedia ? 16 : rich ? 9 : 6;
	const auto texts = std::array{
		u"这是一条普通消息，可检查消息气泡、头像与间距。",
		u"这条消息引用了上一条，点击引用可返回原消息。",
		u"这条消息已经编辑，检查编辑标记。",
		u"这条消息已经删除，检查删除标记和右键菜单。",
		u"这条消息带有自动删除倒计时，右键查看剩余时间。",
		u"这是自己发出的消息，可检查已读标记。",
		u"多行消息中的粗体。\n第二行包含中文、English 和数字 123。\n第三行用于检查长内容、选择与复制。",
		u"这条消息限制转发，检查转发和复制入口。",
		u"链接样本：https://example.com/ ；也可搜索“样本”检查搜索结果。",
	};
	for (auto topic = 0; topic != topicCount; ++topic) {
		const auto root = MsgId(kTopicRootId + 100 * topic);
		for (auto i = 0; i != count; ++i) {
			const auto id = baseId + 20 * topic + i;
			const auto outgoing = (i == 5 && !unread && !(kind == Kind::Topic && topic == 1))
				|| kind == Kind::Saved;
			const auto sender = peer->isBroadcast() ? peer->id
				: outgoing ? session->userPeerId()
				: (peer->isUser() ? peer->id : peerFromUser(UserId(kFirstPeerId)));
			const auto text = i >= 9 ? u"非音视频内容样本 %1"_q.arg(i - 8) : kind == Kind::Translate
				? u"Bonjour, voici un message pour vérifier la traduction."_q
				: rich ? QString::fromUtf16(texts[i])
				: u"样本消息 %1：%2"_q.arg(i + 1).arg(QString::fromUtf16(spec.name));
			const auto sourceIndex = (i == 14) ? 1 : i % 3;
			const auto savedPeer = kind == Kind::Saved
				? (sourceIndex == 0 ? session->userPeerId()
					: scenarioPeerId(findScenario(sourceIndex == 1 ? u"private"_q : u"channel"_q)))
				: (rich && i == 8) ? scenarioPeerId(findScenario(u"channel"_q)) : PeerId();
			auto media = std::optional<MTPMessageMedia>();
			if (i >= 9) {
				const auto kinds = std::array{
					SimulationMedia::Photo, SimulationMedia::File, SimulationMedia::Contact,
					SimulationMedia::Poll, SimulationMedia::Photo, SimulationMedia::Photo,
					SimulationMedia::Sticker,
				};
				media = simulationMedia(session, kinds[i - 9], id);
			}
			messages.prepend(makeMessage(peer, sender, id, text,
				pinned && i < 2, kind == Kind::Topic, 0,
				kind == Kind::Keyboard && i == count - 1,
				rich && i == 2, protectedContent || (rich && i == 7),
				(kind != Kind::Topic && i == 1) ? MsgId(baseId) : MsgId(),
				root, savedPeer, kind == Kind::InlineBot && i == count - 1,
				media, (i == 13 || i == 14) ? int64(baseId) : 0));
			topicIds[topic].push_back(MsgId(id));
			if (kind == Kind::Saved) {
				savedIds[sourceIndex].push_back(MsgId(id));
			}
		}
	}
	for (auto topic = (kind == Kind::Topic) ? topicCount : 0; topic != 0; --topic) {
		const auto root = MsgId(kTopicRootId + 100 * (topic - 1));
		messages.push_back(makeTopicCreated(peer, session->userPeerId(), root,
			peer->forum()->topicFor(root)->title(), 0x6FB9F0, base::unixtime::now() - 600));
	}
	history->addNewerSlice(messages);
	history->addNewerSlice({});
	if (rich) {
		session->data().message(peer->id, MsgId(baseId + 3))->setDeleted();
		session->data().message(peer->id, MsgId(baseId + 4))->applyTTL(base::unixtime::now() + 86400);
		peer->setMessagesTTL(86400);
	}
	if (kind == Kind::Keyboard) {
		history->setLastKeyboard(baseId + count - 1, peer->id);
	} else if (kind == Kind::Sponsored) {
		session->sponsoredMessages().setLocalForDebug(history);
	}
	if (kind == Kind::Topic) {
		for (auto i = 0; i != topicCount; ++i) {
			const auto root = MsgId(kTopicRootId + 100 * i);
			const auto topic = peer->forum()->topicFor(root);
			const auto last = session->data().message(peer->id, topicIds[i].back());
			auto ids = topicIds[i];
			ids.push_back(root);
			topic->replies()->setLocalMessagesForDebug(std::move(ids),
				(i == 1) ? topicIds[i].front() : topicIds[i].back(), (i == 1) ? count - 1 : 0);
			topic->applyMaybeLast(last);
			addTopicPinned(peer, root, { topicIds[i][0], topicIds[i][1] });
			if (i == 0) {
				session->data().setPinnedFromEntryList(topic, true);
			}
		}
		peer->forum()->topicsList()->setLoaded();
	}
	if (kind == Kind::Saved) {
		for (auto i = 0; i != kListSampleCount; ++i) {
			session->scheduledMessages().apply(MTP_updateNewScheduledMessage(
				makeMessage(peer, session->userPeerId(), baseId + 50 + i,
					listSampleText(i), false, false, 0, false, false, false,
					0, 0, {}, false, std::nullopt, 0,
					base::unixtime::now() + 3600 + 900 * i)
			).c_updateNewScheduledMessage());
		}
		for (auto i = 0; i != 3; ++i) {
			const auto source = i == 0 ? session->userPeerId()
				: scenarioPeerId(findScenario(i == 1 ? u"private"_q : u"channel"_q));
			const auto sublist = session->data().savedMessages().sublist(session->data().peer(source));
			sublist->setLocalMessagesForDebug(savedIds[i]);
		}
		session->data().savedMessages().chatsList()->setLoaded();
	}
	history->setUnreadCount(unread ? 3 : 0);
	history->setInboxReadTill(baseId + count - (unread ? 4 : 1));
	history->outboxRead(baseId + count - 1);
	history->setUnreadMark(kind == Kind::ArchivedPrivate);
	history->setMuted(kind == Kind::Draft || kind == Kind::ArchivedGroup);
	if (kind == Kind::Draft || kind == Kind::Topic) {
		auto draft = std::make_unique<Data::Draft>();
		draft->textWithTags.text = u"尚未发送的草稿\n切换对话后可回来继续编辑。"_q;
		draft->reply.topicRootId = kind == Kind::Topic ? MsgId(kTopicRootId + 100) : MsgId();
		history->createCloudDraft(draft->reply.topicRootId, {}, draft.get());
		history->setLocalDraft(std::move(draft));
	}
	history->setChatListTimeId(base::unixtime::now() - index);
	history->updateChatListExistence();
}

// 本地给出快捷回复和问候、离开设置，Business 页无需等待服务器即可进入。
void seedBusiness(not_null<Main::Session*> session) {
	auto &messages = session->data().shortcutMessages();
	const auto now = base::unixtime::now();
	for (auto i = 0; i != kShortcutSampleCount; ++i) {
		messages.apply(MTP_updateQuickReplyMessage(makeMessage(
			session->user(), session->userPeerId(), kShortcutMessageId + i,
			listSampleText(i), false, false, kShortcutId, false, false, false,
			0, 0, {}, false, std::nullopt, 0,
			now - 3600 + 60 * i)).c_updateQuickReplyMessage());
	}
	// 消息先于列表写入，列表的最新消息已在本地时才会标记为已加载。
	messages.apply(MTP_updateQuickReplies(MTP_vector<MTPQuickReply>({
		MTP_quickReply(MTP_int(kShortcutId), MTP_string("layout"),
			MTP_int(kShortcutMessageId + kShortcutSampleCount - 1),
			MTP_int(kShortcutSampleCount)),
	})).c_updateQuickReplies());
	session->data().businessInfo().applyAwaySettings({});
	session->data().businessInfo().applyGreetingSettings({});
}

void seedScenario(not_null<Main::Session*> session, int index) {
	const auto &spec = kScenarios[index];
	const auto kind = spec.kind;
	const auto peer = session->data().peer(scenarioPeerId(index));
	if (const auto user = peer->asUser()) {
		if (kind != Kind::Saved) {
			initialiseUser(user, QString::fromUtf16(spec.name));
		}
		if (kind == Kind::Private || kind == Kind::Draft) {
			user->setFlags(UserDataFlag::MessageMoneyRestrictionsKnown | UserDataFlag::Contact);
		} else if (kind == Kind::Contact) {
			user->setBarSettings(PeerBarSetting::AddContact | PeerBarSetting::BlockContact);
		} else if (kind == Kind::Blocked) {
			user->setIsBlocked(true);
		} else if (kind == Kind::DeletedAccount) {
			user->setFlags(UserDataFlag::MessageMoneyRestrictionsKnown | UserDataFlag::Deleted);
		} else if (spec.category == Category::Bots) {
			user->setBotInfoVersion(1);
			user->botInfo->startToken = kind == Kind::Bot ? u"simulation"_q : QString();
			user->botInfo->inited = true;
			user->botInfo->commands = { { u"help"_q, u"查看模拟命令说明"_q } };
		} else if (kind == Kind::Business || kind == Kind::Paid) {
			using Flag = MTPDpeerSettings::Flag;
			user->setBarSettings(MTP_peerSettings(
				MTP_flags(kind == Kind::Business
					? Flag::f_business_bot_id | Flag::f_business_bot_can_reply
					: Flag::f_charge_paid_message_stars),
				MTPint(), MTPstring(), MTPint(),
				MTP_long(peerToUser(scenarioPeerId(findScenario(u"bot"_q))).bare),
				MTP_string("https://example.com/"), MTP_long(5),
				MTPstring(), MTPstring(), MTPint(), MTPint()));
			if (kind == Kind::Paid) {
				user->setStarsPerMessage(5);
			}
		}
	} else {
		const auto channel = peer->asChannel();
		using Flag = ChannelDataFlag;
		const auto broadcast = spec.category == Category::Channels || kind == Kind::ArchivedChannel;
		channel->setName(QString::fromUtf16(spec.name), {});
		channel->setAccessHash(0);
		channel->setFlags((broadcast ? Flag::Broadcast : Flag::Megagroup)
			| (kind == Kind::Join ? Flag::Left : Flag())
			| (kind == Kind::Discussion ? Flag::HasLink : Flag())
			| (kind == Kind::Topic ? Flag::Forum : Flag())
			| (kind == Kind::TopicTabs ? (Flag::Forum | Flag::ForumTabs) : Flag())
			| (kind == Kind::Slow ? Flag::SlowmodeEnabled : Flag())
			| ((kind == Kind::ProtectedGroup || kind == Kind::ProtectedChannel) ? Flag::NoForwards : Flag()));
		channel->setLoadedStatus(PeerData::LoadedStatus::Full);
		channel->setBarSettings(PeerBarSettings());
		channel->setMembersCount(128);
		channel->setDefaultRestrictions((kind == Kind::Restricted)
			? Data::AllSendRestrictions()
			: (kind == Kind::VoiceRestricted)
			? (ChatRestriction::SendVoiceMessages | ChatRestriction::SendVideoMessages)
			: ChatRestrictions());
		if (kind == Kind::Discussion) {
			channel->setDiscussionLink(session->data().channel(peerToChannel(scenarioPeerId(findScenario(u"group"_q)))));
		}
		if (kind == Kind::Requests || kind == Kind::TopicTabs) {
			channel->setAdminRights(ChatAdminRight::InviteByLinkOrAdd | ChatAdminRight::ProcessJoinRequests
				| (kind == Kind::TopicTabs ? (ChatAdminRight::ManageTopics | ChatAdminRight::PinMessages) : ChatAdminRight()));
			channel->setPendingRequestsCount(1, { UserId(kFirstPeerId + 1) });
		}
		if (kind == Kind::Slow) {
			channel->setSlowmodeSeconds(60);
			channel->growSlowmodeLastMessage(base::unixtime::now());
		}
		if (kind == Kind::Topic) {
			const auto names = std::array{ u"常规与置顶", u"未读讨论与草稿", u"已关闭话题" };
			for (auto i = 0; i != names.size(); ++i) {
				const auto root = MsgId(kTopicRootId + 100 * i);
				channel->forum()->applyTopicAdded(root, QString::fromUtf16(names[i]), 0x6FB9F0, 0,
					session->userPeerId(), base::unixtime::now() - 600, true);
				channel->forum()->topicFor(root)->setClosed(i == 2);
			}
		}
		if (kind == Kind::TopicTabs) {
			for (auto i = 0; i != kTabTopics.size(); ++i) {
				const auto &topic = kTabTopics[i];
				const auto root = tabTopicRoot(i);
				channel->forum()->applyTopicAdded(root, QString::fromUtf16(topic.title), topic.colorId, 0,
					session->userPeerId(), base::unixtime::now() - 7800 + 10 * i, true);
				channel->forum()->topicFor(root)->setClosed(topic.closed);
			}
		}
	}
	if (kind == Kind::SendAs) {
		const auto identity = session->data().peer(
			scenarioPeerId(findScenario(u"channel"_q)));
		session->sendAsPeers().setSimulationPeers({ peer }, {
			{ .peer = session->user() },
			{ .peer = identity },
		});
		session->sendAsPeers().setChosen(peer, identity->id);
	}
	fillHistory(session, peer, index);
	if (kind == Kind::Translate || kind == Kind::TopicTabs) {
		peer->setTranslationDisabled(false);
		session->data().history(peer)->translateOfferFrom({ QLocale::French });
	}
}

// 先离开当前聊天，完成原草稿保存后再安装场景草稿；keep 保留现有草稿。
void installScenarioDraft(
		not_null<Window::SessionController*> controller,
		int index,
		const QString &view,
		const QString &input) {
	if (input == u"keep"_q) {
		return;
	}
	const auto session = &controller->session();
	const auto peer = session->data().peer(scenarioPeerId(index));
	const auto history = session->data().history(peer);
	controller->showPeerHistory(session->userPeerId(),
		Window::SectionShow(Window::SectionShow::Way::ClearStack, anim::type::instant));
	const auto topicOffset = view == u"topic-unread"_q ? 1 : view == u"topic-closed"_q ? 2 : 0;
	const auto topicId = (kScenarios[index].kind == Kind::Topic && view != u"alternate"_q)
		? MsgId(kTopicRootId + 100 * topicOffset) : MsgId();
	history->clearLocalDraft(topicId, {});
	history->clearLocalEditDraft(topicId, {});
	history->setForwardDraft(topicId, {}, {});
	if (input == u"empty"_q) {
		return;
	}
	auto draft = std::make_unique<Data::Draft>();
	if (input == u"forward"_q) {
		history->setForwardDraft(topicId, {}, {
			.ids = { FullMsgId(peer->id, kFirstMessageId + 100 * index + 20 * topicOffset) },
			.options = Data::ForwardOptions::NoSenderNames,
		});
		draft->textWithTags.text = u"本地转发暂存验证"_q;
		history->setDraft(Data::DraftKey::Local(topicId, PeerId()), std::move(draft));
		return;
	}
	draft->reply = {
		.messageId = FullMsgId(peer->id, kFirstMessageId + 100 * index + 20 * topicOffset
			+ ((input == u"edit"_q) ? 5 : 0)),
		.topicRootId = topicId,
	};
	draft->textWithTags.text = u"本地输入区布局验证"_q;
	if (input == u"edit"_q) {
		history->setLocalEditDraft(std::move(draft));
	} else {
		history->setLocalDraft(std::move(draft));
	}
}

[[nodiscard]] Result showScenarioView(
		not_null<Window::SessionController*> controller,
		int index,
		const QString &view) {
	const auto session = &controller->session();
	const auto peer = session->data().peer(scenarioPeerId(index));
	const auto history = session->data().history(peer);
	const auto kind = kScenarios[index].kind;
	const auto way = Window::SectionShow::Way::ClearStack;
	if (kind == Kind::Slow) {
		peer->asChannel()->growSlowmodeLastMessage(base::unixtime::now());
	}
	if (const auto root = topicViewRoot(kind, view)) {
		// 与在会话列表里点开一致：普通论坛先在左栏展开话题列表，标签论坛直接进入话题。
		if (kind == Kind::Topic) {
			controller->showForum(peer->forum(), Window::SectionShow(way).withChildColumn());
		}
		controller->showTopic(peer->forum()->topicFor(root), ShowAtTheEndMsgId, way);
		return Result::Ok();
	}
	if (view == u"shortcuts"_q) {
		controller->showSettings(Settings::ShortcutMessagesId(kShortcutId));
		return Result::Ok();
	}
	if (view.startsWith(u"source-"_q)) {
		const auto source = view == u"source-self"_q ? session->userPeerId()
			: scenarioPeerId(findScenario(view == u"source-private"_q ? u"private"_q : u"channel"_q));
		controller->showSublist(session->data().savedMessages().sublist(
			session->data().peer(source)), ShowAtTheEndMsgId, way);
		return Result::Ok();
	}
	if (view == u"pinned"_q) {
		const auto thread = (kind == Kind::Topic)
			? static_cast<Data::Thread*>(peer->forum()->topicFor(kTopicRootId))
			: history.get();
		// 置顶列表为空时分区会立即退回，先在这里给出明确错误。
		if (!thread->hasPinnedMessages()) {
			return Result::Err(u"scenario has no pinned messages"_q);
		}
		controller->showSection(std::make_shared<HistoryView::PinnedMemento>(thread), way);
		return Result::Ok();
	}
	if (view == u"actions"_q) {
		// 模拟模式拿不到服务器日志，只用于检查分区外框与底部按钮。
		const auto channel = peer->asChannel();
		if (!channel || !(channel->hasAdminRights() || channel->amCreator())) {
			return Result::Err(u"scenario has no recent actions"_q);
		}
		controller->showSection(std::make_shared<AdminLog::SectionMemento>(channel), way);
		return Result::Ok();
	}
	if (view == u"scheduled"_q) {
		controller->showSection(std::make_shared<HistoryView::ScheduledMemento>(history), way);
		return Result::Ok();
	}
	if (view == u"alternate"_q) {
		controller->showSection(std::make_shared<HistoryView::ChatMemento>(
			HistoryView::ChatViewId{ .history = history }), way);
		return Result::Ok();
	}
	controller->showPeerHistory(peer, way, ShowAtTheEndMsgId);
	return Result::Ok();
}

Result openSimulationSceneImpl(const QStringList &args) {
	if (args.empty() || !(args.size() % 2)) {
		return Result::Err(u"usage: simulation.open <key> [--view main|alternate|scheduled|shortcuts|pinned|actions|topic-unread|topic-closed|topic-general|topic-pinned|topic-long|topic-french|source-self|source-private|source-channel] [--input keep|empty|reply|edit|forward] [--tabs top|bottom|left]"_q);
	}
	auto view = u"main"_q;
	auto input = u"keep"_q;
	auto tabs = -1;
	for (auto i = 1; i < args.size(); i += 2) {
		if (args[i] == u"--view"_q) {
			view = args[i + 1];
		} else if (args[i] == u"--input"_q) {
			input = args[i + 1];
		} else if (args[i] == u"--tabs"_q) {
			const auto mode = ranges::find_if(kTabsModes, [&](const char *name) {
				return args[i + 1] == QLatin1String(name);
			});
			if (mode == end(kTabsModes)) {
				return Result::Err(u"unknown tabs layout"_q);
			}
			tabs = int(mode - begin(kTabsModes));
		} else {
			return Result::Err(u"unknown scenario option"_q);
		}
	}
	if (view != u"main"_q && view != u"alternate"_q
		&& view != u"scheduled"_q && view != u"shortcuts"_q
		&& view != u"pinned"_q && view != u"actions"_q
		&& view != u"topic-unread"_q && view != u"topic-closed"_q
		&& view != u"topic-general"_q && view != u"topic-pinned"_q
		&& view != u"topic-long"_q && view != u"topic-french"_q
		&& view != u"source-self"_q && view != u"source-private"_q && view != u"source-channel"_q) {
		return Result::Err(u"unknown view"_q);
	}
	if (input != u"keep"_q && input != u"empty"_q
		&& input != u"reply"_q && input != u"edit"_q && input != u"forward"_q) {
		return Result::Err(u"unknown input state"_q);
	}
	if ((view == u"scheduled"_q || view == u"shortcuts"_q
			|| view == u"pinned"_q || view == u"actions"_q || view.startsWith(u"source-"_q))
		&& input != u"keep"_q) {
		return Result::Err(u"input states require main or alternate view"_q);
	}
	const auto session = ActiveSession();
	if (!session || SeededSession.get() != session) {
		return Result::Err(u"simulation mode is required"_q);
	}
	const auto controller = session->tryResolveWindow();
	if (!controller) {
		return Result::Err(u"no window controller"_q);
	}
	const auto index = findScenario(args.front());
	if (index < 0) {
		return Result::Err(u"unknown scenario, use simulation.list"_q);
	}
	const auto kind = kScenarios[index].kind;
	if (view.startsWith(u"topic-"_q) && !topicViewRoot(kind, view)) {
		return Result::Err(u"topic view is not available in this scene"_q);
	}
	if (tabs >= 0 && kind != Kind::TopicTabs) {
		return Result::Err(u"tabs layout requires the topic-tabs scene"_q);
	}
	if (view.startsWith(u"source-"_q) && kind != Kind::Saved) {
		return Result::Err(u"source view requires the saved scene"_q);
	}
	if (view == u"topic-closed"_q && input != u"keep"_q) {
		return Result::Err(u"closed topics do not accept input states"_q);
	}
	if (view == u"topic-unread"_q && input == u"edit"_q) {
		return Result::Err(u"the unread topic contains incoming messages only"_q);
	}
	if (input != u"keep"_q && kind != Kind::Private && kind != Kind::Topic) {
		return Result::Err(u"input states require private or topic scenario"_q);
	}
	controller->setActiveChatsFilter(FilterId(int(kScenarios[index].category) + 2),
		Window::SectionShow(Window::SectionShow::Way::ClearStack, anim::type::instant));
	installScenarioDraft(controller, index, view, input);
	const auto peer = session->data().peer(scenarioPeerId(index));
	if (tabs >= 0) {
		// 标签布局只在创建时读取：先离开并丢弃缓存的标签，再按新布局打开。
		controller->showPeerHistory(session->userPeerId(),
			Window::SectionShow(Window::SectionShow::Way::ClearStack, anim::type::instant));
		controller->dropSubsectionTabs();
		session->settings().setSubsectionTabsMode(peer->id, qint32(tabs));
	}
	if (kind == Kind::Bot) {
		peer->asUser()->botInfo->startToken = u"layout"_q;
	}
	if (auto result = showScenarioView(controller, index, view); !result.ok) {
		return result;
	}
	return Result::Ok(Compact({ { "key", kScenarios[index].key },
		{ "peerId", peer->id.value }, { "view", view.toStdString() },
		{ "input", input.toStdString() } }));
}


} // namespace

QString simulationCategoryName(SimulationCategory category) {
	switch (category) {
	case Category::Private: return tr::extras_SimulationPrivate(tr::now);
	case Category::Groups: return tr::extras_SimulationGroups(tr::now);
	case Category::Channels: return tr::extras_SimulationChannels(tr::now);
	case Category::Bots: return tr::extras_SimulationBots(tr::now);
	case Category::Topics: return tr::extras_SimulationTopics(tr::now);
	case Category::Saved: return tr::extras_SimulationSaved(tr::now);
	case Category::Archive: return tr::extras_SimulationArchive(tr::now);
	}
	Unexpected("Invalid simulation category.");
}
std::vector<SimulationScene> simulationScenes() {
	auto result = std::vector<SimulationScene>();
	for (auto i = 0; i != kScenarios.size(); ++i) {
		const auto &scene = kScenarios[i];
		result.push_back({
			QString::fromLatin1(scene.key), QString::fromUtf16(scene.name), scene.category,
			QString::fromLatin1(scene.features).split(','), scenarioPeerId(i),
		});
	}
	return result;
}
Result listSimulationScenes(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: simulation.list"_q);
	}
	auto result = json::array();
	for (const auto &scene : simulationScenes()) {
		auto features = json::array();
		for (const auto &feature : scene.features) {
			features.push_back(feature.toStdString());
		}
		auto entry = json{
			{ "key", scene.key.toStdString() }, { "name", scene.name.toStdString() },
			{ "category", int(scene.category) },
			{ "categoryName", simulationCategoryName(scene.category).toStdString() },
			{ "peerId", scene.peerId.value }, { "features", std::move(features) },
		};
		if (const auto session = SeededSession.get()) {
			const auto peer = session->data().peer(scene.peerId);
			const auto history = session->data().history(peer);
			const auto &sendAs = session->sendAsPeers();
			entry["state"] = {
				{ "sendAsCount", sendAs.list({ peer }).size() },
				{ "sendAsPeerId", sendAs.resolveChosen(peer)->id.value },
				{ "unread", history->unreadCount() }, { "unreadMark", history->unreadMark() },
				{ "muted", history->muted() }, { "archived", history->folder() != nullptr },
				{ "pinned", history->hasPinnedMessages() },
				{ "dialogPinned", history->isPinnedDialog(FilterId()) },
				{ "inCategory", history->inChatList(FilterId(int(scene.category) + 2)) },
				{ "slowmodeSecondsLeft", peer->slowmodeSecondsLeft() },
				{ "draft", history->localDraft({}, {}) != nullptr },
				{ "listDraft", history->cloudDraft({}, {}) != nullptr },
			};
			if (scene.category == Category::Topics) {
				auto topics = json::array();
				for (const auto root : scenarioTopicRoots(kScenarios[findScenario(scene.key)].kind)) {
					const auto topic = peer->forum()->topicFor(root);
					topics.push_back({ { "rootId", topic->rootId().bare },
						{ "title", topic->title().toStdString() },
						{ "unread", topic->replies()->unreadCountCurrent() },
						{ "closed", topic->closed() }, { "pinned", topic->hasPinnedMessages() },
						{ "draft", history->localDraft(topic->rootId(), {}) != nullptr },
						{ "listDraft", history->cloudDraft(topic->rootId(), {}) != nullptr } });
				}
				entry["state"]["topics"] = std::move(topics);
				if (peer->useSubsectionTabs()) {
					entry["state"]["tabs"] = kTabsModes[session->settings().subsectionTabsMode(peer->id)];
				}
			}
			if (scene.category == Category::Saved) {
				auto sources = json::array();
				for (const auto source : { session->userPeerId(),
						scenarioPeerId(findScenario(u"private"_q)), scenarioPeerId(findScenario(u"channel"_q)) }) {
					const auto sublist = session->data().savedMessages().sublist(session->data().peer(source));
					sources.push_back({ { "peerId", source.value },
						{ "count", sublist->fullCount().value_or(0) } });
				}
				entry["state"]["sources"] = std::move(sources);
			}
		}
		result.push_back(std::move(entry));
	}
	return Result::Ok(Compact(result));
}
Result openSimulationScene(const QStringList &args) {
	return openSimulationSceneImpl(args);
}

void seedSimulationScenarios(not_null<Main::Session*> session) {
	Expects(isSimulationSession(session));
	if (SeededSession.get() == session) {
		return;
	}
	SeededSession = base::make_weak(session);
	for (auto i = 0; i != kScenarios.size(); ++i) {
		seedScenario(session, i);
	}
	seedBusiness(session);
	auto &filters = session->data().chatsFilters();
	for (auto category = 0; category != 7; ++category) {
		auto histories = base::flat_set<not_null<History*>>();
		for (auto i = 0; i != kScenarios.size(); ++i) {
			if (int(kScenarios[i].category) == category) {
				histories.emplace(session->data().history(scenarioPeerId(i)));
			}
		}
		filters.set(Data::ChatFilter(category + 2,
			{ .text = { simulationCategoryName(Category(category)) } }, {}, {},
			Data::ChatFilter::Flags(), std::move(histories), {}, {}));
	}
	if (ExtrasSettings::getInstance().hideAllChatsFolder()) {
		filters.remove(FilterId());
	}
	// 未加入频道仍需保留在固定列表，使用原生置顶对话状态。
	session->data().setChatPinned(
		session->data().history(scenarioPeerId(findScenario(u"join"_q))), FilterId(), true);
	session->data().setChatPinned(
		session->data().history(scenarioPeerId(findScenario(u"private"_q))), FilterId(), true);
	session->data().chatsList()->setLoaded();
	session->data().folder(Data::Folder::kId)->chatsList()->setLoaded();
}
MTPMessage simulationTextMessage(not_null<PeerData*> peer, PeerId sender,
		int id, const QString &text, bool edited) {
	return makeMessage(peer, sender, id, text, false, false, 0, false, edited);
}
PeerData *simulationPeer(not_null<Main::Session*> session, const QString &key) {
	Expects(isSimulationSession(session));
	const auto index = findScenario(key);
	return (index >= 0) ? session->data().peer(scenarioPeerId(index)).get() : nullptr;
}
Result triggerSimulationCountdown(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: simulation.trigger delete-countdown"_q);
	}
	const auto session = ActiveSession();
	if (!session || !isSimulationSession(session)) {
		return Result::Err(u"simulation mode is required"_q);
	}
	const auto peer = simulationPeer(session, u"private"_q);
	static auto nextId = 2600000;
	const auto item = session->data().addNewMessage(
		makeMessage(peer, peer->id, ++nextId, u"这条消息将在 15 秒后自动删除。"_q,
			false, false), MessageFlags(), NewMessageType::Unread);
	Expects(item != nullptr);
	const auto expires = base::unixtime::now() + 15;
	item->applyTTL(expires);
	return Result::Ok(Compact({ { "peerId", peer->id.value },
		{ "messageId", item->id.bare }, { "expiresAt", expires } }));
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
