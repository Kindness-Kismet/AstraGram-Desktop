#ifdef _DEBUG
#include "ayu/debug/commands/commands_internal.h"

#include "apiwrap.h"
#include "ayu/ayu_settings.h"
#include "ayu/debug/debug_login.h"
#include "ayu/utils/telegram_helpers.h"
#include "api/api_common.h"
#include "core/application.h"
#include "data/data_folder.h"
#include "data/data_msg_id.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "data/data_types.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "ui/text/text_entity.h"
#include "window/window_session_controller.h"

#include "base/unixtime.h"

#include <QFile>

namespace AyuDebug::Commands {
namespace {

using json = nlohmann::json;

// 本地消息 id 从远离服务端空间的正数递增，肉眼可辨且不与真实 id 冲突。
// addNewMessage 的 id 取自 MTPMessage，不能用负数本地 id。
[[nodiscard]] int32 NextFakeMsgId() {
	static auto counter = int32(1000001);
	return counter++;
}

// 与 session.fake 同源的假用户构造，塞进 data() 供 from_id 引用。
[[nodiscard]] not_null<UserData*> FakeUser(
		not_null<Main::Session*> session,
		int64 userId) {
	using Flag = MTPDuser::Flag;
	return session->data().processUser(MTP_user(
		MTP_flags(Flag::f_first_name),
		MTP_long(userId),
		MTPlong(), // access_hash
		MTP_string("Fake"),
		MTPstring(), // last_name
		MTPstring(), // username
		MTPstring(), // phone
		MTPUserProfilePhoto(),
		MTPUserStatus(),
		MTPint(), // bot_info_version
		MTPVector<MTPRestrictionReason>(),
		MTPstring(), // bot_inline_placeholder
		MTPstring(), // lang_code
		MTPEmojiStatus(),
		MTPVector<MTPUsername>(),
		MTPRecentStory(),
		MTPPeerColor(), // color
		MTPPeerColor(), // profile_color
		MTPint(), // bot_active_users
		MTPlong(), // bot_verification_icon
		MTPlong(), // send_paid_messages_stars
		MTPlong())); // linked_community_id
}

// 往假会话的 Saved Messages 塞本地构造的消息，走 addNewMessage 官方路径，
// DocumentData 与缩略图由官方代码解包，渲染行为与真实消息一致。
// 消息只存在内存，重启即消失，不触发任何网络请求。
[[nodiscard]] Result FakeMessage(const QStringList &args) {
	auto text = QString();
	auto fromUserId = int64(0); // 0 = self
	auto blocked = false;
	auto shadowBan = false;
	auto targetPeer = QString();
	for (auto i = 0; i < args.size(); ++i) {
		const auto &arg = args.at(i);
		if (arg == u"--peer"_q) {
			if (++i >= args.size()) return Result::Err(u"usage: --peer <peerId>"_q);
			targetPeer = args.at(i);
		} else if (arg == u"--from"_q) {
			if (++i >= args.size()) {
				return Result::Err(u"usage: --from <userId>"_q);
			}
			auto ok = false;
			fromUserId = args.at(i).toLongLong(&ok);
			if (!ok || fromUserId <= 0) {
				return Result::Err(
					u"expected a positive integer userId"_q);
			}
		} else if (arg == u"--blocked"_q) {
			blocked = true;
		} else if (arg == u"--shadow-ban"_q) {
			shadowBan = true;
		} else if (text.isEmpty()) {
			text = arg;
		} else {
			return Result::Err(
				u"usage: message.fake <text> "
				u"[--peer <peerId>] [--from <userId>] [--blocked] [--shadow-ban]"_q);
		}
	}
	if (text.isEmpty()) {
		return Result::Err(
			u"usage: message.fake <text> "
			u"[--peer <peerId>] [--from <userId>] [--blocked] [--shadow-ban]"_q);
	}

	const auto session = ActiveSession();
	if (!session || !isFakeSession(session)) {
		return Result::Err(u"an in-process fake session is required"_q);
	}
	const auto selfPeer = session->userPeerId();
	const auto peer = targetPeer.isEmpty() ? static_cast<PeerData*>(session->user()) : findPeer(targetPeer);
	if (!peer) return Result::Err(u"peer not found"_q);
	auto fromUser = not_null<UserData*>(session->user());
	if (fromUserId > 0) {
		fromUser = FakeUser(session, fromUserId);
	}

	// --blocked/--shadow-ban 必须配合 --from：self 消息是 out 消息，
	// 过滤链对 out 直接放行，标了也不会隐藏。
	if ((blocked || shadowBan) && fromUserId == 0) {
		return Result::Err(
			u"--blocked/--shadow-ban require --from <userId>"_q);
	}
	if (blocked) {
		fromUser->setIsBlocked(true);
	}
	if (shadowBan) {
		auto &settings = AyuSettings::getInstance();
		settings.addShadowBan(getDialogIdFromPeer(fromUser));
		AyuSettings::save();
	}
	const auto fromPeer = fromUser->id;

	const auto media = MTPMessageMedia();
	const auto flags = MTPDmessage::Flag::f_from_id
		| (fromPeer == selfPeer ? MTPDmessage::Flag::f_out : MTPDmessage::Flag());
	const auto message = MTP_message(
		MTP_flags(flags),
		MTP_int(NextFakeMsgId()),
		peerToMTP(fromPeer), // from_id
		MTPint(), // from_boosts_applied
		MTPstring(), // from_rank
		peerToMTP(peer->id),
		MTPPeer(), // saved_peer_id
		MTPMessageFwdHeader(), // fwd_from
		MTPlong(), // via_bot_id
		MTPlong(), // via_business_bot_id
		MTPPeer(), // guestchat_via_from
		MTPMessageReplyHeader(), // reply_to
		MTP_int(base::unixtime::now()),
		MTP_string(text),
		media,
		MTPReplyMarkup(),
		MTPVector<MTPMessageEntity>(),
		MTPint(), // views
		MTPint(), // forwards
		MTPMessageReplies(),
		MTPint(), // edit_date
		MTPstring(), // post_author
		MTPlong(), // grouped_id
		MTPMessageReactions(),
		MTPVector<MTPRestrictionReason>(),
		MTPint(), // ttl_period
		MTPint(), // quick_reply_shortcut_id
		MTPlong(), // effect
		MTPFactCheck(),
		MTPint(), // report_delivery_until_date
		MTPlong(), // paid_message_stars
		MTPSuggestedPost(),
		MTPint(), // schedule_repeat_period
		MTPstring(), // summary_from_language
		MTPRichMessage());
	const auto item = session->data().addNewMessage(
		message,
		MessageFlags(),
		NewMessageType::Unread);
	if (!item) {
		return Result::Err(u"addNewMessage returned null"_q);
	}
	return Result::Ok(Compact(json{
		{ "msgId", item->id.bare },
		{ "peerId", peer->id.value },
		{ "fromUserId", (fromUserId > 0) ? json(fromUserId) : json(nullptr) },
		{ "blocked", blocked },
		{ "shadowBanned", shadowBan },
		{ "note", "local fake message, no server data" },
	}));
}

// 列出已加载对话的 peerId 与名称，filter 为名称子串，忽略大小写。
// send-message / open-chat 的 peerId 均以本指令输出为准。
[[nodiscard]] Result Chats(const QStringList &args) {
	if (args.size() > 1) {
		return Result::Err(u"usage: chat.list [filter]"_q);
	}
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session"_q);
	}
	const auto filter = args.isEmpty() ? QString() : args.front();
	auto items = json::array();
	for (const auto &row : session->data().chatsList()->indexed()->all()) {
		const auto history = row->history();
		if (!history) {
			continue;
		}
		const auto peer = history->peer;
		if (!filter.isEmpty()
			&& !peer->name().contains(filter, Qt::CaseInsensitive)) {
			continue;
		}
		items.push_back(json{
			{ "peerId", peer->id.value },
			{ "name", peer->name().toStdString() },
			{ "type", peer->isUser() ? "user"
				: peer->isChat() ? "chat"
				: "channel" },
		});
	}
	return Result::Ok(Compact(std::move(items)));
}

// 会话列表里的频道与群只带最小信息，完整数据没加载时 peerLoaded 返回空。
// 所以再按 peerId 在会话列表里找一遍，保证 chat.list 输出的 id 一定可用。
[[nodiscard]] PeerData *ResolvePeer(
		not_null<Main::Session*> session,
		int64 idValue) {
	if (idValue == 0) {
		return session->user();
	}
	const auto id = PeerId(BareId(idValue));
	if (const auto loaded = session->data().peerLoaded(id)) {
		return loaded;
	}
	for (const auto &row : session->data().chatsList()->indexed()->all()) {
		const auto history = row->history();
		if (history && history->peer->id == id) {
			return history->peer;
		}
	}
	return nullptr;
}

// 真实发送文本，走官方发送链路，auto_space 等钩子均生效。
// --file 读文件原样发送，命令行参数按空白切分，带不了换行与引号。
// 仅限本人测试群使用。
[[nodiscard]] Result SendTextMessage(const QStringList &args) {
	if (args.size() < 2) {
		return Result::Err(u"usage: message.send <peerId> <text"
			u" | --file path>"_q);
	}
	auto ok = false;
	const auto peerIdValue = args.front().toLongLong(&ok);
	if (!ok || peerIdValue == 0) {
		return Result::Err(u"expected numeric peerId, run chat.list"_q);
	}
	auto text = QString();
	if (args.size() >= 3 && args[1] == u"--file"_q) {
		const auto path = args[2];
		auto file = QFile(path);
		if (!file.open(QIODevice::ReadOnly)) {
			return Result::Err(u"cannot read file: "_q + path);
		}
		text = QString::fromUtf8(file.readAll());
	} else {
		text = args.mid(1).join(u" "_q);
	}
	if (text.isEmpty()) {
		return Result::Err(u"text must not be empty"_q);
	}
	const auto session = ActiveSession();
	if (!session || isFakeSession(session)) {
		return Result::Err(u"an authenticated session is required"_q);
	}
	const auto peer = ResolvePeer(session, peerIdValue);
	if (!peer) {
		return Result::Err(u"peer not found, run chat.list first"_q);
	}
	auto action = Api::SendAction(session->data().history(peer));
	action.clearDraft = false;
	auto message = Api::MessageToSend(action);
	message.textWithTags = { text, TextWithTags::Tags{} };
	session->api().sendMessage(std::move(message));
	// 长文本不回显正文，避免响应被撑爆
	auto result = json{
		{ "peerId", peer->id.value },
		{ "name", peer->name().toStdString() },
		{ "textLength", text.size() },
	};
	if (text.size() <= 512) {
		result["text"] = text.toStdString();
	}
	return Result::Ok(Compact(result));
}

// 打开对话并清空导航栈；参数取 chat.list 输出的 peerId，
// 不指定会话时打开收藏夹。
[[nodiscard]] Result OpenChat(const QStringList &args) {
	if (args.size() > 1) {
		return Result::Err(u"usage: chat.open [peerId]"_q);
	}
	auto idValue = int64(0);
	if (args.size() == 1) {
		auto ok = false;
		idValue = args.front().toLongLong(&ok);
		if (!ok || idValue == 0) {
			return Result::Err(u"expected a non-zero integer peerId"_q);
		}
	}
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session, run session.fake first"_q);
	}
	const auto controller = session->tryResolveWindow();
	if (!controller) {
		return Result::Err(u"no window controller"_q);
	}
	const auto peer = ResolvePeer(session, idValue);
	if (!peer) {
		return Result::Err(u"peer not found, run chat.list first"_q);
	}
	controller->showPeerHistory(
		peer,
		Window::SectionShow::Way::ClearStack,
		ShowAtTheEndMsgId);
	return Result::Ok(Compact(json{
		{ "peerId", peer->id.value },
		{ "name", peer->name().toStdString() },
		{ "isSelf", peer->isSelf() },
	}));
}

// 直接打开归档文件夹，不走抽屉入口，用于单独验证归档页行为。
[[nodiscard]] Result OpenArchive(const QStringList &args) {
	if (!args.isEmpty()) {
		return Result::Err(u"usage: chat.open-archive"_q);
	}
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session, run session.fake first"_q);
	}
	const auto controller = session->tryResolveWindow();
	if (!controller) {
		return Result::Err(u"no window controller"_q);
	}
	controller->openFolder(session->data().folder(Data::Folder::kId));
	return Result::Ok(Compact(json{
		{ "folderId", Data::Folder::kId },
	}));
}

// 逐个报告指定对话的消息状态，缺省查询收藏夹。
[[nodiscard]] Result HistoryStats(const QStringList &args) {
	if (args.isEmpty()) {
		return Result::Err(u"usage: chat.history-stats <msgId>... [--peer <peerId>]"_q);
	}
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session"_q);
	}
	auto peerId = session->userPeerId();
	auto messageIds = QStringList();
	for (auto i = 0; i < args.size(); ++i) {
		if (args[i] != u"--peer"_q) {
			messageIds.push_back(args[i]);
			continue;
		}
		if (++i == args.size()) {
			return Result::Err(u"expected peerId after --peer"_q);
		}
		const auto peer = findPeer(args[i]);
		if (!peer) {
			return Result::Err(u"peer not found"_q);
		}
		peerId = peer->id;
	}
	if (messageIds.isEmpty()) {
		return Result::Err(u"at least one msgId is required"_q);
	}
	auto items = json::array();
	for (const auto &arg : messageIds) {
		auto ok = false;
		const auto id = arg.toLongLong(&ok);
		if (!ok || id <= 0) {
			return Result::Err(u"expected positive msgId, got "_q + arg);
		}
		auto node = json{
			{ "msgId", id },
		};
		if (const auto item = session->data().message(
					FullMsgId(peerId, MsgId(BareId(id))))) {
			node["exists"] = true;
			node["isRegular"] = item->isRegular();
			node["out"] = item->out();
			node["fromId"] = item->from()->id.value;
			node["hidden"] = isMessageHidden(item);
			node["hasMainView"] = item->mainView() != nullptr;
		} else {
			node["exists"] = false;
		}
		items.push_back(std::move(node));
	}
	return Result::Ok(Compact(std::move(items)));
}

} // namespace

const HandlerMap &MessageHandlers() {
	static const auto result = HandlerMap{
		{ u"message.fake"_q, &FakeMessage },
		{ u"chat.list"_q, &Chats },
		{ u"message.send"_q, &SendTextMessage },
		{ u"chat.open"_q, &OpenChat },
		{ u"chat.open-archive"_q, &OpenArchive },
		{ u"chat.history-stats"_q, &HistoryStats },
	};
	return result;
}

} // namespace AyuDebug::Commands
#endif // _DEBUG
