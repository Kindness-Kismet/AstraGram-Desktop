/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "overview/overview_layout.h"

#include "extras/features/music_player/music_player_download.h"

#include "overview/overview_checkbox.h"
#include "overview/overview_layout_delegate.h"
#include "core/application.h"
#include "core/click_handler_types.h"
#include "core/ui_integration.h" // TextContext
#include "data/data_document.h"
#include "data/data_document_resolver.h"
#include "data/data_session.h"
#include "data/data_web_page.h"
#include "data/data_peer.h"
#include "data/data_photo_media.h"
#include "data/data_document_media.h"
#include "data/data_file_click_handler.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/file_preview.h"
#include "lang/lang_keys.h"
#include "layout/layout_selection.h"
#include "storage/file_upload.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "media/player/media_player_instance.h"
#include "storage/localstorage.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/history_item_helpers.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/media/history_view_media_common.h"
#include "history/view/media/history_view_document.h" // DrawThumbnailAsSongCover
#include "iv/iv_instance.h"
#include "base/unixtime.h"
#include "boxes/sticker_set_box.h"
#include "ui/effects/round_checkbox.h"
#include "ui/image/image.h"
#include "ui/text/format_song_document_name.h"
#include "ui/text/format_values.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/cached_round_corners.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_chat_style.h"
#include "styles/style_overview.h"

namespace Overview::Layout {
namespace {

using TextState = HistoryView::TextState;

TextParseOptions _documentNameOptions = {
	TextParseMultiline | TextParseLinks | TextParseMarkdown, // flags
	0, // maxw
	0, // maxh
	Qt::LayoutDirectionAuto, // dir
};

[[nodiscard]] bool ItemPlaying(
		not_null<DocumentData*> document,
		FullMsgId context,
		AudioMsgId::Type type) {
	const auto state = Media::Player::instance()->getState(type);
	return (state.id
			== AudioMsgId(document, context, state.id.externalPlayId()))
		&& !Media::Player::IsStoppedOrStopping(state.state);
}

[[nodiscard]] ClickHandlerPtr MakeUrlHandler(const QString &url) {
	return UrlClickHandler::IsSuspicious(url)
		? std::make_shared<HiddenUrlClickHandler>(url)
		: std::make_shared<UrlClickHandler>(url, false);
}

void OpenPreferringIv(
		not_null<Window::SessionController*> controller,
		WebPageData *page,
		const TextWithEntities &text,
		const QString &url,
		QVariant context) {
	if (page && page->iv && UrlMatchesWebPage(page, url)) {
		const auto hash = ExtractHash(page, text);
		Core::App().iv().show(controller, page->iv.get(), hash);
	} else {
		Core::App().iv().openWithIvPreferred(controller, url, context);
	}
}

class IvLink final : public UrlClickHandler {
public:
	IvLink(WebPageData *page, TextWithEntities text, const QString &url)
	: UrlClickHandler(url, false)
	, _page(page)
	, _text(std::move(text)) {
	}

	void onClick(ClickContext context) const override {
		const auto button = context.button;
		if (button != Qt::LeftButton && button != Qt::MiddleButton) {
			return;
		}
		const auto my = context.other.value<ClickHandlerContext>();
		if (const auto controller = my.sessionWindow.get()) {
			OpenPreferringIv(
				controller,
				_page,
				_text,
				originalUrl(),
				context.other);
		}
	}

private:
	WebPageData *_page = nullptr;
	TextWithEntities _text;

};

} // namespace

ItemBase::ItemBase(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent)
: _delegate(delegate)
, _parent(parent)
, _dateTime(ItemDateTime(parent)) {
}

ItemBase::~ItemBase() = default;

QDateTime ItemBase::dateTime() const {
	return _dateTime;
}

void ItemBase::clickHandlerActiveChanged(
		const ClickHandlerPtr &action,
		bool active) {
	_parent->history()->session().data().requestItemRepaint(_parent);
	if (_check) {
		_check->setActive(active);
	}
}

void ItemBase::clickHandlerPressedChanged(
		const ClickHandlerPtr &action,
		bool pressed) {
	_parent->history()->session().data().requestItemRepaint(_parent);
	if (_check) {
		_check->setPressed(pressed);
	}
}

void ItemBase::invalidateCache() {
	if (_check) {
		_check->invalidateCache();
	}
}

bool ItemBase::selectionConsumesClick(QPoint) const {
	return true;
}

void ItemBase::paintCheckbox(
		Painter &p,
		QPoint position,
		bool selected,
		const PaintContext *context) {
	if (selected || context->selecting) {
		ensureCheckboxCreated();
	}
	if (_check) {
		_check->paint(p, position, _width, selected, context->selecting);
	}
}

void ItemBase::ensureCheckboxCreated() {
	if (_check) {
		return;
	}
	const auto repaint = [=] {
		_parent->history()->session().data().requestItemRepaint(_parent);
	};
	_check = std::make_unique<Checkbox>(repaint, st::overviewCheck);
}

void RadialProgressItem::setDocumentLinks(
		not_null<DocumentData*> document,
		bool forceOpen) {
	const auto context = parent()->fullId();
	setLinks(
		std::make_shared<DocumentOpenClickHandler>(
			document,
			crl::guard(this, [=](FullMsgId id) {
				clearSpoiler();
				delegate()->openDocument(document, id, forceOpen);
			}),
			context),
		std::make_shared<DocumentSaveClickHandler>(document, context),
		std::make_shared<DocumentCancelClickHandler>(
			document,
			nullptr,
			context));
}

void RadialProgressItem::clickHandlerActiveChanged(
		const ClickHandlerPtr &action,
		bool active) {
	ItemBase::clickHandlerActiveChanged(action, active);
	if (action == _openl || action == _savel || action == _cancell) {
		if (iconAnimated()) {
			const auto repaint = [=] {
				parent()->history()->session().data().requestItemRepaint(
					parent());
			};
			_a_iconOver.start(
				repaint,
				active ? 0. : 1.,
				active ? 1. : 0.,
				st::msgFileOverDuration);
		}
	}
}

void RadialProgressItem::setLinks(
		ClickHandlerPtr &&openl,
		ClickHandlerPtr &&savel,
		ClickHandlerPtr &&cancell) {
	_openl = std::move(openl);
	_savel = std::move(savel);
	_cancell = std::move(cancell);
}

void RadialProgressItem::radialAnimationCallback(crl::time now) const {
	const auto updated = [&] {
		return _radial->update(dataProgress(), dataFinished(), now, dataLoaded());
	}();
	if (!anim::Disabled() || updated) {
		parent()->history()->session().data().requestItemRepaint(parent());
	}
	if (!_radial->animating()) {
		checkRadialFinished();
	}
}

void RadialProgressItem::ensureRadial() {
	if (_radial) {
		return;
	}
	_radial = std::make_unique<Ui::MediaProgressAnimation>([=](crl::time now) {
		radialAnimationCallback(now);
	});
}

void RadialProgressItem::checkRadialFinished() const {
	if (_radial && !_radial->animating() && dataLoaded()) {
		_radial.reset();
	}
}

RadialProgressItem::~RadialProgressItem() = default;

void StatusText::update(
		int64 newSize,
		int64 fullSize,
		TimeId duration,
		TimeId realDuration) {
	setSize(newSize);
	if (_size == Ui::FileStatusSizeReady) {
		_text = (duration >= 0) ? Ui::FormatDurationAndSizeText(duration, fullSize) : (duration < -1 ? Ui::FormatGifAndSizeText(fullSize) : Ui::FormatSizeText(fullSize));
	} else if (_size == Ui::FileStatusSizeLoaded) {
		_text = (duration >= 0) ? Ui::FormatDurationText(duration) : (duration < -1 ? u"GIF"_q : Ui::FormatSizeText(fullSize));
	} else if (_size == Ui::FileStatusSizeFailed) {
		_text = tr::lng_attach_failed(tr::now);
	} else if (_size >= 0) {
		_text = Ui::FormatDownloadText(_size, fullSize);
	} else {
		_text = Ui::FormatPlayedText(-_size - 1, realDuration);
	}
}

void StatusText::setSize(int64 newSize) {
	_size = newSize;
}

Voice::Voice(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	not_null<DocumentData*> voice,
	const style::OverviewFileLayout &st)
: RadialProgressItem(delegate, parent)
, _data(voice)
, _namel(std::make_shared<DocumentOpenClickHandler>(
	_data,
	crl::guard(this, [=](FullMsgId id) {
		delegate->openDocument(_data, id);
	}),
	parent->fullId()))
, _st(st) {
	AddComponents(Info::Bit());

	setDocumentLinks(_data);
	_data->loadThumbnail(parent->fullId());

	updateName();
	const auto dateText = tr::link(
		langDateTime(base::unixtime::parse(parent->date()))); // Link 1.
	_details.setMarkedText(
		st::defaultTextStyle,
		tr::lng_date_and_duration(
			tr::now,
			lt_date,
			dateText,
			lt_duration,
			{ .text = Ui::FormatDurationText(_data->duration() / 1000) },
			tr::marked));
	_details.setLink(1, JumpToMessageClickHandler(parent));
}

void Voice::initDimensions() {
	_maxw = _st.maxWidth;
	_minh = _st.songPadding.top() + _st.songThumbSize + _st.songPadding.bottom() + st::lineWidth;
}

void Voice::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	ensureDataMediaCreated();
	bool selected = (selection == FullSelection);
	bool loaded = dataLoaded(), displayLoading = _data->displayLoading();

	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(dataProgress());
		}
	}
	const auto showPause = updateStatusText();
	const auto nameVersion = parent()->fromOriginal()->nameVersion();
	if (_nameVersion < nameVersion) {
		updateName();
	}
	const auto radial = isRadialAnimation();

	const auto nameleft = _st.songPadding.left()
		+ _st.songThumbSize
		+ _st.songPadding.right();
	const auto nameright = _st.songPadding.left();
	const auto nametop = _st.songNameTop;
	const auto statustop = _st.songStatusTop;
	const auto namewidth = _width - nameleft - nameright;

	const auto inner = style::rtlrect(
		_st.songPadding.left(),
		_st.songPadding.top(),
		_st.songThumbSize,
		_st.songThumbSize,
		_width);
	if (clip.intersects(Ui::MediaButtonPaintRect(inner))) {
		if (_data->hasThumbnail()) {
			ensureDataMediaCreated();
		}
		const auto thumbnail = _dataMedia
			? _dataMedia->thumbnail()
			: nullptr;
		const auto blurred = _dataMedia
			? _dataMedia->thumbnailInline()
			: nullptr;

		p.setPen(Qt::NoPen);
		if (thumbnail || blurred) {
			const auto options = Images::Option::RoundCircle
				| (blurred ? Images::Option::Blur : Images::Option());
			const auto thumb = (thumbnail ? thumbnail : blurred)->pix(
				inner.size(),
				{ .options = options });
			p.drawPixmap(inner.topLeft(), thumb);
		} else if (_data->hasThumbnail()) {
			PainterHighQualityEnabler hq(p);
			p.setBrush(st::imageBg);
			p.drawEllipse(inner);
		}
		const auto &checkLink = (_data->loading() || _data->uploading())
			? _cancell
			: (_dataMedia->canBePlayed() || loaded)
			? _openl
			: _savel;
		const auto overImage = thumbnail || blurred || _data->hasThumbnail();
		const auto colors = Ui::MediaButtonPalette(
			overImage ? st::historyFileThumbRadialFg->c : st::windowActiveTextFg->c,
			overImage ? st::msgDateImgBg->c : st::windowBg->c,
			overImage);

		const auto icon = [&] {
			if (_data->loading() || _data->uploading()) {
				return &(selected ? _st.voiceCancelSelected : _st.voiceCancel);
			} else if (showPause) {
				return &(selected ? _st.voicePauseSelected : _st.voicePause);
			} else if (_dataMedia->canBePlayed()) {
				return &(selected ? _st.voicePlaySelected : _st.voicePlay);
			}
			return &(selected
				? _st.voiceDownloadSelected
				: _st.voiceDownload);
		}();
		if (radial) {
			_radial->paint(p, inner, colors, _data->loading() || _data->uploading(),
				ClickHandler::showAsActive(_cancell), icon);
		} else {
			Ui::PaintMediaButtonSurface(p, inner, colors, ClickHandler::showAsActive(checkLink));
			icon->paintInCenter(p, inner, colors.foreground);
		}
	}

	if (clip.intersects(style::rtlrect(nameleft, nametop, namewidth, st::semiboldFont->height, _width))) {
		p.setPen(st::historyFileNameInFg);
		_name.drawLeftElided(p, nameleft, nametop, namewidth, _width);
	}

	if (clip.intersects(style::rtlrect(nameleft, statustop, namewidth, st::normalFont->height, _width))) {
		p.setFont(st::normalFont);
		p.setPen(selected ? st::mediaInFgSelected : st::mediaInFg);
		int32 unreadx = nameleft;
		if (_status.size() == Ui::FileStatusSizeLoaded || _status.size() == Ui::FileStatusSizeReady) {
			p.setTextPalette(selected ? st::mediaInPaletteSelected : st::mediaInPalette);
			_details.drawLeftElided(p, nameleft, statustop, namewidth, _width);
			p.restoreTextPalette();
			unreadx += _details.maxWidth();
		} else {
			int32 statusw = st::normalFont->width(_status.text());
			p.drawTextLeft(nameleft, statustop, _width, _status.text(), statusw);
			unreadx += statusw;
		}
		auto captionLeft = unreadx + st::mediaUnreadSkip;
		if (parent()->hasUnreadMediaFlag() && unreadx + st::mediaUnreadSkip + st::mediaUnreadSize <= _width) {
			p.setPen(Qt::NoPen);
			p.setBrush(selected ? st::msgFileInBgSelected : st::msgFileInBg);

			{
				PainterHighQualityEnabler hq(p);
				p.drawEllipse(style::rtlrect(unreadx + st::mediaUnreadSkip, statustop + st::mediaUnreadTop, st::mediaUnreadSize, st::mediaUnreadSize, _width));
			}
			captionLeft += st::mediaUnreadSkip + st::mediaUnreadSize;
		}
		if (!_caption.isEmpty()) {
			p.setPen(st::historyFileNameInFg);
			const auto w = _width - captionLeft - st::defaultScrollArea.width;
			_caption.draw(p, Ui::Text::PaintContext{
				.position = QPoint(captionLeft, statustop),
				.availableWidth = w,
				.spoiler = Ui::Text::DefaultSpoilerCache(),
				.paused = context
					? context->paused
					: On(PowerSaving::kEmojiChat),
				.pausedEmoji = On(PowerSaving::kEmojiChat),
				.pausedSpoiler = On(PowerSaving::kChatSpoiler),
				.elisionLines = 1,
			});
		}
	}

	const auto checkDelta = _st.songThumbSize
		+ st::overviewCheckSkip
		- st::overviewCheck.size;
	const auto checkLeft = _st.songPadding.left() + checkDelta;
	const auto checkTop = _st.songPadding.top() + checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

bool Voice::elementsAnimating() const {
	return RadialProgressItem::elementsAnimating()
		|| _details.hasPersistentAnimation()
		|| _caption.hasPersistentAnimation()
		|| ItemPlaying(_data, parent()->fullId(), AudioMsgId::Type::Voice);
}

TextState Voice::getState(
		QPoint point,
		StateRequest request) const {
	ensureDataMediaCreated();
	const auto loaded = dataLoaded();

	const auto nameleft = _st.songPadding.left()
		+ _st.songThumbSize
		+ _st.songPadding.right();
	const auto nameright = _st.songPadding.left();
	const auto nametop = _st.songNameTop;
	const auto statustop = _st.songStatusTop;

	const auto inner = style::rtlrect(
		_st.songPadding.left(),
		_st.songPadding.top(),
		_st.songThumbSize,
		_st.songThumbSize,
		_width);
	if (_data->loading() || _data->uploading()) {
		if (Ui::MediaCancelBadgeContains(inner, point)) {
			return { parent(), _cancell };
		}
		if (inner.contains(point)) {
			return { parent(), nullptr };
		}
	}
	if (inner.contains(point)) {
		const auto link = (_data->loading() || _data->uploading())
			? _cancell
			: (_dataMedia->canBePlayed() || loaded)
			? _openl
			: _savel;
		return { parent(), link };
	}
	auto result = TextState(parent());
	const auto statusmaxwidth = _width - nameleft - nameright;
	const auto statusrect = style::rtlrect(
		nameleft,
		statustop,
		statusmaxwidth,
		st::normalFont->height,
		_width);
	if (statusrect.contains(point)) {
		if (_status.size() == Ui::FileStatusSizeLoaded || _status.size() == Ui::FileStatusSizeReady) {
			auto textState = _details.getStateLeft(point - QPoint(nameleft, statustop), _width, _width);
			result.link = textState.link;
			result.cursor = textState.uponSymbol
				? HistoryView::CursorState::Text
				: HistoryView::CursorState::None;
		}
	}
	const auto namewidth = std::min(
		_width - nameleft - nameright,
		_name.maxWidth());
	const auto namerect = style::rtlrect(
		nameleft,
		nametop,
		namewidth,
		st::normalFont->height,
		_width);
	if (namerect.contains(point) && !result.link && !_data->loading()) {
		return { parent(), _namel };
	}
	return result;
}

void Voice::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	delegate()->registerHeavyItem(this);
}

void Voice::clearHeavyPart() {
	_dataMedia = nullptr;
}

float64 Voice::dataProgress() const {
	ensureDataMediaCreated();
	return _dataMedia->progress();
}

bool Voice::dataFinished() const {
	return !_data->loading()
		&& (!_data->uploading() || _data->waitingForAlbum());
}

bool Voice::dataLoaded() const {
	ensureDataMediaCreated();
	return _dataMedia->loaded();
}

bool Voice::iconAnimated() const {
	return true;
}

void Voice::updateName() {
	if (parent()->Has<HistoryMessageForwarded>()) {
		const auto info = parent()->originalHiddenSenderInfo();
		const auto name = info
			? tr::lng_forwarded(tr::now, lt_user, info->nameText().toString())
			: parent()->fromOriginal()->isChannel()
			? tr::lng_forwarded_channel(
				tr::now,
				lt_channel,
				parent()->fromOriginal()->name())
			: tr::lng_forwarded(
				tr::now,
				lt_user,
				parent()->fromOriginal()->name());
		_name.setText(st::semiboldTextStyle, name, Ui::NameTextOptions());
	} else {
		_name.setText(
			st::semiboldTextStyle,
			parent()->from()->name(),
			Ui::NameTextOptions());
	}
	_nameVersion = parent()->fromOriginal()->nameVersion();
	_caption.setMarkedText(
		st::defaultTextStyle,
		parent()->originalText(),
		Ui::DialogTextOptions(),
		Core::TextContext({
			.session = &parent()->history()->session(),
			.repaint = [=] { delegate()->repaintItem(this); },
		}));
}

bool Voice::updateStatusText() {
	auto showPause = false;
	auto statusSize = int64();
	auto realDuration = TimeId();
	if (_data->status == FileDownloadFailed || _data->status == FileUploadFailed) {
		statusSize = Ui::FileStatusSizeFailed;
	} else if (dataLoaded()) {
		statusSize = Ui::FileStatusSizeLoaded;
	} else {
		statusSize = Ui::FileStatusSizeReady;
	}

	const auto state = Media::Player::instance()->getState(AudioMsgId::Type::Voice);
	if (state.id == AudioMsgId(_data, parent()->fullId(), state.id.externalPlayId())
		&& !Media::Player::IsStoppedOrStopping(state.state)) {
		statusSize = -1 - (state.position / state.frequency);
		realDuration = (state.length / state.frequency);
		showPause = Media::Player::ShowPauseIcon(state.state);
	}

	if (statusSize != _status.size()) {
		_status.update(statusSize, _data->size, _data->duration() / 1000, realDuration);
	}
	return showPause;
}

Document::Document(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	DocumentFields fields,
	const style::OverviewFileLayout &st)
: RadialProgressItem(delegate, parent)
, _data(fields.document)
, _msgl(parent->isHistoryEntry()
	? JumpToMessageClickHandler(parent)
	: nullptr)
, _namel(std::make_shared<DocumentOpenClickHandler>(
	_data,
	crl::guard(this, [=](FullMsgId id) {
		delegate->openDocument(_data, id);
	}),
	parent->fullId()))
, _st(st)
, _generic(::Layout::DocumentGenericPreview::Create(_data))
, _externalLoading(std::move(fields.externalLoading))
, _forceFileLayout(fields.forceFileLayout)
, _date([&] {
	const auto date = base::unixtime::parse(fields.dateOverride
		? fields.dateOverride
		: parent->date());
	return langDayOfMonth(date.date()) + ' ' + date.time().toString(u"HH:mm"_q);
}()) {
	_name.setMarkedText(
		st::defaultTextStyle,
		(!_forceFileLayout
			? Ui::Text::FormatSongNameFor(_data).textWithEntities()
			: Ui::Text::FormatDownloadsName(_data)),
		_documentNameOptions);

	AddComponents(Info::Bit());

	setDocumentLinks(_data);
	if (auto cancel = std::move(fields.externalCancel)) {
		_cancell = std::make_shared<LambdaClickHandler>(crl::guard(
			this,
			[cancel = std::move(cancel)] { cancel(); }));
	}

	_status.update(
		Ui::FileStatusSizeReady,
		_data->size,
		songLayout() ? (_data->duration() / 1000) : -1,
		0);

	if (withThumb()) {
		if (_data->hasThumbnail()) {
			_data->loadThumbnail(parent->fullId());
			auto tw = style::ConvertScale(
				_data->thumbnailLocation().width());
			auto th = style::ConvertScale(
				_data->thumbnailLocation().height());
			if (tw > th) {
				_thumbw = (tw * _st.fileThumbSize) / th;
			} else {
				_thumbw = _st.fileThumbSize;
			}
		} else {
			_thumbw = _st.fileThumbSize;
		}
	} else {
		_thumbw = 0;
	}

}

bool Document::downloadInCorner() const {
	return _data->isAudioFile()
		&& parent()->allowsMediaDownloadControls()
		&& _data->canBeStreamed()
		&& !_data->inappPlaybackFailed();
}

void Document::initDimensions() {
	_maxw = _st.maxWidth;
	if (songLayout()) {
		_minh = _st.songPadding.top() + _st.songThumbSize + _st.songPadding.bottom();
	} else {
		_minh = _st.filePadding.top() + _st.fileThumbSize + _st.filePadding.bottom() + st::lineWidth;
	}
}

void Document::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	ensureDataMediaCreated();

	const auto selected = (selection == FullSelection);

	const auto cornerDownload = downloadInCorner();

	_dataMedia->automaticLoad(parent()->fullId(), parent());
	const auto loaded = dataLoaded();
	const auto displayLoading = externalLoading().has_value()
		|| _data->displayLoading();

	if (displayLoading) {
		ensureRadial();
		if (!_radial->animating()) {
			_radial->start(dataProgress());
		}
	}
	const auto showPause = updateStatusText();
	const auto radial = isRadialAnimation();

	int32 nameleft = 0, nametop = 0, nameright = 0, statustop = 0;
	const auto wthumb = withThumb();

	const auto isSong = songLayout();
	if (isSong) {
		nameleft = _st.songPadding.left() + _st.songThumbSize + _st.songPadding.right();
		nameright = _st.songPadding.left();
		nametop = _st.songNameTop;
		statustop = _st.songStatusTop;

		auto inner = style::rtlrect(_st.songPadding.left(), _st.songPadding.top(), _st.songThumbSize, _st.songThumbSize, _width);
		if (clip.intersects(Ui::MediaButtonPaintRect(inner))) {
			const auto isLoading = (!cornerDownload
				&& (_data->loading() || _data->uploading()));
			p.setPen(Qt::NoPen);

			using namespace HistoryView;
			const auto coverDrawn = _data->isSongWithCover()
				&& DrawThumbnailAsSongCover(
					p,
					st::songCoverOverlayFg,
					_dataMedia,
					inner,
					selected);
			const auto colors = Ui::MediaButtonPalette(
				coverDrawn ? st::historyFileThumbRadialFg->c : st::windowActiveTextFg->c,
				coverDrawn ? st::msgDateImgBg->c : st::windowBg->c,
				coverDrawn);

			const auto icon = [&] {
				if (!coverDrawn) {
					if (isLoading) {
						return &(selected
							? _st.voiceCancelSelected
							: _st.voiceCancel);
					} else if (showPause) {
						return &(selected
							? _st.voicePauseSelected
							: _st.voicePause);
					} else if (loaded || _dataMedia->canBePlayed()) {
						return &(selected
							? _st.voicePlaySelected
							: _st.voicePlay);
					}
					return &(selected
						? _st.voiceDownloadSelected
						: _st.voiceDownload);
				}
				if (isLoading) {
					return &(selected ? _st.songCancelSelected : _st.songCancel);
				} else if (showPause) {
					return &(selected ? _st.songPauseSelected : _st.songPause);
				} else if (loaded || _dataMedia->canBePlayed()) {
					return &(selected ? _st.songPlaySelected : _st.songPlay);
				}
				return &(selected ? _st.songDownloadSelected : _st.songDownload);
			}();
			if (radial && !cornerDownload) {
				_radial->paint(p, inner, colors, isLoading,
					ClickHandler::showAsActive(_cancell), icon);
			} else {
				Ui::PaintMediaButtonSurface(p, inner, colors,
					ClickHandler::showAsActive(loaded || _dataMedia->canBePlayed() ? _openl : _savel));
				icon->paintInCenter(p, inner, colors.foreground);
			}

			drawCornerDownload(p, selected, context);
		}
	} else {
		nameleft = _st.fileThumbSize + _st.filePadding.right();
		nametop = st::linksBorder + _st.fileNameTop;
		statustop = st::linksBorder + _st.fileStatusTop;

		QRect border(style::rtlrect(nameleft, 0, _width - nameleft, st::linksBorder, _width));
		if (!context->skipBorder && clip.intersects(border)) {
			p.fillRect(clip.intersected(border), st::linksBorderFg);
		}

		QRect rthumb(style::rtlrect(0, st::linksBorder + _st.filePadding.top(), _st.fileThumbSize, _st.fileThumbSize, _width));
		if (clip.intersects(rthumb)) {
			paintThumbnail(p, rthumb, wthumb);
			if (selected) {
				auto hq = PainterHighQualityEnabler(p);
				p.setPen(Qt::NoPen);
				p.setBrush(st::defaultTextPalette.selectOverlay);
				const auto radius = Ui::FilePreviewRadius(rthumb);
				p.drawRoundedRect(rthumb, radius, radius);
			}
		}
	}

	const auto availwidth = _width - nameleft - nameright;
	const auto namewidth = std::min(availwidth, _name.maxWidth());
	if (clip.intersects(style::rtlrect(nameleft, nametop, namewidth, st::semiboldFont->height, _width))) {
		p.setPen(st::historyFileNameInFg);
		_name.drawLeftElided(p, nameleft, nametop, namewidth, _width);
	}

	if (clip.intersects(style::rtlrect(nameleft, statustop, availwidth, st::normalFont->height, _width))) {
		const auto action = isSong ? nullptr : fileActionLink();
		const auto actionSkip = action
			? (st::normalFont->height + style::ConvertScale(4))
			: 0;
		if (action) {
			paintFileAction(p, action);
		} else if (_fileActionCancelling) {
			_fileActionAnimation.stop();
			_fileActionCancelling.reset();
		}
		p.setFont((!isSong && _msgl && ClickHandler::showAsActive(_msgl))
			? st::normalFont->underline()
			: st::normalFont);
		p.setPen((isSong && selected) ? st::mediaInFgSelected : st::mediaInFg);
		p.drawTextLeft(nameleft + actionSkip, statustop, _width,
			st::normalFont->elided(isSong ? _status.text() : fileStatusText(),
				availwidth - actionSkip));
	}

	const auto checkDelta = (isSong ? _st.songThumbSize : _st.fileThumbSize)
		+ (isSong ? st::overviewCheckSkip : -st::overviewCheckSkip)
		- st::overviewCheck.size;
	const auto checkLeft = (isSong
		? _st.songPadding.left()
		: 0) + checkDelta;
	const auto checkTop = (isSong
		? _st.songPadding.top()
		: (st::linksBorder + _st.filePadding.top())) + checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

bool Document::elementsAnimating() const {
	if (RadialProgressItem::elementsAnimating()
		|| _fileActionAnimation.animating()
		|| _name.hasPersistentAnimation()) {
		return true;
	}
	return songLayout()
		&& ItemPlaying(_data, parent()->fullId(), AudioMsgId::Type::Song);
}

void Document::paintThumbnail(
		Painter &p,
		QRect rthumb,
		bool wthumb) {
	if (wthumb) {
		ensureDataMediaCreated();
		const auto good = _data->isSvgImage()
			? _dataMedia->goodThumbnail()
			: nullptr;
		const auto thumbnail = good
			? good
			: _dataMedia->thumbnail();
		const auto blurred = _dataMedia->thumbnailInline();
		if (thumbnail || blurred) {
			if (_thumb.isNull() || (thumbnail && !_thumbLoaded)) {
				_thumbLoaded = (thumbnail != nullptr);
				const auto options = _thumbLoaded
					? Images::Option()
					: Images::Option::Blur;
				const auto image = thumbnail ? thumbnail : blurred;
				const auto prepared = image->pixNoCache(
					_thumbw * style::DevicePixelRatio(),
					{
						.options = options,
						.outer = QSize(
							_st.fileThumbSize,
							_st.fileThumbSize),
					});
				_thumb = Images::PixmapFast(Images::Round(
					prepared.toImage(),
					Images::CornersMask(qRound(Ui::FilePreviewRadius(rthumb)))));
			}
			p.drawPixmap(rthumb.topLeft(), _thumb);
		} else {
			p.setPen(Qt::NoPen);
			p.setBrush(st::overviewFileThumbBg);
			const auto radius = Ui::FilePreviewRadius(rthumb);
			p.drawRoundedRect(
				rthumb,
				radius,
				radius);
		}
	} else {
		Ui::PaintFilePreview(p, rthumb, _generic);
	}
}

QImage Document::dragPreviewImage() {
	ensureDataMediaCreated();
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(_st.fileThumbSize, _st.fileThumbSize) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);

	auto p = Painter(&result);
	auto hq = PainterHighQualityEnabler(p);
	paintThumbnail(
		p,
		QRect(0, 0, _st.fileThumbSize, _st.fileThumbSize),
		withThumb());
	return result;
}

void Document::drawCornerDownload(QPainter &p, bool selected, const PaintContext *context) const {
	if (dataLoaded()
		|| _data->loadedInMediaCache()
		|| selected
		|| context->selecting
		|| !downloadInCorner()) {
		return;
	}
	const auto size = st::overviewCheck.size;
	const auto shift = _st.songThumbSize + st::overviewCheckSkip - size;
	const auto inner = style::rtlrect(_st.songPadding.left() + shift, _st.songPadding.top() + shift, size, size, _width);
	const auto colors = Ui::MediaButtonPalette(st::windowActiveTextFg->c, st::windowBg->c);
	const auto hovered = ClickHandler::showAsActive(
		_data->loading() ? _cancell : _savel);
	if (_radial && _radial->animating()) {
		_radial->paintFill(p, inner, colors, hovered);
	} else {
		Ui::PaintMediaButtonProgress(p, inner, colors,
			Extras::MusicPlayer::trackDownloadProgress(_data), hovered);
	}
	const auto icon = [&] {
		if (_data->loading()) {
			return &(selected ? st::overviewSmallCancelSelected : st::overviewSmallCancel);
		}
		return &(selected ? st::overviewSmallDownloadSelected : st::overviewSmallDownload);
	}();
	icon->paintInCenter(p, inner, colors.foreground);
}

TextState Document::cornerDownloadTextState(
		QPoint point,
		StateRequest request) const {
	auto result = TextState(parent());
	if (!downloadInCorner()
		|| dataLoaded()
		|| _data->loadedInMediaCache()) {
		return result;
	}
	const auto size = st::overviewCheck.size;
	const auto shift = _st.songThumbSize + st::overviewCheckSkip - size;
	const auto inner = style::rtlrect(_st.songPadding.left() + shift, _st.songPadding.top() + shift, size, size, _width);
	if (inner.contains(point)) {
		result.link = _data->loading() ? _cancell : _savel;
	}
	return result;

}

TextState Document::getState(
		QPoint point,
		StateRequest request) const {
	ensureDataMediaCreated();
	const auto loaded = dataLoaded();

	if (songLayout()) {
		const auto nameleft = _st.songPadding.left() + _st.songThumbSize + _st.songPadding.right();
		const auto nameright = _st.songPadding.left();
		const auto namewidth = std::min(
			_width - nameleft - nameright,
			_name.maxWidth());
		const auto nametop = _st.songNameTop;

		if (const auto state = cornerDownloadTextState(point, request); state.link) {
			return state;
		}

		const auto inner = style::rtlrect(
			_st.songPadding.left(),
			_st.songPadding.top(),
			_st.songThumbSize,
			_st.songThumbSize,
			_width);
		if (!downloadInCorner() && (_data->loading() || _data->uploading())) {
			if (Ui::MediaCancelBadgeContains(inner, point)) {
				return { parent(), _cancell };
			}
			if (inner.contains(point)) {
				return { parent(), nullptr };
			}
		}
		if (inner.contains(point)) {
			const auto link = (!downloadInCorner()
				&& (_data->loading() || _data->uploading()))
				? _cancell
				: (loaded || _dataMedia->canBePlayed())
				? _openl
				: _savel;
			return { parent(), link };
		}
		const auto namerect = style::rtlrect(
			nameleft,
			nametop,
			namewidth,
			st::semiboldFont->height,
			_width);
		if (namerect.contains(point) && !_data->loading()) {
			return { parent(), _namel };
		}
	} else {
		const auto nameleft = _st.fileThumbSize + _st.filePadding.right();
		const auto nameright = 0;
		const auto nametop = st::linksBorder + _st.fileNameTop;
		const auto namewidth = std::min(
			_width - nameleft - nameright,
			_name.maxWidth());
		const auto statustop = st::linksBorder + _st.fileStatusTop;
		const auto action = fileActionLink();
		if (action && fileActionRect().contains(point)) {
			return { parent(), action };
		}
		const auto actionSkip = action
			? (st::normalFont->height + style::ConvertScale(4))
			: 0;

		const auto rthumb = style::rtlrect(
			0,
			st::linksBorder + _st.filePadding.top(),
			_st.fileThumbSize,
			_st.fileThumbSize,
			_width);

		if (rthumb.contains(point)) {
			const auto link = (activeLoading() || _data->uploading())
				? nullptr
				: loaded
				? _openl
				: _savel;
			return { parent(), link };
		}

		if (_data->status != FileUploadFailed) {
			const auto statusrect = style::rtlrect(
				nameleft + actionSkip,
				statustop,
				std::min(_width - nameleft - actionSkip, st::normalFont->width(fileStatusText())),
				st::normalFont->height,
				_width);
			if (statusrect.contains(point)) {
				return { parent(), _msgl };
			}
		}
		if (!activeLoading() && !_data->isNull()) {
			auto leftofnamerect = style::rtlrect(
				0,
				st::linksBorder,
				nameleft,
				_height - st::linksBorder,
				_width);
			if (loaded && leftofnamerect.contains(point)) {
				return { parent(), _namel };
			}
			const auto namerect = style::rtlrect(
				nameleft,
				nametop,
				namewidth,
				st::semiboldFont->height,
				_width);
			if (namerect.contains(point)) {
				return { parent(), _namel };
			}
		}
	}
	return {};
}

bool Document::selectionConsumesClick(QPoint point) const {
	if (!songLayout()) {
		return !fileActionLink() || !fileActionRect().contains(point);
	}
	if (const auto state = cornerDownloadTextState(point, StateRequest());
		state.link) {
		return false;
	}
	const auto inner = style::rtlrect(
		_st.songPadding.left(),
		_st.songPadding.top(),
		_st.songThumbSize,
		_st.songThumbSize,
		_width);
	return !inner.contains(point)
		&& !((_data->loading() || _data->uploading())
			&& Ui::MediaCancelBadgeContains(inner, point));
}

bool Document::songLayout() const {
	return !_forceFileLayout && _data->isSong();
}

void Document::ensureDataMediaCreated() const {
	if (_dataMedia) {
		return;
	}
	_dataMedia = _data->createMediaView();
	_dataMedia->thumbnailWanted(parent()->fullId());
	if (_data->isSvgImage()) {
		_dataMedia->goodThumbnailWanted();
		Data::DocumentMedia::CheckGoodThumbnail(_data);
	}
	delegate()->registerHeavyItem(this);
}

void Document::clearHeavyPart() {
	_dataMedia = nullptr;
}

auto Document::externalLoading() const
-> std::optional<DocumentExternalLoading> {
	return _externalLoading ? _externalLoading() : std::nullopt;
}

bool Document::activeLoading() const {
	return externalLoading().has_value() || _data->loading();
}

QString Document::fileStatusText() const {
	return _status.text() + u" · "_q + _date;
}

QRect Document::fileActionRect() const {
	return style::rtlrect(
		_st.fileThumbSize + _st.filePadding.right(),
		st::linksBorder + _st.fileStatusTop,
		st::normalFont->height,
		st::normalFont->height,
		_width);
}

ClickHandlerPtr Document::fileActionLink() const {
	if (activeLoading() || _data->uploading()) {
		return _cancell;
	}
	return (dataLoaded() || _data->isNull()) ? nullptr : _savel;
}

void Document::paintFileAction(QPainter &p, const ClickHandlerPtr &action) {
	const auto cancelling = (action == _cancell);
	const auto previous = _fileActionAnimation.value(
		_fileActionCancelling.value_or(cancelling) ? 1. : 0.);
	if (_fileActionCancelling && *_fileActionCancelling != cancelling) {
		_fileActionAnimation.start([=] {
			delegate()->repaintItem(this);
		}, previous, cancelling ? 1. : 0., st::fadeWrapDuration);
	}
	_fileActionCancelling = cancelling;
	const auto progress = _fileActionAnimation.value(cancelling ? 1. : 0.);
	const auto rect = QRectF(fileActionRect());
	const auto metrics = QFontMetricsF(st::normalFont->f);
	const auto glyphHeight = metrics.capHeight();
	const auto center = QPointF(rect.center().x(),
		rect.top() + st::normalFont->ascent - glyphHeight / 2.);
	const auto opacity = p.opacity();
	const auto paint = [&](bool cancel, float64 shown) {
		if (shown <= 0.) {
			return;
		}
		// 去掉矢量画布留白后，符号与文字的大写字高对齐。
		const auto side = glyphHeight * 24. / (cancel ? 15. : 18.);
		p.setOpacity(opacity * shown);
		Ui::PaintMediaIcon(p, QRectF(center - QPointF(side / 2., side / 2.),
			QSizeF(side, side)),
			cancel ? st::overviewSmallCancel : st::overviewSmallDownload,
			st::windowActiveTextFg->c);
	};
	p.save();
	paint(false, 1. - progress);
	paint(true, progress);
	p.restore();
}

float64 Document::dataProgress() const {
	if (const auto external = externalLoading()) {
		return (external->total > 0)
			? std::clamp(
				external->ready / float64(external->total),
				0.,
				1.)
			: 0.;
	}
	ensureDataMediaCreated();
	return _data->uploading() ? _dataMedia->progress()
		: std::max(_dataMedia->progress(), Extras::MusicPlayer::trackDownloadProgress(_data));
}

bool Document::dataFinished() const {
	return !activeLoading()
		&& (!_data->uploading() || _data->waitingForAlbum());
}

bool Document::dataLoaded() const {
	if (externalLoading()) {
		return false;
	}
	ensureDataMediaCreated();
	return _dataMedia->loaded();
}

bool Document::iconAnimated() const {
	return songLayout()
		|| !dataLoaded()
		|| (_radial && _radial->animating());
}

bool Document::withThumb() const {
	return !songLayout()
		&& (_data->hasThumbnail() || _data->isSvgImage());
}

bool Document::updateStatusText() {
	auto showPause = false;
	auto statusSize = int64();
	auto realDuration = TimeId();
	const auto external = externalLoading();
	if (external) {
		statusSize = external->ready;
	} else if (_data->status == FileDownloadFailed
		|| _data->status == FileUploadFailed) {
		statusSize = Ui::FileStatusSizeFailed;
	} else if (_data->uploading()) {
		statusSize = _data->uploadingData->offset;
	} else if (_data->loading()) {
		statusSize = _data->loadOffset();
	} else if (dataLoaded()) {
		statusSize = Ui::FileStatusSizeLoaded;
	} else {
		statusSize = Ui::FileStatusSizeReady;
	}

	const auto isSong = songLayout();
	if (isSong) {
		const auto state = Media::Player::instance()->getState(AudioMsgId::Type::Song);
		if (state.id == AudioMsgId(_data, parent()->fullId(), state.id.externalPlayId()) && !Media::Player::IsStoppedOrStopping(state.state)) {
			statusSize = -1 - (state.position / state.frequency);
			realDuration = (state.length / state.frequency);
			showPause = Media::Player::ShowPauseIcon(state.state);
		}
		if (!showPause && (state.id == AudioMsgId(_data, parent()->fullId(), state.id.externalPlayId())) && Media::Player::instance()->isSeeking(AudioMsgId::Type::Song)) {
			showPause = true;
		}
	}

	if (statusSize != _status.size()) {
		_status.update(
			statusSize,
			external ? external->total : _data->size,
			isSong ? (_data->duration() / 1000) : -1,
			realDuration);
	}
	return showPause;
}

Link::Link(
	not_null<Delegate*> delegate,
	not_null<HistoryItem*> parent,
	Data::Media *media)
: ItemBase(delegate, parent)
, _text(st::msgMinWidth) {
	AddComponents(Info::Bit());

	auto textWithEntities = parent->originalText();
	QString mainUrl;

	auto text = textWithEntities.text;
	const auto &entities = textWithEntities.entities;
	int32 from = 0, till = text.size(), lnk = entities.size();
	for (const auto &entity : entities) {
		auto type = entity.type();
		if (type != EntityType::Url && type != EntityType::CustomUrl && type != EntityType::Email) {
			continue;
		}
		const auto customUrl = entity.data();
		const auto entityText = text.mid(entity.offset(), entity.length());
		const auto url = customUrl.isEmpty() ? entityText : customUrl;
		if (_links.empty()) {
			mainUrl = url;
		}
		_links.push_back(LinkEntry(url, entityText));
	}
	if (_links.empty()) {
		if (const auto media = parent->media()) {
			if (const auto webpage = media->webpage()) {
				if (!webpage->displayUrl.isEmpty()
					&& !webpage->url.isEmpty()) {
					_links.push_back(
						LinkEntry(webpage->displayUrl, webpage->url));
				}
			}
		}
	}
	while (lnk > 0 && till > from) {
		--lnk;
		const auto &entity = entities.at(lnk);
		auto type = entity.type();
		if (type != EntityType::Url && type != EntityType::CustomUrl && type != EntityType::Email) {
			++lnk;
			break;
		}
		int32 afterLinkStart = entity.offset() + entity.length();
		if (till > afterLinkStart) {
			if (!QRegularExpression(u"^[,.\\s_=+\\-;:`'\"\\(\\)\\[\\]\\{\\}<>*&^%\\$#@!\\\\/]+$"_q).match(text.mid(afterLinkStart, till - afterLinkStart)).hasMatch()) {
				++lnk;
				break;
			}
		}
		till = entity.offset();
	}
	if (!lnk) {
		if (QRegularExpression(u"^[,.\\s\\-;:`'\"\\(\\)\\[\\]\\{\\}<>*&^%\\$#@!\\\\/]+$"_q).match(text.mid(from, till - from)).hasMatch()) {
			till = from;
		}
	}

	const auto createHandler = MakeUrlHandler;
	_page = media ? media->webpage() : nullptr;
	if (_page) {
		mainUrl = _page->url;
		if (_page->document) {
			_photol = std::make_shared<DocumentOpenClickHandler>(
				_page->document,
				crl::guard(this, [=](FullMsgId id) {
					delegate->openDocument(_page->document, id);
				}),
				parent->fullId());
		} else if (_page->photo) {
			if (_page->type == WebPageType::Profile
				|| _page->type == WebPageType::Video) {
				_photol = createHandler(_page->url);
			} else if (_page->type == WebPageType::Photo
				|| _page->type == WebPageType::Document
				|| _page->siteName == u"Twitter"_q
				|| _page->siteName == u"Facebook"_q) {
				_photol = std::make_shared<PhotoOpenClickHandler>(
					_page->photo,
					crl::guard(this, [=](FullMsgId id) {
						delegate->openPhoto(_page->photo, id);
					}),
					parent->fullId());
			} else {
				_photol = createHandler(_page->url);
			}
		} else {
			_photol = createHandler(_page->url);
		}
	} else if (!mainUrl.isEmpty()) {
		_photol = createHandler(mainUrl);
	}

	if (from >= till && _page) {
		text = _page->description.text;
		from = 0;
		till = text.size();
	}
	if (till > from) {
		TextParseOptions opts = { TextParseMultiline, int32(st::linksMaxWidth), 3 * st::normalFont->height, Qt::LayoutDirectionAuto };
		_text.setText(st::defaultTextStyle, text.mid(from, till - from), opts);
	}
	int32 tw = 0, th = 0;
	if (_page && _page->photo) {
		const auto photo = _page->photo;
		if (photo->hasExact(Data::PhotoSize::Small)
			|| photo->hasExact(Data::PhotoSize::Thumbnail)) {
			photo->load(Data::PhotoSize::Small, parent->fullId());
		}
		tw = style::ConvertScale(photo->width());
		th = style::ConvertScale(photo->height());
	} else if (_page && _page->document && _page->document->hasThumbnail()) {
		_page->document->loadThumbnail(parent->fullId());
		const auto &location = _page->document->thumbnailLocation();
		tw = style::ConvertScale(location.width());
		th = style::ConvertScale(location.height());
	}
	if (tw > st::linksPhotoSize) {
		if (th > tw) {
			th = th * st::linksPhotoSize / tw;
			tw = st::linksPhotoSize;
		} else if (th > st::linksPhotoSize) {
			tw = tw * st::linksPhotoSize / th;
			th = st::linksPhotoSize;
		}
	}
	_pixw = std::max(tw, 1);
	_pixh = std::max(th, 1);

	if (_page) {
		_title = _page->title;
	}

	auto parts = QStringView(mainUrl).split('/');
	if (!parts.isEmpty()) {
		auto domain = parts.at(0);
		if (parts.size() > 2 && domain.endsWith(':') && parts.at(1).isEmpty()) { // http:// and others
			domain = parts.at(2);
		}

		parts = domain.split('@').constLast().split('.', Qt::SkipEmptyParts);
		if (parts.size() > 1) {
			_letter = parts.at(parts.size() - 2).at(0).toUpper();
			if (_title.isEmpty()) {
				_title.reserve(parts.at(parts.size() - 2).size());
				_title.append(_letter).append(parts.at(parts.size() - 2).mid(1));
			}
		}
	}
	_titlew = st::semiboldFont->width(_title);

	setupLinks(textWithEntities, mainUrl);
}

void Link::setupLinks(
		const TextWithEntities &text,
		const QString &mainUrl) {
	const auto page = _page;
	const auto ivCapable = [=](const QString &url) {
		return Iv::PreferForUri(url)
			|| (page && page->iv && UrlMatchesWebPage(page, url));
	};
	const auto makeHandler = [&](const QString &url, bool iv) {
		return iv
			? ClickHandlerPtr(std::make_shared<IvLink>(page, text, url))
			: MakeUrlHandler(url);
	};
	static const auto kLinkOptions = TextParseOptions{
		0,
		0,
		0,
		Qt::LayoutDirectionAuto,
	};
	for (auto &link : _links) {
		const auto iv = ivCapable(link.url);
		link.handler = makeHandler(link.url, iv);
		auto markup = iv
			? Ui::Text::IconEmoji(&st::overviewLinkIvIcon)
			: TextWithEntities();
		const auto offset = int(markup.text.size());
		markup.append(link.display);
		markup.entities.push_back({
			EntityType::CustomUrl,
			offset,
			int(link.display.size()),
			link.url,
		});
		link.text.setMarkedText(st::defaultTextStyle, markup, kLinkOptions);
		link.text.setLink(1, link.handler);
	}

	const auto titleUrl = !mainUrl.isEmpty()
		? mainUrl
		: (page ? page->url : QString());
	if (!titleUrl.isEmpty()) {
		_titlel = makeHandler(titleUrl, ivCapable(titleUrl));
	}

	if (!mainUrl.isEmpty() && ivCapable(mainUrl)) {
		const auto previous = _photol;
		_photol = std::make_shared<LambdaClickHandler>([=](
				ClickContext context) {
			const auto my = context.other.value<ClickHandlerContext>();
			if (const auto controller = my.sessionWindow.get()) {
				OpenPreferringIv(
					controller,
					page,
					text,
					mainUrl,
					context.other);
			} else if (previous) {
				previous->onClick(context);
			}
		});
	}
}

void Link::initDimensions() {
	_maxw = st::linksMaxWidth;
	_minh = 0;
	if (!_title.isEmpty()) {
		_minh += st::semiboldFont->height;
	}
	if (!_text.isEmpty()) {
		_minh += std::min(
			3 * st::normalFont->height,
			_text.countHeight(_maxw
				- st::linksPhotoSize
				- st::linksPhotoPadding));
	}
	_minh += _links.size() * st::normalFont->height;
	_minh = std::max(_minh, int32(st::linksPhotoSize))
		+ st::linksMargin.top()
		+ st::linksMargin.bottom()
		+ st::linksBorder;
}

int32 Link::resizeGetHeight(int32 width) {
	_width = std::min(width, _maxw);
	int32 w = _width - st::linksPhotoSize - st::linksPhotoPadding;
	for (const auto &link : _links) {
		if (const auto handler = std::dynamic_pointer_cast<TextClickHandler>(
				link.handler)) {
			handler->setFullDisplayed(w >= link.text.maxWidth());
		}
	}

	_height = 0;
	if (!_title.isEmpty()) {
		_height += st::semiboldFont->height;
	}
	if (!_text.isEmpty()) {
		_height += std::min(
			3 * st::normalFont->height,
			_text.countHeight(_width
				- st::linksPhotoSize
				- st::linksPhotoPadding));
	}
	_height += _links.size() * st::normalFont->height;
	_height = std::max(_height, int32(st::linksPhotoSize))
		+ st::linksMargin.top()
		+ st::linksMargin.bottom()
		+ st::linksBorder;
	return _height;
}

void Link::paint(Painter &p, const QRect &clip, TextSelection selection, const PaintContext *context) {
	auto selected = (selection == FullSelection);

	const auto pixLeft = 0;
	const auto pixTop = st::linksMargin.top() + st::linksBorder;
	if (clip.intersects(style::rtlrect(0, pixTop, st::linksPhotoSize, st::linksPhotoSize, _width))) {
		validateThumbnail();
		if (!_thumbnail.isNull()) {
			p.drawPixmap(pixLeft, pixTop, _thumbnail);
		}
	}

	const auto left = st::linksPhotoSize + st::linksPhotoPadding;
	const auto w = _width - left;
	auto top = [&] {
		if (!_title.isEmpty() && _text.isEmpty() && _links.size() == 1) {
			return pixTop + (st::linksPhotoSize - st::semiboldFont->height - st::normalFont->height) / 2;
		}
		return st::linksTextTop;
	}();

	p.setPen(st::linksTextFg);
	p.setFont(st::semiboldFont);
	if (!_title.isEmpty()) {
		if (clip.intersects(style::rtlrect(
				left,
				top,
				std::min(w, _titlew),
				st::semiboldFont->height,
				_width))) {
			p.drawTextLeft(left, top, _width, (w < _titlew) ? st::semiboldFont->elided(_title, w) : _title);
		}
		top += st::semiboldFont->height;
	}
	p.setFont(st::msgFont);
	if (!_text.isEmpty()) {
		int32 h = std::min(st::normalFont->height * 3, _text.countHeight(w));
		if (clip.intersects(style::rtlrect(left, top, w, h, _width))) {
			_text.drawLeftElided(p, left, top, w, _width, 3);
		}
		top += h;
	}

	p.setPen(st::windowActiveTextFg);
	for (const auto &link : _links) {
		const auto width = link.text.maxWidth();
		if (clip.intersects(style::rtlrect(
				left,
				top,
				std::min(w, width),
				st::normalFont->height,
				_width))) {
			link.text.drawLeftElided(p, left, top, w, _width);
		}
		top += st::normalFont->height;
	}

	QRect border(style::rtlrect(left, 0, w, st::linksBorder, _width));
	if (!context->skipBorder && clip.intersects(border)) {
		p.fillRect(clip.intersected(border), st::linksBorderFg);
	}

	const auto checkDelta = st::linksPhotoSize + st::overviewCheckSkip
		- st::overviewCheck.size;
	const auto checkLeft = pixLeft + checkDelta;
	const auto checkTop = pixTop + checkDelta;
	paintCheckbox(p, { checkLeft, checkTop }, selected, context);
}

bool Link::elementsAnimating() const {
	if (_text.hasPersistentAnimation()) {
		return true;
	}
	for (const auto &link : _links) {
		if (link.text.hasPersistentAnimation()) {
			return true;
		}
	}
	return false;
}

void Link::validateThumbnail() {
	if (!_thumbnail.isNull() && !_thumbnailBlurred) {
		return;
	}
	const auto size = QSize(_pixw, _pixh);
	const auto outer = QSize(st::linksPhotoSize, st::linksPhotoSize);
	if (_page && _page->photo) {
		using Data::PhotoSize;
		ensurePhotoMediaCreated();
		const auto args = Images::PrepareArgs{
			.options = Images::Option::RoundSmall,
			.outer = outer,
		};
		if (const auto thumbnail = _photoMedia->image(PhotoSize::Thumbnail)) {
			_thumbnail = thumbnail->pixSingle(size, args);
			_thumbnailBlurred = false;
		} else if (const auto large = _photoMedia->image(PhotoSize::Large)) {
			_thumbnail = large->pixSingle(size, args);
			_thumbnailBlurred = false;
		} else if (const auto small = _photoMedia->image(PhotoSize::Small)) {
			_thumbnail = small->pixSingle(size, args);
			_thumbnailBlurred = false;
		} else if (const auto blurred = _photoMedia->thumbnailInline()) {
			_thumbnail = blurred->pixSingle(size, args.blurred());
			return;
		} else {
			return;
		}
		_photoMedia = nullptr;
		delegate()->unregisterHeavyItem(this);
	} else if (_page && _page->document && _page->document->hasThumbnail()) {
		ensureDocumentMediaCreated();
		const auto args = Images::PrepareArgs{
			.options = (_page->document->isVideoMessage()
				? Images::Option::RoundCircle
				: Images::Option::RoundSmall),
			.outer = outer,
		};
		if (const auto thumbnail = _documentMedia->thumbnail()) {
			_thumbnail = thumbnail->pixSingle(size, args);
			_thumbnailBlurred = false;
		} else if (const auto blurred = _documentMedia->thumbnailInline()) {
			_thumbnail = blurred->pixSingle(size, args.blurred());
			return;
		} else {
			return;
		}
		_documentMedia = nullptr;
		delegate()->unregisterHeavyItem(this);
	} else {
		const auto size = QSize(st::linksPhotoSize, st::linksPhotoSize);
		_thumbnail = QPixmap(size * style::DevicePixelRatio());
		_thumbnail.fill(Qt::transparent);
		auto p = Painter(&_thumbnail);
		const auto index = _letter.isEmpty()
			? 0
			: (_letter[0].unicode() % 4);
		const auto fill = [&](style::color color, Ui::CachedRoundCorners corners) {
			auto pixRect = QRect(
				0,
				0,
				st::linksPhotoSize,
				st::linksPhotoSize);
			Ui::FillRoundRect(p, pixRect, color, corners);
		};
		switch (index) {
		case 0: fill(st::msgFile1Bg, Ui::Doc1Corners); break;
		case 1: fill(st::msgFile2Bg, Ui::Doc2Corners); break;
		case 2: fill(st::msgFile3Bg, Ui::Doc3Corners); break;
		case 3: fill(st::msgFile4Bg, Ui::Doc4Corners); break;
		}

		if (!_letter.isEmpty()) {
			p.setFont(st::linksLetterFont);
			p.setPen(st::linksLetterFg);
			p.drawText(
				QRect(0, 0, st::linksPhotoSize, st::linksPhotoSize),
				_letter,
				style::al_center);
		}
		_thumbnailBlurred = false;
	}
}

void Link::ensurePhotoMediaCreated() {
	if (_photoMedia) {
		return;
	}
	_photoMedia = _page->photo->createMediaView();
	_photoMedia->wanted(Data::PhotoSize::Small, parent()->fullId());
	delegate()->registerHeavyItem(this);
}

void Link::ensureDocumentMediaCreated() {
	if (_documentMedia) {
		return;
	}
	_documentMedia = _page->document->createMediaView();
	_documentMedia->thumbnailWanted(parent()->fullId());
	delegate()->registerHeavyItem(this);
}

void Link::clearHeavyPart() {
	_photoMedia = nullptr;
	_documentMedia = nullptr;
}

TextState Link::getState(
		QPoint point,
		StateRequest request) const {
	int32 left = st::linksPhotoSize + st::linksPhotoPadding, top = st::linksMargin.top() + st::linksBorder, w = _width - left;
	if (style::rtlrect(0, top, st::linksPhotoSize, st::linksPhotoSize, _width).contains(point)) {
		return { parent(), _photol };
	}

	if (!_title.isEmpty() && _text.isEmpty() && _links.size() == 1) {
		top += (st::linksPhotoSize - st::semiboldFont->height - st::normalFont->height) / 2;
	}
	if (!_title.isEmpty()) {
		if (style::rtlrect(
				left,
				top,
				std::min(w, _titlew),
				st::semiboldFont->height,
				_width).contains(point)) {
			return { parent(), _titlel };
		}
		top += st::webPageTitleFont->height;
	}
	if (!_text.isEmpty()) {
		top += std::min(st::normalFont->height * 3, _text.countHeight(w));
	}
	for (const auto &link : _links) {
		const auto width = link.text.maxWidth();
		if (style::rtlrect(
				left,
				top,
				std::min(w, width),
				st::normalFont->height,
				_width).contains(point)) {
			return { parent(), link.handler };
		}
		top += st::normalFont->height;
	}
	return {};
}

Link::LinkEntry::LinkEntry(const QString &url, const QString &display)
: url(url)
, display(display) {
}

} // namespace Overview::Layout
