#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "editor/editor_paint.h"
#include "editor/editor_crop.h"
#include "editor/photo_editor.h"
#include "editor/video/video_timeline.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "data/data_peer.h"
#include "data/data_photo.h"
#include "data/data_session.h"
#include "history/history_item.h"
#include "extras/debug/debug_login.h"
#include "settings.h"

#include <QApplication>
#include <QFileInfo>
#include <QGraphicsScene>
#include <QGraphicsView>
#include <QMimeData>
#include <QUrl>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;

[[nodiscard]] Editor::Paint *activePaint() {
	for (const auto widget : QApplication::allWidgets()) {
		const auto editor = dynamic_cast<Editor::PhotoEditor*>(widget);
		if (!editor || !editor->isVisible()) {
			continue;
		}
		for (const auto child : editor->findChildren<QWidget*>()) {
			if (const auto paint = dynamic_cast<Editor::Paint*>(child)) {
				return paint;
			}
		}
	}
	return nullptr;
}

[[nodiscard]] bool isolatedSimulation() {
	const auto session = ActiveSession();
	return cDebugProfile() && session && isSimulationSession(session);
}

[[nodiscard]] QRect cropRect(not_null<Editor::Paint*> paint) {
	for (const auto child : paint->parentWidget()->children()) {
		if (const auto crop = dynamic_cast<Editor::Crop*>(child)) {
			return crop->cropRect();
		}
	}
	Unexpected("Photo editor crop is missing.");
}

Result editorState(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: editor.state"_q);
	}
	if (!isolatedSimulation()) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto paint = activePaint();
	if (!paint) {
		return Result::Err(u"no active photo editor"_q);
	}
	const auto view = paint->findChild<QGraphicsView*>();
	Expects(view != nullptr);
	const auto audio = paint->audio();
	const auto crop = cropRect(paint);
	return Result::Ok(Compact(Json{
		{ "paintMode", !paint->testAttribute(Qt::WA_TransparentForMouseEvents) },
		{ "sceneItems", view->scene()->items().size() },
		{ "crop", { crop.x(), crop.y(), crop.width(), crop.height() } },
		{ "durationsLinked", paint->durationsLinked() },
		{ "audio", audio ? Json{
			{ "filename", QFileInfo(audio->path).fileName().toStdString() },
			{ "duration", audio->duration },
			{ "from", audio->from },
			{ "till", audio->till },
			{ "length", audio->length() },
			{ "volume", audio->volume },
			{ "selected", paint->audioSelected() },
		} : Json(nullptr) },
	}));
}

Result videoEditorState(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: editor.video-state"_q);
	}
	if (!isolatedSimulation()) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	for (const auto widget : QApplication::allWidgets()) {
		const auto timeline = dynamic_cast<Editor::VideoTimeline*>(widget);
		if (!timeline || !timeline->isVisible()) {
			continue;
		}
		return Result::Ok(Compact(Json{
			{ "from", timeline->from() },
			{ "till", timeline->till() },
			{ "cover", timeline->cover() },
			{ "playback", timeline->playbackPosition() },
			{ "zoom", timeline->zoom() },
			{ "visibleFrom", timeline->visibleFrom() },
			{ "visibleTill", timeline->visibleTill() },
		}));
	}
	return Result::Err(u"no visible video timeline"_q);
}

Result addEditorFile(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: editor.add-file <path>"_q);
	}
	if (!isolatedSimulation()) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto paint = activePaint();
	if (!paint || paint->testAttribute(Qt::WA_TransparentForMouseEvents)) {
		return Result::Err(u"open the photo editor paint tools first"_q);
	}
	const auto file = QFileInfo(args.front());
	if (!file.isFile() || !file.isReadable()) {
		return Result::Err(u"expected a readable local file"_q);
	}
	auto mime = QMimeData();
	mime.setUrls({ QUrl::fromLocalFile(file.absoluteFilePath()) });
	if (!paint->canHandleMimeData(&mime)) {
		return Result::Err(u"the editor does not accept this media file"_q);
	}
	paint->handleMimeData(&mime);
	return Result::Ok(Compact(Json{ { "state", "submitted" } }));
}

// 与查看器的绘图按钮发出同一个请求，在当前窗口打开图片编辑器。
Result openEditor(const QStringList &args) {
	if (args.size() != 2) {
		return Result::Err(u"usage: editor.open <peerId> <messageId>"_q);
	}
	if (!isolatedSimulation()) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	const auto peer = findPeer(args[0]);
	if (!peer) {
		return Result::Err(u"peer not found"_q);
	}
	const auto item = peer->owner().message(peer->id, MsgId(args[1].toLongLong()));
	const auto media = item ? item->media() : nullptr;
	if (!media) {
		return Result::Err(u"message with media not found"_q);
	}
	const auto photo = media->photo();
	const auto document = media->document();
	const auto photoId = photo ? photo->id : PhotoId();
	const auto documentId = (document && document->isImage())
		? document->id
		: DocumentId();
	if (!photoId && !documentId) {
		return Result::Err(u"message media is not an image"_q);
	}
	peer->owner().requestDrawToReply({ item->fullId(), photoId, documentId });
	return Result::Ok(Compact(Json{ { "state", "requested" } }));
}

} // namespace

const HandlerMap &editorHandlers() {
	static const auto handlers = HandlerMap{
		{ u"editor.state"_q, &editorState },
		{ u"editor.video-state"_q, &videoEditorState },
		{ u"editor.add-file"_q, &addEditorFile },
		{ u"editor.open"_q, &openEditor },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
