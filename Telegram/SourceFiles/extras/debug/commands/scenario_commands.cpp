#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "extras/debug/debug_login.h"
#include "base/unixtime.h"
#include "data/data_channel.h"
#include "data/business/data_shortcut_messages.h"
#include "data/components/sponsored_messages.h"
#include "data/data_drafts.h"
#include "data/data_forum.h"
#include "data/data_forum_topic.h"
#include "data/data_peer_bot_command.h"
#include "data/data_replies_list.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/dialogs_key.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/admin_log/history_admin_log_section.h"
#include "history/view/history_view_chat_section.h"
#include "history/view/history_view_pinned_section.h"
#include "history/view/history_view_scheduled_section.h"
#include "main/main_session.h"
#include "spellcheck/spellcheck_types.h"
#include "settings/business/settings_shortcut_messages.h"
#include "window/window_session_controller.h"

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

enum class Kind {
	Private,
	Contact,
	Blocked,
	Bot,
	Broadcast,
	Discussion,
	Join,
	Restricted,
	Translate,
	Requests,
	Topic,
	Call,
	Business,
	Paid,
	Keyboard,
	Sponsored,
	Pinned,
};

struct Scenario {
	const char *key;
	const char16_t *name;
	Kind kind;
};

constexpr auto kScenarios = std::array{
	Scenario{ "private", u"01 普通私聊与多行输入", Kind::Private },
	Scenario{ "contact", u"02 陌生人联系人提示", Kind::Contact },
	Scenario{ "blocked", u"03 已屏蔽用户", Kind::Blocked },
	Scenario{ "bot", u"04 机器人启动", Kind::Bot },
	Scenario{ "channel", u"05 只读频道与置顶", Kind::Broadcast },
	Scenario{ "discussion", u"06 频道讨论入口", Kind::Discussion },
	Scenario{ "join", u"07 未加入频道", Kind::Join },
	Scenario{ "restricted", u"08 群聊发送限制", Kind::Restricted },
	Scenario{ "translate", u"09 翻译与置顶组合", Kind::Translate },
	Scenario{ "requests", u"10 加入申请与置顶", Kind::Requests },
	Scenario{ "topic", u"11 话题输入区", Kind::Topic },
	Scenario{ "call", u"12 通话与申请堆叠", Kind::Call },
	Scenario{ "business", u"13 商业机器人提示", Kind::Business },
	Scenario{ "paid", u"14 付费消息提示", Kind::Paid },
	Scenario{ "keyboard", u"15 机器人键盘", Kind::Keyboard },
	Scenario{ "sponsored", u"16 顶部广告样本", Kind::Sponsored },
	Scenario{ "pinned", u"17 多条置顶消息", Kind::Pinned },
};

constexpr auto kFirstPeerId = uint64(810000001);
constexpr auto kFirstMessageId = 2000001;
constexpr auto kTopicRootId = 500;
base::weak_ptr<Main::Session> SeededSession;

[[nodiscard]] bool isUser(Kind kind) {
	return kind == Kind::Private
		|| kind == Kind::Contact
		|| kind == Kind::Blocked
		|| kind == Kind::Bot
		|| kind == Kind::Business
		|| kind == Kind::Paid
		|| kind == Kind::Keyboard
		|| kind == Kind::Sponsored;
}

[[nodiscard]] PeerId scenarioPeerId(int index) {
	const auto id = kFirstPeerId + index;
	return isUser(kScenarios[index].kind)
		? peerFromUser(UserId(id))
		: peerFromChannel(ChannelId(id));
}

[[nodiscard]] json scenarioList() {
	auto result = json::array();
	for (auto i = 0; i != kScenarios.size(); ++i) {
		result.push_back({
			{ "key", kScenarios[i].key },
			{ "name", QString::fromUtf16(kScenarios[i].name).toStdString() },
			{ "peerId", scenarioPeerId(i).value },
		});
	}
	return result;
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
		bool edited = false) {
	using Flag = MTPDmessage::Flag;
	using ReplyFlag = MTPDmessageReplyHeader::Flag;
	const auto reply = topic ? MTP_messageReplyHeader(
		MTP_flags(ReplyFlag::f_forum_topic | ReplyFlag::f_reply_to_msg_id),
		MTP_int(kTopicRootId),
		MTPPeer(), MTPMessageFwdHeader(), MTPMessageMedia(),
		MTPint(), MTPstring(), MTPVector<MTPMessageEntity>(),
		MTPint(), MTPint(), MTPstring()) : MTPMessageReplyHeader();
	const auto button = [](const QString &text) {
		return MTP_keyboardButton(MTP_flags(0), MTPKeyboardButtonStyle(),
			MTP_string(text), MTP_buttonTypeDefault());
	};
	const auto markup = keyboard ? MTP_replyKeyboardMarkup(
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
			| (topic ? Flag::f_reply_to : Flag())
			| (shortcutId ? Flag::f_quick_reply_shortcut_id : Flag())
			| (keyboard ? Flag::f_reply_markup : Flag())
			| (edited ? Flag::f_edit_date : Flag())
			| (peer->isBroadcast() ? Flag::f_post : Flag())),
		MTP_int(id), peerToMTP(sender), MTPint(), MTPstring(),
		peerToMTP(peer->id), MTPPeer(), MTPMessageFwdHeader(),
		MTPlong(), MTPlong(), MTPPeer(), reply,
		MTP_int(base::unixtime::now() - 300 + (id % 100)),
		MTP_string(text), MTPMessageMedia(), markup,
		MTPVector<MTPMessageEntity>(), MTPint(), MTPint(),
		MTPMessageReplies(), MTP_int(edited ? base::unixtime::now() : 0), MTPstring(), MTPlong(),
		MTPMessageReactions(), MTPVector<MTPRestrictionReason>(),
		MTPint(), MTP_int(shortcutId), MTPlong(), MTPFactCheck(), MTPint(),
		MTPlong(), MTPSuggestedPost(), MTPint(), MTPstring(),
		MTPRichMessage());
}

void fillHistory(
		not_null<Main::Session*> session,
		not_null<PeerData*> peer,
		int index,
		bool pinned) {
	const auto history = session->data().history(peer);
	history->clearFolder();
	history->addOlderSlice({});
	const auto topic = kScenarios[index].kind == Kind::Topic;
	const auto translating = kScenarios[index].kind == Kind::Translate;
	const auto pinAll = kScenarios[index].kind == Kind::Pinned;
	const auto sender = peer->isBroadcast() ? peer->id
		: peer->isUser() ? peer->id
		: peerFromUser(UserId(kFirstPeerId));
	auto messages = QVector<MTPMessage>();
	auto replyIds = std::vector<MsgId>();
	if (topic) {
		messages.push_back(makeMessage(peer, session->userPeerId(),
			kTopicRootId, u"话题开始：检查独立输入区。"_q, false, false));
	}
	const auto messageCount = translating ? 5
		: kScenarios[index].kind == Kind::Sponsored ? 24 : 6;
	for (auto i = 0; i != messageCount; ++i) {
		const auto text = translating
			? u"Bonjour, voici un exemple de conversation pour vérifier la traduction et la disposition des messages. %1"_q.arg(i + 1)
			: (i == 0 && pinned)
			? u"置顶说明：检查顶部条、正文留白与窄窗换行。"_q
			: u"本地场景消息 %1：用于检查输入区和提示条的布局。"_q.arg(i + 1);
		const auto id = kFirstMessageId + 100 * index + i;
		const auto outgoing = (i == messageCount - 1)
			&& (topic || kScenarios[index].kind == Kind::Private);
		messages.prepend(makeMessage(peer,
			outgoing ? session->userPeerId() : sender, id,
			text, pinned && (i == 0 || pinAll), topic, 0,
			kScenarios[index].kind == Kind::Keyboard && i == messageCount - 1));
		replyIds.push_back(id);
	}
	history->addNewerSlice(messages);
	history->addNewerSlice({});
	if (kScenarios[index].kind == Kind::Keyboard) {
		history->setLastKeyboard(kFirstMessageId + 100 * index + messageCount - 1,
			peer->id);
	} else if (kScenarios[index].kind == Kind::Sponsored) {
		session->sponsoredMessages().setLocalForDebug(history);
	}
	if (topic) {
		const auto forumTopic = peer->forum()->topicFor(kTopicRootId);
		// 话题行的预览取自最后一条消息，缺少时行内只剩标题。
		const auto last = session->data().message(peer->id, replyIds.back());
		forumTopic->replies()->setLocalMessagesForDebug(std::move(replyIds));
		forumTopic->applyMaybeLast(last);
	}
	history->setUnreadCount(0);
	history->setInboxReadTill(kFirstMessageId + 100 * index + messageCount);
	history->setChatListTimeId(base::unixtime::now() - index);
	if (kScenarios[index].kind == Kind::Join) {
		session->data().setChatPinned(history, FilterId(), true);
	}
	history->updateChatListExistence();
}

void seedScenario(not_null<Main::Session*> session, int index) {
	const auto &spec = kScenarios[index];
	const auto name = QString::fromUtf16(spec.name);
	const auto peer = session->data().peer(scenarioPeerId(index));
	if (const auto user = peer->asUser()) {
		initialiseUser(user, name);
		if (spec.kind == Kind::Contact) {
			user->setBarSettings(PeerBarSetting::AddContact
				| PeerBarSetting::BlockContact);
		} else if (spec.kind == Kind::Blocked) {
			user->setIsBlocked(true);
		} else if (spec.kind == Kind::Bot || spec.kind == Kind::Keyboard
			|| spec.kind == Kind::Sponsored) {
			user->setBotInfoVersion(1);
			user->botInfo->startToken = (spec.kind == Kind::Bot)
				? u"layout"_q : QString();
			user->botInfo->inited = true;
			if (spec.kind == Kind::Keyboard) {
				user->botInfo->commands = {
					{ u"help"_q, u"查看本地命令补全示例"_q },
				};
			}
		} else if (spec.kind == Kind::Business || spec.kind == Kind::Paid) {
			using Flag = MTPDpeerSettings::Flag;
			const auto business = spec.kind == Kind::Business;
			user->setBarSettings(MTP_peerSettings(
				MTP_flags(business
					? (Flag::f_business_bot_id | Flag::f_business_bot_can_reply)
					: Flag::f_charge_paid_message_stars),
				MTPint(), MTPstring(), MTPint(),
				MTP_long(kFirstPeerId + 3), MTP_string("https://t.me/"),
				MTP_long(5), MTPstring(), MTPstring(), MTPint(), MTPint()));
			if (!business) {
				user->setStarsPerMessage(5);
			}
		}
	} else {
		const auto channel = peer->asChannel();
		using Flag = ChannelDataFlag;
		const auto broadcast = spec.kind == Kind::Broadcast
			|| spec.kind == Kind::Discussion || spec.kind == Kind::Join;
		channel->setName(name, {});
		channel->setAccessHash(0);
		channel->setFlags((broadcast ? Flag::Broadcast : Flag::Megagroup)
			| (spec.kind == Kind::Join ? Flag::Left : Flag())
			| (spec.kind == Kind::Discussion ? Flag::HasLink : Flag())
			| (spec.kind == Kind::Topic ? Flag::Forum : Flag()));
		channel->setLoadedStatus(PeerData::LoadedStatus::Full);
		channel->setBarSettings(PeerBarSettings());
		channel->setMembersCount(16);
		channel->setDefaultRestrictions(spec.kind == Kind::Restricted
			? Data::AllSendRestrictions() : ChatRestrictions());
		if (spec.kind == Kind::Discussion) {
			const auto group = session->data().channel(ChannelId(kFirstPeerId + 8));
			channel->setDiscussionLink(group);
		} else if (spec.kind == Kind::Requests || spec.kind == Kind::Call) {
			channel->setAdminRights(ChatAdminRight::InviteByLinkOrAdd | ChatAdminRight::ProcessJoinRequests);
			channel->setPendingRequestsCount(1,
				std::vector<UserId>{ UserId(kFirstPeerId + 1) });
		} else if (spec.kind == Kind::Topic) {
			channel->forum()->applyTopicAdded(kTopicRootId,
				u"话题输入布局"_q, 0x6FB9F0, 0,
				session->userPeerId(), base::unixtime::now() - 600, true);
		}
		if (spec.kind == Kind::Call) {
			channel->setGroupCall(MTP_inputGroupCall(MTP_long(810001), MTPlong()),
				base::unixtime::now() + 3600);
		}
	}
	const auto pinned = spec.kind == Kind::Broadcast
		|| spec.kind == Kind::Discussion || spec.kind == Kind::Translate
		|| spec.kind == Kind::Requests || spec.kind == Kind::Call
		|| spec.kind == Kind::Pinned;
	fillHistory(session, peer, index, pinned);
	if (spec.kind == Kind::Translate) {
		peer->setTranslationDisabled(false);
		session->data().history(peer)->translateOfferFrom({ QLocale::French });
	}
}

[[nodiscard]] Result listScenarios(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: scenario.list"_q);
	}
	return Result::Ok(Compact(scenarioList()));
}

[[nodiscard]] int findScenario(const QString &key) {
	for (auto i = 0; i != kScenarios.size(); ++i) {
		if (key == QLatin1String(kScenarios[i].key)) {
			return i;
		}
	}
	return -1;
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
	const auto topicId = (kScenarios[index].kind == Kind::Topic && view == u"main"_q)
		? MsgId(kTopicRootId) : MsgId();
	history->clearLocalDraft(topicId, {});
	history->clearLocalEditDraft(topicId, {});
	if (input == u"empty"_q) {
		return;
	}
	auto draft = std::make_unique<Data::Draft>();
	draft->reply = {
		.messageId = FullMsgId(peer->id, kFirstMessageId + 100 * index
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
	if (view == u"shortcuts"_q) {
		auto &messages = session->data().shortcutMessages();
		constexpr auto kShortcutMessageId = kFirstMessageId + 2000;
		messages.apply(MTP_updateQuickReplyMessage(makeMessage(
			session->user(), session->userPeerId(), kShortcutMessageId,
			u"快捷回复样本：检查独立设置页的输入区。"_q,
			false, false, 1)).c_updateQuickReplyMessage());
		messages.apply(MTP_updateQuickReplies(MTP_vector<MTPQuickReply>({
			MTP_quickReply(MTP_int(1), MTP_string("layout"),
				MTP_int(kShortcutMessageId), MTP_int(1)),
		})).c_updateQuickReplies());
		controller->showSettings(Settings::ShortcutMessagesId(1));
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
		// 假会话拿不到服务器日志，只用于检查分区外框与底部按钮。
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
	if (kind == Kind::Topic) {
		// 与在会话列表里点开论坛一致：先在左栏展开话题列表，再进入话题。
		controller->showForum(peer->forum(), Window::SectionShow(way).withChildColumn());
		controller->showTopic(peer->forum()->topicFor(kTopicRootId), ShowAtTheEndMsgId, way);
		return Result::Ok();
	}
	controller->showPeerHistory(peer, way, ShowAtTheEndMsgId);
	return Result::Ok();
}

[[nodiscard]] Result openScenario(const QStringList &args) {
	if (args.empty() || !(args.size() % 2)) {
		return Result::Err(u"usage: scenario.open <key> [--view main|alternate|scheduled|shortcuts|pinned|actions] [--input keep|empty|reply|edit]"_q);
	}
	auto view = u"main"_q;
	auto input = u"keep"_q;
	for (auto i = 1; i < args.size(); i += 2) {
		if (args[i] == u"--view"_q) {
			view = args[i + 1];
		} else if (args[i] == u"--input"_q) {
			input = args[i + 1];
		} else {
			return Result::Err(u"unknown scenario option"_q);
		}
	}
	if (view != u"main"_q && view != u"alternate"_q
		&& view != u"scheduled"_q && view != u"shortcuts"_q
		&& view != u"pinned"_q && view != u"actions"_q) {
		return Result::Err(u"unknown view"_q);
	}
	if (input != u"keep"_q && input != u"empty"_q
		&& input != u"reply"_q && input != u"edit"_q) {
		return Result::Err(u"unknown input state"_q);
	}
	if ((view == u"scheduled"_q || view == u"shortcuts"_q
			|| view == u"pinned"_q || view == u"actions"_q)
		&& input != u"keep"_q) {
		return Result::Err(u"input states require main or alternate view"_q);
	}
	const auto session = ActiveSession();
	if (!session || SeededSession.get() != session) {
		return Result::Err(u"an in-process fake session is required"_q);
	}
	const auto controller = session->tryResolveWindow();
	if (!controller) {
		return Result::Err(u"no window controller"_q);
	}
	const auto index = findScenario(args.front());
	if (index < 0) {
		return Result::Err(u"unknown scenario, use scenario.list"_q);
	}
	const auto kind = kScenarios[index].kind;
	if (input != u"keep"_q && kind != Kind::Private && kind != Kind::Topic) {
		return Result::Err(u"input states require private or topic scenario"_q);
	}
	installScenarioDraft(controller, index, view, input);
	const auto peer = session->data().peer(scenarioPeerId(index));
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

void seedFakeScenarios(not_null<Main::Session*> session) {
	Expects(isFakeSession(session));
	if (SeededSession.get() == session) {
		return;
	}
	for (auto i = 0; i != kScenarios.size(); ++i) {
		seedScenario(session, i);
	}
	SeededSession = base::make_weak(session);
}

MTPMessage fakeTextMessage(not_null<PeerData*> peer, PeerId sender,
		int id, const QString &text, bool edited) {
	return makeMessage(peer, sender, id, text, false, false, 0, false, edited);
}

const HandlerMap &ScenarioHandlers() {
	static const auto result = HandlerMap{
		{ u"scenario.list"_q, &listScenarios },
		{ u"scenario.open"_q, &openScenario },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
