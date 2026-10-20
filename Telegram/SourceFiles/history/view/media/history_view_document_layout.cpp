/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_document.h"

#include "api/api_transcribes.h"
#include "apiwrap.h"
#include "data/data_document.h"
#include "data/data_media_types.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_message.h"
#include "history/view/history_view_transcribe_button.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_session.h"
#include "media/audio/media_audio.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_lottie_custom_emoji.h"
#include "ui/text/text_utilities.h"
#include "styles/style_chat.h"
#include "styles/style_chat_style.h"

namespace HistoryView {
namespace {

[[nodiscard]] bool IsHostedInstantViewMedia(not_null<const Element*> parent) {
	return parent->Get<InstantViewMediaRuntime>() != nullptr;
}

[[nodiscard]] int MaxStatusWidth(not_null<DocumentData*> document) {
	using namespace Ui;
	auto result = 0;
	const auto add = [&](const QString &text) {
		accumulate_max(result, st::normalFont->width(text));
	};
	add(FormatDownloadText(document->size, document->size));
	const auto duration = document->duration() / 1000;
	if (document->song()) {
		add(FormatPlayedText(duration, duration));
		add(FormatDurationAndSizeText(duration, document->size));
	} else if (document->voice() ? document->voice() : document->round()) {
		add(FormatPlayedText(duration, duration));
		add(FormatDurationAndSizeText(duration, document->size));
	} else if (document->isVideoFile()) {
		add(FormatDurationAndSizeText(duration, document->size));
	} else {
		add(FormatSizeText(document->size));
	}
	return result;
}

} // namespace

QSize Document::countOptimalSize() {
	auto hasTranscribe = false;
	const auto voice = Get<HistoryDocumentVoice>();
	if (voice) {
		const auto history = _realParent->history();
		const auto session = &history->session();
		const auto transcribes = &session->api().transcribes();
		const auto media = _parent->data()->media();
		if ((media && media->ttlSeconds())
			|| IsHostedInstantViewMedia(_parent)
			|| _realParent->isScheduled()
			|| _realParent->isAdminLogEntry()
			|| (!session->premium()
				&& !transcribes->freeFor(_realParent)
				&& !transcribes->trialsSupport())
			|| (!session->premium()
				&& _data->duration() > transcribes->trialsMaxLengthMs())) {
			voice->transcribe = nullptr;
			voice->transcribeText = {};
		} else {
			const auto creating = !voice->transcribe;
			if (creating) {
				voice->transcribe = std::make_unique<TranscribeButton>(
					_realParent,
					false);
			}
			const auto &entry = transcribes->entry(_realParent);
			const auto update = [=] { repaint(); };
			voice->transcribe->setLoading(
				entry.shown && (entry.requestId || entry.pending));
			const auto pending = entry.pending;
			auto descriptor = pending
				? Lottie::IconDescriptor{
					.name = u"transcribe_loading"_q,
					.color = &st::attentionButtonFg, // Any contrast.
					.sizeOverride = Size(st::historyTranscribeLoadingSize),
					.colorizeUsingAlpha = true,
				}
				: Lottie::IconDescriptor();
			auto text = (entry.requestId || !entry.shown)
				? TextWithEntities()
				: entry.toolong
				? tr::italic(tr::lng_audio_transcribe_long(tr::now))
				: entry.failed
				? tr::italic(tr::lng_attach_failed(tr::now))
				: TextWithEntities{ entry.result }.append(
					pending
						? Ui::Text::LottieEmoji(descriptor)
						: TextWithEntities());
			voice->transcribe->setOpened(
				!text.empty(),
				creating ? Fn<void()>() : update);
			if (text.empty()) {
				voice->transcribeText = {};
			} else {
				const auto minResizeWidth = st::minPhotoSize
					- st::msgPadding.left()
					- st::msgPadding.right();
				voice->transcribeText = Ui::Text::String(minResizeWidth);
				voice->transcribeText.setMarkedText(
					st::messageTextStyle,
					text,
					kMarkupTextOptions,
					pending
						? Ui::Text::LottieEmojiContext(std::move(descriptor))
						: Ui::Text::MarkedContext());
				hasTranscribe = true;
				if (const auto skipBlockWidth = _parent->hasVisibleText()
					? 0
					: _parent->skipBlockWidth()) {
					voice->transcribeText.updateSkipBlock(
						skipBlockWidth,
						_parent->skipBlockHeight());
				}
			}
		}
	}

	auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = thumbed ? st::msgFileThumbLayout : st::msgFileLayout;
	if (thumbed) {
		if (_data->hasThumbnail()) {
			const auto &location = _data->thumbnailLocation();
			auto tw = style::ConvertScale(location.width());
			auto th = style::ConvertScale(location.height());
			if (tw > th) {
				thumbed->thumbw = (tw * st.thumbSize) / th;
			} else {
				thumbed->thumbw = st.thumbSize;
			}
		} else {
			thumbed->thumbw = st.thumbSize;
		}
	}

	auto maxWidth = st::msgFileMinWidth;

	const auto tleft = st.padding.left() + st.thumbSize + st.thumbSkip;
	const auto tright = st.padding.right();
	if (thumbed) {
		accumulate_max(maxWidth, tleft + MaxStatusWidth(_data) + tright);
	} else {
		auto unread = (_data->isVoiceMessage() || _transcribedRound)
			? (st::mediaUnreadSkip + st::mediaUnreadSize)
			: 0;
		accumulate_max(maxWidth, tleft + MaxStatusWidth(_data) + unread + _parent->skipBlockWidth() + st::msgPadding.right());
	}

	if (const auto named = Get<HistoryDocumentNamed>()) {
		accumulate_max(maxWidth, tleft + named->name.maxWidth() + tright);
		accumulate_min(maxWidth, st::msgMaxWidth);
	}
	if (voice) {
		const auto maxWaveformWidth = ::Media::Player::kWaveformSamplesCount *
			(st::msgWaveformBar + st::msgWaveformSkip);
		const auto transcribeWidth = voice->transcribe
			? (voice->transcribe->size().width() + st::historyTranscribeSkip)
			: 0;
		accumulate_max(
			maxWidth,
			maxWaveformWidth
				+ rect::m::sum::h(st.padding)
				+ st.thumbSize
				+ st.thumbSkip
				+ transcribeWidth);
	}

	auto minHeight = st.padding.top() + st.thumbSize + st.padding.bottom();
	if (isBubbleBottom() && !hasTranscribe) {
		if (const auto link = thumbedLinkMaxWidth()) {
			accumulate_max(
				maxWidth,
				(tleft
					+ link
					+ st.thumbSkip
					+ _parent->bottomInfoFirstLineWidth()
					+ tright));
		}
	}
	if (!isBubbleTop()) {
		minHeight -= st::msgFileTopMinus;
	}

	if (hasTranscribe) {
		auto captionw = maxWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		minHeight += voice->transcribeText.countHeight(captionw);
		if (isBubbleBottom()) {
			minHeight += st::msgPadding.bottom();
		}
	}
	return { maxWidth, minHeight };
}

QSize Document::countCurrentSize(int newWidth) {
	const auto captioned = Get<HistoryDocumentCaptioned>();
	const auto voice = Get<HistoryDocumentVoice>();
	const auto hasTranscribe = voice && !voice->transcribeText.isEmpty();
	const auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = thumbed ? st::msgFileThumbLayout : st::msgFileLayout;
	const auto hostedInstantView = IsHostedInstantViewMedia(_parent);
	if (!captioned && !hasTranscribe) {
		auto result = File::countCurrentSize(newWidth);
		if (hostedInstantView) {
			result.setWidth(std::max(newWidth, result.width()));
		}
		if (isBubbleBottom()) {
			const auto thumbedWidth = thumbedLinkMaxWidth();
			const auto statusWidth = thumbedWidth
				? 0
				: st::normalFont->width(_statusText);
			if (thumbedWidth || statusWidth) {
				const auto needed = st.padding.left()
					+ (thumbedWidth
						? st.thumbSize + st.thumbSkip
						: st::msgFileLayout.thumbSize
							+ st::mediaUnreadSkip)
					+ (thumbedWidth + statusWidth)
					+ st.thumbSkip
					+ (_realParent->isUnreadMedia()
						? st::mediaUnreadSkip + st::mediaUnreadSize
						: 0)
					+ _parent->bottomInfoFirstLineWidth()
					+ st.padding.right();
				if (result.width() < needed) {
					result.setHeight(result.height()
						+ st::msgDateFont->height
						- st::msgDateDelta.y());
				}
			}
		}
		return result;
	}

	if (!hostedInstantView) {
		accumulate_min(newWidth, maxWidth());
	}
	auto newHeight = st.padding.top() + st.thumbSize + st.padding.bottom();
	if (!isBubbleTop()) {
		newHeight -= st::msgFileTopMinus;
	}
	auto captionw = newWidth - st::msgPadding.left() - st::msgPadding.right();
	if (hasTranscribe) {
		newHeight += voice->transcribeText.countHeight(captionw);
		if (captioned) {
			newHeight += st::mediaCaptionSkip;
		} else if (isBubbleBottom()) {
			newHeight += st::msgPadding.bottom();
		}
	}
	if (captioned) {
		newHeight += captioned->caption.countHeight(captionw);
		if (isBubbleBottom()) {
			newHeight += st::msgPadding.bottom();
		}
	}

	return { newWidth, newHeight };
}

QSize Document::sizeForGroupingOptimal(int maxWidth, bool last) const {
	const auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = (thumbed ? st::msgFileThumbLayoutGrouped : st::msgFileLayoutGrouped);
	auto height = st.padding.top() + st.thumbSize + st.padding.bottom();
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		auto captionw = maxWidth
			- st::msgPadding.left()
			- st::msgPadding.right();
		height += captioned->caption.countHeight(captionw);
	}
	return { maxWidth, height };
}

QSize Document::sizeForGrouping(int width) const {
	const auto thumbed = Get<HistoryDocumentThumbed>();
	const auto &st = (thumbed ? st::msgFileThumbLayoutGrouped : st::msgFileLayoutGrouped);
	auto height = st.padding.top() + st.thumbSize + st.padding.bottom();
	if (const auto captioned = Get<HistoryDocumentCaptioned>()) {
		auto captionw = width
			- st::msgPadding.left()
			- st::msgPadding.right();
		height += captioned->caption.countHeight(captionw);
	}
	return { maxWidth(), height };
}

} // namespace HistoryView
