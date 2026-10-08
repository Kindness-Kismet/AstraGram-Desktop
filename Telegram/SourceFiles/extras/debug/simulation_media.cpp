#ifdef _DEBUG
#include "extras/debug/simulation_media.h"

#include "base/unixtime.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_photo.h"
#include "data/data_photo_media.h"
#include "data/data_session.h"
#include "main/main_session.h"
#include "storage/localimageloader.h"
#include "storage/storage_media_prepare.h"

#include <QBuffer>
#include <QPainter>

namespace ExtrasDebug::Commands {

MTPMessageMedia simulationMedia(
		not_null<Main::Session*> session, SimulationMedia kind, int id) {
	if (kind == SimulationMedia::Contact) {
		return MTP_messageMediaContact(MTP_string("+9996600001"),
			MTP_string("模拟联系人"), MTPstring(), MTPstring(), MTP_long(810000001));
	}
	if (kind == SimulationMedia::Poll) {
		const auto text = [](const QString &value) {
			return MTP_textWithEntities(MTP_string(value), MTPVector<MTPMessageEntity>());
		};
		const auto answer = [&](const QString &value, const char *option) {
			return MTP_pollAnswer(MTP_flags(0), text(value), MTP_bytes(option),
				MTPMessageMedia(), MTPPeer(), MTPint());
		};
		const auto poll = MTP_poll(MTP_long(9200000000LL + id),
			MTP_flags(MTPDpoll::Flag::f_closed), text(u"这次优先检查哪部分界面？"_q),
			MTP_vector<MTPPollAnswer>({ answer(u"消息列表"_q, "a"), answer(u"对话列表"_q, "b") }),
			MTPint(), MTPint(), MTPVector<MTPstring>(), MTPlong());
		const auto votes = [](const char *option, int count) {
			return MTP_pollAnswerVoters(MTP_flags(MTPDpollAnswerVoters::Flag::f_voters),
				MTP_bytes(option), MTP_int(count), MTPVector<MTPPeer>());
		};
		const auto results = MTP_pollResults(
			MTP_flags(MTPDpollResults::Flag::f_results | MTPDpollResults::Flag::f_total_voters),
			MTP_vector<MTPPollAnswerVoters>({ votes("a", 7), votes("b", 3) }),
			MTP_int(10), MTPVector<MTPPeer>(), MTPstring(),
			MTPVector<MTPMessageEntity>(), MTPMessageMedia());
		return MTP_messageMediaPoll(MTP_flags(0), poll, results, MTPMessageMedia());
	}
	auto bytes = QByteArray("Local simulation document.\nNo remote download is required.\n");
	auto image = QImage();
	if (kind != SimulationMedia::File) {
		const auto sticker = kind == SimulationMedia::Sticker;
		image = QImage(sticker ? QSize(256, 256) : QSize(480, 300), QImage::Format_ARGB32_Premultiplied);
		image.fill(sticker ? Qt::transparent : QColor(214, 232, 248));
		{
			auto painter = QPainter(&image);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setPen(Qt::NoPen);
			painter.setBrush(QColor(76, 146, 204));
			painter.drawRoundedRect(image.rect().adjusted(24, 24, -24, -24), 36, 36);
			painter.setBrush(QColor(255, 211, 91));
			painter.drawEllipse(QPoint(image.width() / 2, image.height() / 2), 60, 60);
		}
		bytes.clear();
		auto buffer = QBuffer(&bytes);
		const auto encoded = image.save(&buffer, "PNG");
		Expects(encoded);
	}
	if (kind == SimulationMedia::Photo) {
		const auto photo = MTP_photo(MTP_flags(0), MTP_long(9100000000LL + id),
			MTPlong(), MTP_bytes(), MTP_int(base::unixtime::now()),
			MTP_vector<MTPPhotoSize>({ MTP_photoSize(MTP_string("y"),
				MTP_int(image.width()), MTP_int(image.height()), MTP_int(bytes.size())) }),
			MTPVector<MTPVideoSize>(), MTP_int(0));
		const auto data = session->data().processPhoto(photo, PreparedPhotoThumbs{
			{ 'y', PreparedPhotoThumb{ .image = image, .bytes = bytes } },
		});
		auto &view = *session->lifetime().make_state<std::shared_ptr<Data::PhotoMedia>>(
			data->createMediaView());
		view->set(Data::PhotoSize::Large, Data::PhotoSize::Large, image, bytes);
		return MTP_messageMediaPhoto(MTP_flags(MTPDmessageMediaPhoto::Flag::f_photo),
			photo, MTPint(), MTPDocument());
	}
	auto attributes = QVector<MTPDocumentAttribute>{
		MTP_documentAttributeFilename(MTP_string(kind == SimulationMedia::File
			? "example.txt" : "sticker.png")),
	};
	if (kind == SimulationMedia::Sticker) {
		attributes.push_back(MTP_documentAttributeImageSize(MTP_int(256), MTP_int(256)));
		attributes.push_back(MTP_documentAttributeSticker(MTP_flags(0), MTP_string(""),
			MTP_inputStickerSetEmpty(), MTPMaskCoords()));
	}
	const auto document = MTP_document(MTP_flags(0), MTP_long(9300000000LL + id),
		MTPlong(), MTP_bytes(), MTP_int(base::unixtime::now()),
		MTP_string(kind == SimulationMedia::File ? "text/plain" : "image/png"),
		MTP_long(bytes.size()), MTPVector<MTPPhotoSize>(), MTPVector<MTPVideoSize>(),
		MTP_int(0), MTP_vector<MTPDocumentAttribute>(attributes));
	const auto data = session->data().processDocument(document);
	// 固定样本需要跨场景保留，不能只延长到下一次界面事件。
	auto &view = *session->lifetime().make_state<std::shared_ptr<Data::DocumentMedia>>(
		data->createMediaView());
	view->setBytes(bytes);
	if (!image.isNull()) {
		view->setThumbnail(image);
	}
	return MTP_messageMediaDocument(MTP_flags(MTPDmessageMediaDocument::Flag::f_document),
		document, MTPVector<MTPDocument>(), MTPPhoto(), MTPint(), MTPint());
}

} // namespace ExtrasDebug::Commands
#endif // _DEBUG
