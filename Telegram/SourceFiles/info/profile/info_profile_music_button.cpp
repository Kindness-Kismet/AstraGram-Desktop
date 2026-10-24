/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_music_button.h"

#include "data/data_document.h"
#include "data/data_document_media.h"
#include "data/data_file_origin.h"
#include "data/data_session.h"
#include "extras/ui/utils/itunes_search.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "ui/image/image.h"
#include "ui/painter.h"
#include "styles/style_info.h"
#include "styles/style_chat.h"

#ifdef _DEBUG
#include "extras/debug/debug_login.h"
#endif

namespace Info::Profile {

MusicButton::MusicButton(
	QWidget *parent,
	MusicButtonData data,
	Fn<void()> handler)
: RippleButton(parent, st::infoMusicButtonRipple) {
	setObjectName(u"profile.attachments.music"_q);
	updateData(std::move(data));
	setClickedCallback(std::move(handler));
}

MusicButton::~MusicButton() = default;

void MusicButton::updateData(MusicButtonData data) {
	_title.setText(st::semiboldTextStyle, data.title);
	_performer.setText(st::defaultTextStyle, data.performer);
	setAccessibleName(data.name.textWithEntities().text);
	setToolTip(data.title + QChar('\n') + data.performer);
	loadCover(data);
	update();
}

rpl::producer<std::optional<QColor>> MusicButton::coverColorValue() const {
	return _coverColor.value();
}

void MusicButton::setCover(QImage image) {
	_cover = std::move(image);
	_coverColor = _cover.isNull()
		? std::nullopt
		: std::make_optional(_cover.scaled(
			1, 1, Qt::IgnoreAspectRatio, Qt::SmoothTransformation).pixelColor(0, 0));
	update();
}

void MusicButton::loadCover(const MusicButtonData &data) {
	_downloadLifetime.destroy();
	const auto generation = ++_coverGeneration;
	_mediaView = data.mediaView;
	setCover({});
	if (!_mediaView) {
		return;
	}
	if (const auto thumbnail = _mediaView->thumbnail()) {
		setCover(thumbnail->original());
		return;
	}
	const auto document = _mediaView->owner();
	if (document->isSongWithCover()
		&& document->session().settings().autoDownload().shouldDownload(
			Data::AutoDownload::Source::User,
			Data::AutoDownload::Type::File,
			document->size)) {
		_mediaView->thumbnailWanted(Data::FileOrigin(data.msgId));
		document->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			if (const auto thumbnail = _mediaView->thumbnail()) {
				setCover(thumbnail->original());
			}
		}, _downloadLifetime);
	}
#ifdef _DEBUG
	if (ExtrasDebug::isSimulationSession(&document->session())) {
		return;
	}
#endif
	// 工作线程只处理图片和文字，界面与文档状态留在主线程。
	const auto weak = base::make_weak(this);
	crl::async([weak, generation, title = data.title, performer = data.performer] {
		auto image = Extras::Ui::Itunes::FetchCover(performer, title, 96);
		crl::on_main([weak, generation, image = std::move(image)]() mutable {
			const auto strong = weak.get();
			if (strong && strong->_coverGeneration == generation
				&& strong->_cover.isNull() && !image.isNull()) {
				strong->setCover(std::move(image));
			}
		});
	});
}

void MusicButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	paintRipple(p, QPoint());
	const auto size = style::ConvertScale(48);
	const auto top = (height() - size) / 2;
	const auto coverRect = QRect(0, top, size, size);
	auto path = QPainterPath();
	path.addRoundedRect(coverRect, style::ConvertScale(9), style::ConvertScale(9));
	p.save();
	p.setClipPath(path);
	if (_cover.isNull()) {
		p.fillRect(coverRect, st::windowBgOver);
		st::infoIconMediaAudio.paintInCenter(p, coverRect);
	} else {
		const auto side = std::min(_cover.width(), _cover.height());
		p.drawImage(coverRect, _cover, QRect(
			(_cover.width() - side) / 2, (_cover.height() - side) / 2, side, side));
	}
	p.restore();
	const auto &arrow = st::topicButtonArrow;
	const auto left = size + style::ConvertScale(12);
	const auto available = std::max(width() - left - arrow.width()
		- style::ConvertScale(12), 0);
	const auto titleTop = (height() - st::semiboldFont->height
		- st::normalFont->height - style::ConvertScale(3)) / 2;
	p.setPen(st::windowBoldFg);
	_title.draw(p, {
		.position = { left, titleTop },
		.availableWidth = available,
		.elisionLines = 1,
	});
	p.setPen(st::windowSubTextFg);
	_performer.draw(p, {
		.position = { left, titleTop + st::semiboldFont->height + style::ConvertScale(3) },
		.availableWidth = available,
		.elisionLines = 1,
	});
	arrow.paint(p, width() - arrow.width(), (height() - arrow.height()) / 2,
		width(), st::windowSubTextFg->c);
}

int MusicButton::resizeGetHeight(int newWidth) {
	return style::ConvertScale(48);
}

} // namespace Info::Profile
