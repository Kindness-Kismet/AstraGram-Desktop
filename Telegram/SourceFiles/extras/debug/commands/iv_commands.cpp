#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "core/application.h"
#include "api/api_common.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "extras/debug/debug_login.h"
#include "extras/debug/simulation_media.h"
#include "history/history.h"
#include "history/history_item.h"
#include "iv/editor/iv_editor_session.h"
#include "iv/iv_controller.h"
#include "iv/iv_instance.h"
#include "iv/iv_rich_page.h"
#include "main/main_session.h"
#include "settings.h"
#include "window/window_session_controller.h"

#include <QtGui/QKeyEvent>
#include <QtWidgets/QApplication>

namespace ExtrasDebug::Commands {
namespace {

Result openFile(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: iv.open-file <path>"_q);
	}
	if (!Core::App().iv().showMarkdown(args.front())) {
		return Result::Err(u"file could not be opened as a Markdown article"_q);
	}
	return Result::Ok();
}

Result openMediaSample(const QStringList &args) {
	if (args.size() != 3) {
		return Result::Err(u"usage: iv.open-media-sample <first-photo> <second-photo> <video>"_q);
	}
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto peer = session->user();
	const auto controller = session->tryResolveWindow(peer);
	if (!controller) {
		return Result::Err(u"no session window"_q);
	}
	static auto nextId = int32(2800000);
	const auto messageId = nextId;
	nextId += 3;
	const auto first = simulationPhotoMedia(session, args[0], messageId);
	const auto second = simulationPhotoMedia(session, args[1], messageId + 1);
	if (!first || !second) {
		return Result::Err(u"expected two readable photos up to 2048 pixels per side"_q);
	}
	const auto video = simulationVideoMedia(session, args[2], messageId + 2);
	if (!video) {
		return Result::Err(u"expected a readable video file"_q);
	}
	const auto firstPhoto = *first->c_messageMediaPhoto().vphoto();
	const auto secondPhoto = *second->c_messageMediaPhoto().vphoto();
	const auto document = *video->c_messageMediaDocument().vdocument();
	const auto caption = [](const QString &text) {
		return MTP_pageCaption(MTP_textPlain(MTP_string(text)), MTP_textEmpty());
	};
	const auto photoBlock = [&](const MTPPhoto &photo, const QString &text) {
		return MTP_pageBlockPhoto(MTP_flags(0), photo.c_photo().vid(),
			caption(text), MTPstring(), MTPlong());
	};
	const auto firstBlock = photoBlock(firstPhoto, u"蓝色横图"_q);
	const auto secondBlock = photoBlock(secondPhoto, u"橙色竖图"_q);
	const auto videoBlock = MTP_pageBlockVideo(MTP_flags(0), document.c_document().vid(),
		caption(u"四秒本地视频"_q));
	const auto group = MTP_vector<MTPPageBlock>({ firstBlock, secondBlock, videoBlock });
	// 完整富消息沿正式文章入口打开，不请求服务端或发送消息。
	const auto richMessage = MTPRichMessage(MTP_richMessage(MTP_flags(0),
		MTP_vector<MTPPageBlock>({
			MTP_pageBlockTitle(MTP_textPlain(MTP_string(u"文章媒体模拟验收"_q))),
			MTP_pageBlockParagraph(MTP_textPlain(MTP_string(
				u"以下包含独立图片、视频、拼图与幻灯片，全部来自本地文件。"_q))),
			firstBlock,
			secondBlock,
			videoBlock,
			MTP_pageBlockCollage(group, caption(u"图片与视频拼图"_q)),
			MTP_pageBlockSlideshow(group, caption(u"图片与视频幻灯片"_q)),
		}),
		MTP_vector<MTPPhoto>({ firstPhoto, secondPhoto }),
		MTP_vector<MTPDocument>({ document })));
	const auto item = session->data().addNewMessage(
		simulationTextMessage(peer, peer->id, messageId, u"文章媒体模拟验收"_q),
		MessageFlags(),
		NewMessageType::Unread);
	Expects(item != nullptr);
	item->updateSentContent({}, nullptr, &richMessage);
	const auto page = item->richPage();
	Expects(page && !page->part);
	Core::App().iv().showRichMessage(controller, item);
	return Result::Ok(Compact({
		{ "peerId", peer->id.value },
		{ "messageId", item->id.bare },
		{ "photoIds", { firstPhoto.c_photo().vid().v, secondPhoto.c_photo().vid().v } },
		{ "documentId", document.c_document().vid().v },
		{ "window", "iv.preview" },
		{ "blocks", page->blocks.size() },
		{ "complete", !page->part },
		{ "localOnly", true },
	}));
}

Result openSite(const QStringList &args) {
	if (args.size() != 1 || !Iv::Controller::IsGoodTonSiteUrl(args.front())) {
		return Result::Err(u"usage: iv.open-site <ton-site-url>"_q);
	}
	Core::App().iv().showTonSite(args.front());
	return Result::Ok();
}

Result openEditor(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: iv.open-editor"_q);
	}
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto peer = session->user();
	const auto controller = session->tryResolveWindow(peer);
	if (!controller) {
		return Result::Err(u"no session window"_q);
	}
	Iv::Editor::ShowComposeBox(
		controller,
		peer,
		Api::SendAction(session->data().history(peer)),
		{},
		{},
		nullptr,
		{ .scope = Iv::Editor::ComposeBoxOptions::Scope::Detached });
	return Result::Ok();
}

Result toggleSearch(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: iv.toggle-search"_q);
	}
	const auto window = controlWindow();
	if (!window || (window->objectName() != u"iv.preview"_q
		&& window->objectName() != u"iv.editor"_q)
		|| !window->isActiveWindow()) {
		return Result::Err(u"select and activate an article preview or editor window"_q);
	}
	// 文章窗口在快捷键覆盖事件中处理搜索，沿原有入口切换。
	auto event = QKeyEvent(QEvent::ShortcutOverride, Qt::Key_F, Qt::ControlModifier);
	event.ignore();
	QApplication::sendEvent(window, &event);
	return event.isAccepted()
		? Result::Ok()
		: Result::Err(u"article search shortcut was not handled"_q);
}

} // namespace

const HandlerMap &ivHandlers() {
	static const auto handlers = HandlerMap{
		{ u"iv.open-file"_q, &openFile },
		{ u"iv.open-media-sample"_q, &openMediaSample },
		{ u"iv.open-site"_q, &openSite },
		{ u"iv.open-editor"_q, &openEditor },
		{ u"iv.toggle-search"_q, &toggleSearch },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
