#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"

#include "data/data_session.h"
#include "data/data_stories.h"
#include "data/data_story_drafts.h"
#include "data/data_user.h"
#include "extras/debug/debug_login.h"
#include "main/main_session.h"
#include "media/stories/media_stories_post.h"
#include "window/window_session_controller.h"

#include <QFileInfo>

namespace ExtrasDebug::Commands {
namespace {

using Json = nlohmann::json;

Result openFile(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: stories.open-file <path>"_q);
	}
	const auto session = ActiveSession();
	if (!session || isSimulationSession(session)) {
		return Result::Err(u"an authenticated account is required"_q);
	}
	const auto controller = session->tryResolveWindow(session->user());
	if (!controller) {
		return Result::Err(u"no session window"_q);
	}
	const auto file = QFileInfo(args.front());
	if (!file.isFile() || !file.isReadable()) {
		return Result::Err(u"expected a readable local media file"_q);
	}
	Media::Stories::PostStoryWithFile(
		controller,
		session->user(),
		file.absoluteFilePath());
	return Result::Ok(Compact(Json{ { "state", "requested" } }));
}

Result openDraft(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: stories.open-draft <id>"_q);
	}
	const auto session = ActiveSession();
	if (!session || isSimulationSession(session)) {
		return Result::Err(u"an authenticated account is required"_q);
	}
	const auto controller = session->tryResolveWindow(session->user());
	if (!controller) {
		return Result::Err(u"no session window"_q);
	}
	auto ok = false;
	const auto id = args.front().toULongLong(&ok);
	if (!ok || !session->data().stories().drafts().find(id)) {
		return Result::Err(u"story draft not found"_q);
	}
	Media::Stories::PostStoryDraft(controller, id);
	return Result::Ok(Compact(Json{ { "state", "requested" }, { "id", id } }));
}

Result drafts(const QStringList &args) {
	if (!args.empty()) {
		return Result::Err(u"usage: stories.drafts"_q);
	}
	const auto session = ActiveSession();
	if (!session) {
		return Result::Err(u"no active session"_q);
	}
	auto result = Json::array();
	for (const auto &draft : session->data().stories().drafts().list()) {
		const auto rect = draft.edits.canvasMediaRect;
		result.push_back(Json{
			{ "id", draft.id },
			{ "peerId", draft.peer.value },
			{ "video", draft.video },
			{ "caption", draft.caption.text.toStdString() },
			{ "period", draft.period },
			{ "duration", draft.duration },
			{ "dimensions", { draft.dimensions.width(), draft.dimensions.height() } },
			{ "mediaExists", QFileInfo::exists(draft.mediaPath) },
			{ "sceneItems", draft.scene.size() },
			{ "sceneFiles", draft.sceneFiles.size() },
			{ "edits", {
				{ "angle", draft.edits.angle },
				{ "flipped", draft.edits.flipped },
				{ "from", draft.edits.from },
				{ "till", draft.edits.till },
				{ "cover", draft.edits.cover },
				{ "canvasMediaRect", { rect.x(), rect.y(), rect.width(), rect.height() } },
			} },
			{ "audio", draft.audioPath.isEmpty() ? Json(nullptr) : Json{
				{ "duration", draft.audio.duration },
				{ "from", draft.audio.from },
				{ "till", draft.audio.till },
				{ "volume", draft.audio.volume },
				{ "selected", draft.audio.selected },
			} },
		});
	}
	return Result::Ok(Compact(Json{
		{ "peerId", session->userPeerId().value },
		{ "drafts", std::move(result) },
	}));
}

} // namespace

const HandlerMap &storyHandlers() {
	static const auto handlers = HandlerMap{
		{ u"stories.open-file"_q, &openFile },
		{ u"stories.open-draft"_q, &openDraft },
		{ u"stories.drafts"_q, &drafts },
	};
	return handlers;
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
