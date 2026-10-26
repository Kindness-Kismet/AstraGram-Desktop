#ifdef _DEBUG
#include "extras/debug/simulation_music.h"

#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "base/unixtime.h"
#include "base/random.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "data/data_streaming.h"
#include "history/history.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "media/player/media_player_instance.h"
#include "media/streaming/media_streaming_document.h"
#include "media/streaming/media_streaming_loader_local.h"
#include "media/streaming/media_streaming_player.h"
#include "settings.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>

namespace ExtrasDebug::Commands {
namespace {

struct Source {
	QString path;
	Storage::Cache::Key key = { 0x53494D4D55534943ULL, base::RandomValue<uint64>() };
	int requests = 0;
	int requestsAtFull = -1;
};

struct Entry {
	std::shared_ptr<Source> source;
	std::shared_ptr<Media::Streaming::Document> streamed;
	FullMsgId message;
};

struct State {
	base::flat_map<DocumentId, Entry> entries;
	DocumentId last = 0;
};

base::weak_ptr<Main::Session> MusicSession;
State *MusicState = nullptr;

class MusicLoader final : public Media::Streaming::LoaderLocal {
public:
	MusicLoader(std::shared_ptr<Source> source)
	: LoaderLocal(std::make_unique<QFile>(source->path))
	, _source(std::move(source)) {
	}

	Storage::Cache::Key baseCacheKey() const override {
		return _source->key;
	}

	void load(int64 offset) override {
		++_source->requests;
		LoaderLocal::load(offset);
	}

	// 样本通过正式 Reader 复用分块，附件下载不需要网络任务。
	void attachDownloader(not_null<Storage::StreamedFileDownloader*>) override {
	}
	void clearAttachedDownloader() override {
	}

private:
	const std::shared_ptr<Source> _source;
};

} // namespace

std::unique_ptr<Media::Streaming::Loader> musicLoaderForDebug(
		not_null<const DocumentData*> document) {
	if (MusicSession.get() != &document->session()) {
		return nullptr;
	}
	const auto i = MusicState->entries.find(document->id);
	return i == MusicState->entries.end() ? nullptr
		: std::make_unique<MusicLoader>(i->second.source);
}

Result simulateMusic(const QStringList &args) {
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return Result::Err(u"an isolated simulation profile is required"_q);
	}
	if (MusicSession.get() != session) {
		MusicSession = base::make_weak(session);
		MusicState = session->lifetime().make_state<State>();
	}
	if (args.size() > 2 || (args.size() == 2
		&& args[1] != u"protected"_q && args[1] != u"cancelled"_q)) {
		return Result::Err(u"usage: simulation.music [path [protected|cancelled]]"_q);
	}
	if (!args.empty()) {
		const auto file = QFileInfo(args[0]);
		if (!file.isFile() || file.size() <= 0 || file.size() > 64 * 1024 * 1024) {
			return Result::Err(u"expected an audio fixture up to 64 MB"_q);
		}
		const auto index = int(MusicState->entries.size());
		const auto id = DocumentId(9700050000ULL + index);
		const auto messageId = session->data().nextLocalMessageId();
		using Flag = MTPDdocumentAttributeAudio::Flag;
		const auto mtp = MTP_document(MTP_flags(0), MTP_long(id), MTP_long(1),
			MTP_bytes(), MTP_int(base::unixtime::now()), MTP_string("audio/mpeg"),
			MTP_long(file.size()), MTPVector<MTPPhotoSize>(), MTPVector<MTPVideoSize>(),
			MTP_int(1), MTP_vector<MTPDocumentAttribute>({
				MTP_documentAttributeFilename(MTP_string(file.fileName())),
				MTP_documentAttributeAudio(MTP_flags(Flag::f_title | Flag::f_performer),
					MTP_int(12), MTP_string("原始封面样本"), MTP_string("本地测试"), MTPstring()),
			}));
		const auto document = session->data().processDocument(mtp);
		const auto peer = simulationPeer(session, args.contains(u"protected"_q)
			? u"protected-group"_q : u"saved"_q);
		const auto item = session->data().history(peer)->addNewLocalMessage({
			.id = messageId,
			.flags = MessageFlag::Outgoing,
			.from = session->userPeerId(),
			.date = base::unixtime::now(),
		}, document, TextWithEntities{ u"本地音乐播放样本"_q });
		auto &media = *session->lifetime().make_state<std::shared_ptr<Data::DocumentMedia>>(
			document->createMediaView());
		auto thumbnail = QImage(90, 90, QImage::Format_RGB32);
		thumbnail.fill(QColor(35, 68, 76));
		media->setThumbnail(std::move(thumbnail));
		auto &entry = MusicState->entries[id];
		entry.source = std::make_shared<Source>();
		entry.source->path = file.absoluteFilePath();
		entry.message = item->fullId();
		entry.streamed = session->data().streaming().sharedDocument(document, item->fullId());
		entry.streamed->player().fullInCache() | rpl::filter(rpl::mappers::_1)
		| rpl::take(1) | rpl::on_next([source = entry.source] {
			source->requestsAtFull = source->requests;
		}, entry.streamed->player().lifetime());
		if (args.contains(u"cancelled"_q)) {
			document->save(item->fullId(), QString());
			document->cancel();
			Ensures(document->cancelled());
		}
		MusicState->last = id;
		Media::Player::instance()->play(AudioMsgId(document, item->fullId()));
	}
	if (!MusicState->last) {
		return Result::Err(u"no music fixture"_q);
	}
	const auto &entry = MusicState->entries.find(MusicState->last)->second;
	const auto document = session->data().document(MusicState->last);
	return Result::Ok(Compact(nlohmann::json{
		{ "documentId", document->id }, { "peerId", entry.message.peer.value },
		{ "messageId", entry.message.msg.bare }, { "size", document->size },
		{ "sourceRequests", entry.source->requests },
		{ "sourceRequestsAtFull", entry.source->requestsAtFull },
		{ "fullInCache", document->loadedInMediaCache() },
		{ "loading", document->loading() }, { "cancelled", document->cancelled() },
		{ "path", document->filepath(true).toStdString() },
	}));
}

} // namespace ExtrasDebug::Commands
#endif
