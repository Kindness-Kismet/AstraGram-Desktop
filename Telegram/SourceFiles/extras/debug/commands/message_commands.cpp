#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "apiwrap.h"
#include "extras/extras_settings.h"
#include "extras/debug/debug_login.h"
#include "extras/utils/telegram_helpers.h"
#include "api/api_common.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_folder.h"
#include "data/data_msg_id.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "data/data_thread.h"
#include "data/data_types.h"
#include "data/data_user.h"
#include "data/notify/data_notify_settings.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/view/history_view_element.h"
#include "history/view/media/history_view_media.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "platform/platform_notifications_manager.h"
#include "ui/text/text_entity.h"
#include "window/window_session_controller.h"

#include "base/unixtime.h"

#include <QBuffer>
#include <QFile>
#include <QImage>
#include <QtGui/QEnterEvent>
#include <QtGui/QMouseEvent>
#include <QtWidgets/QWidget>

namespace ExtrasDebug::Commands {
namespace {

using json = nlohmann::json;

// 本地消息 id 从远离服务端空间的正数递增，肉眼可辨且不与真实 id 冲突。
// addNewMessage 的 id 取自 MTPMessage，不能用负数本地 id。
[[nodiscard]] int32 nextSimulationMessageId() {
	static auto counter = int32(1000001);
	return counter++;
}

// 与 simulation.enter 同源的模拟用户，放入 data() 供 from_id 引用。
[[nodiscard]] not_null<UserData*> simulationUser(
		not_null<Main::Session*> session,
		int64 userId) {
	using Flag = MTPDuser::Flag;
	return session->data().processUser(MTP_user(
		MTP_flags(Flag::f_first_name),
		MTP_long(userId),
		MTPlong(), // access_hash
		MTP_string("模拟用户"),
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

// 本地图片只放入模拟模式的内存媒体，不上传文件。
[[nodiscard]] std::optional<MTPMessageMedia> simulationStickerMedia(
		not_null<Main::Session*> session,
		const QString &path,
		int32 messageId) {
	const auto image = QImage(path);
	if (image.isNull() || image.width() > 512 || image.height() > 512) {
		return std::nullopt;
	}
	auto bytes = QByteArray();
	auto buffer = QBuffer(&bytes);
	if (!image.save(&buffer, "PNG")) {
		return std::nullopt;
	}
	const auto document = MTP_document(
		MTP_flags(0),
		MTP_long(8000000000LL + messageId),
		MTP_long(0),
		MTP_bytes(),
		MTP_int(base::unixtime::now()),
		MTP_string("image/png"),
		MTP_long(bytes.size()),
		MTP_vector<MTPPhotoSize>(),
		MTPVector<MTPVideoSize>(),
		MTP_int(0),
		MTP_vector<MTPDocumentAttribute>({
			MTP_documentAttributeImageSize(
				MTP_int(image.width()), MTP_int(image.height())),
			MTP_documentAttributeSticker(
				MTP_flags(0), MTP_string(""),
				MTP_inputStickerSetEmpty(), MTPMaskCoords()),
		}));
	const auto data = session->data().processDocument(document);
	if (!data->sticker()) {
		return std::nullopt;
	}
	auto &media = *session->lifetime().make_state<std::shared_ptr<Data::DocumentMedia>>(
		data->createMediaView());
	media->setBytes(bytes);
	media->setThumbnail(image);
	return MTP_messageMediaDocument(
		MTP_flags(MTPDmessageMediaDocument::Flag::f_document),
		document, MTPVector<MTPDocument>(), MTPPhoto(), MTPint(), MTPint());
}

// 走正式消息渲染路径，模拟消息仅存在内存，重启后消失。
[[nodiscard]] Result simulationMessage(const QStringList &args) {
	auto text = QString();
	auto fromUserId = int64(0); // 0 = self
	auto blocked = false;
	auto shadowBan = false;
	auto targetPeer = QString();
	auto stickerPath = QString();
	auto photoPath = QString();
	auto groupId = uint64(0);
	for (auto i = 0; i < args.size(); ++i) {
		const auto &arg = args.at(i);
		if (arg == u"--photo"_q) {
			if (++i >= args.size()) return Result::Err(u"usage: --photo <imagePath>"_q);
			photoPath = args.at(i);
		} else if (arg == u"--group"_q) {
			if (++i >= args.size()) return Result::Err(u"usage: --group <positiveId>"_q);
			auto ok = false;
			groupId = args.at(i).toULongLong(&ok);
			if (!ok || !groupId) return Result::Err(u"expected a positive group ID"_q);
		} else if (arg == u"--sticker"_q) {
			if (++i >= args.size()) return Result::Err(u"usage: --sticker <imagePath>"_q);
			stickerPath = args.at(i);
		} else if (arg == u"--peer"_q) {
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
				u"usage: simulation.message <text> "
				u"[--peer <peerId>] [--from <userId>] [--blocked] [--shadow-ban] "
				u"[--sticker <imagePath> | --photo <imagePath> [--group <positiveId>]]"_q);
		}
	}
	if (text.isEmpty()) {
		return Result::Err(
			u"usage: simulation.message <text> "
			u"[--peer <peerId>] [--from <userId>] [--blocked] [--shadow-ban] "
			u"[--sticker <imagePath> | --photo <imagePath> [--group <positiveId>]]"_q);
	}

	const auto session = ActiveSession();
	if (!session || !isSimulationSession(session)) {
		return Result::Err(u"simulation mode is required"_q);
	}
	if ((!photoPath.isEmpty() && !stickerPath.isEmpty())
		|| (groupId && photoPath.isEmpty())) {
		return Result::Err(u"--photo and --sticker are exclusive; --group requires --photo"_q);
	}
	const auto selfPeer = session->userPeerId();
	const auto peer = targetPeer.isEmpty() ? static_cast<PeerData*>(session->user()) : findPeer(targetPeer);
	if (!peer) return Result::Err(u"peer not found"_q);
	auto fromUser = not_null<UserData*>(session->user());
	if (fromUserId > 0) {
		fromUser = simulationUser(session, fromUserId);
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
		auto &settings = ExtrasSettings::getInstance();
		settings.addShadowBan(getDialogIdFromPeer(fromUser));
		ExtrasSettings::save();
	}
	const auto fromPeer = fromUser->id;

	const auto messageId = nextSimulationMessageId();
	auto media = MTPMessageMedia();
	if (!photoPath.isEmpty()) {
		const auto image = QImage(photoPath);
		if (image.isNull() || image.width() > 2048 || image.height() > 2048) {
			return Result::Err(u"expected a photo up to 2048 pixels per side"_q);
		}
		auto bytes = QByteArray();
		auto buffer = QBuffer(&bytes);
		if (!image.save(&buffer, "JPG")) {
			return Result::Err(u"could not encode the photo"_q);
		}
		const auto photo = MTP_photo(MTP_flags(0),
			MTP_long(9000000000LL + messageId), MTP_long(0), MTP_bytes(),
			MTP_int(base::unixtime::now()),
			MTP_vector<MTPPhotoSize>({ MTP_photoSize(
				MTP_string("y"), MTP_int(image.width()), MTP_int(image.height()),
				MTP_int(bytes.size())) }),
			MTPVector<MTPVideoSize>(), MTP_int(0));
		const auto data = session->data().processPhoto(photo, PreparedPhotoThumbs{
			{ 'y', PreparedPhotoThumb{ .image = image, .bytes = bytes } },
		});
		auto &view = *session->lifetime().make_state<std::shared_ptr<Data::PhotoMedia>>(
			data->createMediaView());
		view->set(Data::PhotoSize::Large, Data::PhotoSize::Large, image, bytes);
		media = MTP_messageMediaPhoto(
			MTP_flags(MTPDmessageMediaPhoto::Flag::f_photo), photo, MTPint(), MTPDocument());
	}
	if (!stickerPath.isEmpty()) {
		const auto sticker = simulationStickerMedia(session, stickerPath, messageId);
		if (!sticker) {
			return Result::Err(u"expected a valid sticker image up to 512 pixels per side"_q);
		}
		media = *sticker;
	}
	const auto flags = MTPDmessage::Flag::f_from_id
		| ((stickerPath.isEmpty() && photoPath.isEmpty())
			? MTPDmessage::Flag() : MTPDmessage::Flag::f_media)
		| (groupId ? MTPDmessage::Flag::f_grouped_id : MTPDmessage::Flag())
		| (fromPeer == selfPeer ? MTPDmessage::Flag::f_out : MTPDmessage::Flag());
	const auto message = MTP_message(
		MTP_flags(flags),
		MTP_int(messageId),
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
		MTP_long(groupId),
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
		{ "note", "local simulation message, no server data" },
	}));
}

// 模拟模式没有服务端下发的通知设置，通知会被判为“未知”而跳过；
// 先本地标记为已知且未静音，再借 simulation.message 触发真实的通知链路。
[[nodiscard]] Result notificationTest(const QStringList &args) {
	auto text = u"Debug 通知测试"_q;
	auto userId = int64(830000001);
	for (auto i = 0; i < args.size(); ++i) {
		if (args.at(i) != u"--peer"_q) {
			text = args.at(i);
			continue;
		}
		auto ok = false;
		userId = (++i < args.size()) ? args.at(i).toLongLong(&ok) : 0;
		if (!ok || userId <= 0) {
			return Result::Err(
				u"usage: notification.test [text] [--peer <userId>]"_q);
		}
	}

	const auto session = ActiveSession();
	if (!session || !isSimulationSession(session)) {
		return Result::Err(u"simulation mode is required"_q);
	}
	// 必须显式带 mute_until，缺省会被当成静音。
	const auto known = MTP_peerNotifySettings(
		MTP_flags(MTPDpeerNotifySettings::Flag::f_mute_until),
		MTPBool(),
		MTPBool(),
		MTP_int(0),
		MTPNotificationSound(),
		MTPNotificationSound(),
		MTPNotificationSound(),
		MTPBool(),
		MTPBool(),
		MTPNotificationSound(),
		MTPNotificationSound(),
		MTPNotificationSound());
	const auto user = simulationUser(session, userId);
	auto &notify = session->data().notifySettings();
	notify.apply(user, known);
	notify.apply(Data::DefaultNotify::User, known);

	const auto id = QString::number(userId);
	const auto result = simulationMessage({ text, u"--peer"_q, id, u"--from"_q, id });
	if (!result.ok) {
		return result;
	}

	// 返回通知的各项判断结果，便于定位未弹出的原因。
	auto payload = json::parse(result.payload.toStdString());
	const auto msgId = QString::number(payload["msgId"].get<int64>());
	const auto item = findMessage(id, msgId);
	Expects(item != nullptr);
	const auto thread = item->notificationThread();
	const auto &settings = Core::App().settings();
	payload["unread"] = item->unread(thread);
	payload["showNotification"] = item->showNotification();
	payload["hasNotification"] = thread->hasNotification();
	payload["muteUnknown"] = notify.muteUnknown(thread);
	payload["muted"] = notify.isMuted(thread);
	payload["desktopNotify"] = settings.desktopNotify();
	payload["nativeNotifications"] = settings.nativeNotifications();
	payload["nativeSupported"] = Platform::Notifications::Supported();
	payload["managerType"] = int(Core::App().notifications().manager().type());
	payload["activeAccount"] = (&session->account()
		== &Core::App().domain().active());
	payload["notifyFromAll"] = settings.notifyFromAll();
	return Result::Ok(Compact(payload));
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
	if (!session || isSimulationSession(session)) {
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
		return Result::Err(u"no active session, run simulation.enter first"_q);
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
		return Result::Err(u"no active session, run simulation.enter first"_q);
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
		const auto item = session->data().message(
			FullMsgId(peerId, MsgId(BareId(id))));
		if (!item) {
			items.push_back({ { "msgId", id }, { "exists", false } });
			continue;
		}
		const auto view = item->mainView();
		const auto media = view ? view->media() : nullptr;
		items.push_back({
			{ "msgId", id },
			{ "exists", true },
			{ "isRegular", item->isRegular() },
			{ "out", item->out() },
			{ "fromId", item->from()->id.value },
			{ "hidden", isMessageHidden(item) },
			{ "hasMainView", item->mainView() != nullptr },
			{ "mediaSize", media ? json{
				{ "width", media->width() },
				{ "height", media->height() },
			} : json(nullptr) },
		});
	}
	return Result::Ok(Compact(std::move(items)));
}

// 给自绘通知窗口发合成的进入／离开事件，不移动真实光标。
[[nodiscard]] Result notificationHover(const QStringList &args) {
	if (args.size() != 1 || (args[0] != u"on"_q && args[0] != u"off"_q)) {
		return Result::Err(u"usage: notification.hover <on|off>"_q);
	}
	const auto enter = (args[0] == u"on"_q);
	const auto windows = notificationWindows();
	for (const auto widget : windows) {
		const auto center = widget->rect().center();
		auto entered = QEnterEvent(
			QPointF(center),
			QPointF(center),
			QPointF(widget->mapToGlobal(center)));
		auto left = QEvent(QEvent::Leave);
		QCoreApplication::sendEvent(
			widget.get(),
			enter ? static_cast<QEvent*>(&entered) : &left);
	}
	return Result::Ok(Compact(json{
		{ "count", int(windows.size()) },
	}));
}

// 按固定标识向可见的通知按钮投递合成点击。
[[nodiscard]] Result notificationClick(const QStringList &args) {
	if (args.size() != 1 || (args[0] != u"reply"_q && args[0] != u"close"_q)) {
		return Result::Err(u"usage: notification.click <reply|close>"_q);
	}
	const auto name = u"notification."_q + args[0];
	auto targets = std::vector<not_null<QWidget*>>();
	for (const auto window : notificationWindows()) {
		const auto button = window->findChild<QWidget*>(
			name,
			Qt::FindDirectChildrenOnly);
		if (!button || !button->isVisible() || !button->isEnabled()) {
			continue;
		}
		targets.push_back(button);
	}
	if (targets.empty()) {
		return Result::Err(u"no visible notification action button"_q);
	}
	for (const auto button : targets) {
		const auto center = button->rect().center();
		const auto local = QPointF(center);
		const auto global = QPointF(button->mapToGlobal(center));
		auto entered = QEnterEvent(local, local, global);
		auto press = QMouseEvent(
			QEvent::MouseButtonPress,
			local,
			global,
			Qt::LeftButton,
			Qt::LeftButton,
			Qt::NoModifier);
		auto release = QMouseEvent(
			QEvent::MouseButtonRelease,
			local,
			global,
			Qt::LeftButton,
			Qt::NoButton,
			Qt::NoModifier);
		QCoreApplication::sendEvent(button.get(), &entered);
		QCoreApplication::sendEvent(button.get(), &press);
		QCoreApplication::sendEvent(button.get(), &release);
	}
	return Result::Ok(Compact(json{
		{ "count", int(targets.size()) },
	}));
}

} // namespace

const HandlerMap &MessageHandlers() {
	static const auto result = HandlerMap{
		{ u"simulation.message"_q, &simulationMessage },
		{ u"notification.test"_q, &notificationTest },
		{ u"notification.hover"_q, &notificationHover },
		{ u"notification.click"_q, &notificationClick },
		{ u"chat.list"_q, &Chats },
		{ u"message.send"_q, &SendTextMessage },
		{ u"chat.open"_q, &OpenChat },
		{ u"chat.open-archive"_q, &OpenArchive },
		{ u"chat.history-stats"_q, &HistoryStats },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
