#include "history/view/media/history_view_gram_transfer_internal.h"

namespace HistoryView {

namespace GramTransferInternal {}

using namespace GramTransferInternal;

namespace GramTransferInternal {

GramTransferCommentPart::GramTransferCommentPart(
	GramTransferOrigin origin,
	Wallet::TransferItem item,
	QString display)
: _origin(std::move(origin))
, _cover(item.commentEncrypted
	? Wallet::TransferCommentCover(item)
	: tr::marked(std::move(display)))
, _text(0) {
	if (Wallet::EncryptedCommentRevealable(item)) {
		createComment(std::move(item));
		GramTransferInvalidations(_origin) | rpl::on_next([=] {
			invalidate();
		}, _lifetime);
	}
	updateText();
}

GramTransferCommentPart::~GramTransferCommentPart() {
	_retired = true;
	_lifetime.destroy();
	_commentLifetime.destroy();
	_comment = nullptr;
}

bool GramTransferCommentPart::covered() const {
	return _comment && !_revealed;
}

void GramTransferCommentPart::createComment(Wallet::TransferItem item) {
	_commentLifetime.destroy();
	_comment = nullptr;
	_commentIdentity = item.walletIdentity;
	const auto weak = base::make_weak(this);
	_comment = std::make_unique<Wallet::TransferComment>(
		_origin.session.get(),
		std::move(item),
		[weak] {
			return weak
				&& !weak->_retired
				&& CurrentGramTransfer(weak->_origin);
		});
	_comment->changes() | rpl::on_next([=] {
		const auto revealed = _comment->plaintext().has_value();
		if (revealed == _revealed) {
			return;
		}
		_revealed = revealed;
		updateText();
		if (const auto view = _origin.view.get()) {
			if (revealed) {
				view->history()->owner().registerShownSpoiler(view);
			}
			if (_retired) {
				// The list may be mid-removal and re-lays the view out anyway.
				view->setPendingResize();
			} else {
				view->history()->owner().requestViewResize(view);
			}
			view->repaint();
		}
	}, _commentLifetime);
}

void GramTransferCommentPart::updateText() {
	const auto view = _origin.view;
	auto text = Ui::Text::String(0);
	text.setMarkedText(
		st::serviceTextStyle,
		_revealed ? tr::marked(*_comment->plaintext()) : _cover,
		kPlainTextOptions,
		{
			.repaint = [view] {
				if (view) {
					view->repaint();
				}
			},
		});
	_text = std::move(text);
	if (_text.hasSpoilers()) {
		const auto weak = base::make_weak(this);
		_text.setSpoilerLinkFilter([weak](const ClickContext &context) {
			const auto strong = weak.get();
			if (!strong || context.button != Qt::LeftButton) {
				return false;
			} else if (strong->_comment) {
				strong->activate(context);
				return false;
			}
			// The user's own comment waits for no key, so it lifts the way
			// a spoiler in a message text does.
			if (const auto view = strong->_origin.view.get()) {
				view->history()->owner().registerShownSpoiler(view);
			}
			return true;
		});
	}
}

// A revealed comment is covered again wherever a spoiler is, and dropping
// the plaintext re-covers it through the same changes() handler.
void GramTransferCommentPart::hideSpoilers() {
	if (_text.hasSpoilers()) {
		_text.setSpoilerRevealed(false, anim::type::instant);
	}
	if (_comment) {
		_comment->reset();
	}
}

void GramTransferCommentPart::invalidate() {
	_retired = true;
	if (_comment) {
		_comment->reset();
	}
}

void GramTransferCommentPart::activate(const ClickContext &context) {
	if (_retired || !_comment) {
		return;
	}
	if (const auto show = GramTransferShow(_origin, context)) {
		if (_comment->pending() || _comment->plaintext().has_value()) {
			return;
		}
		auto details = ResolveGramTransfer(_origin.session.get(), _origin.action);
		if (details.item.walletIdentity != _commentIdentity) {
			if (!Wallet::EncryptedCommentRevealable(details.item)) {
				return;
			}
			createComment(std::move(details.item));
		}
		_comment->activate(show);
	}
}

QSize GramTransferCommentPart::countOptimalSize() {
	const auto height = resolveLayout(st::chatUniqueGiftMaxWidth);
	return { st::chatUniqueGiftMaxWidth, height };
}

QSize GramTransferCommentPart::countCurrentSize(int newWidth) {
	return { newWidth, resolveLayout(newWidth) };
}

int GramTransferCommentPart::resolveLayout(int outerWidth) {
	if (_text.isEmpty()) {
		_textRect = QRect();
		return 0;
	}
	const auto skip = st::walletChatCardCommentSkip;
	const auto limit = std::max(GramTransferCardWidth(outerWidth), 1);
	const auto size = Ui::Text::CountOptimalTextSize(_text, 0, limit);
	_textRect = QRect(
		(outerWidth - size.width()) / 2,
		skip,
		size.width(),
		size.height());
	return skip + size.height() + skip + st::chatUniqueGiftBorder;
}

void GramTransferCommentPart::draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const {
	if (_textRect.isEmpty()) {
		return;
	}
	p.setPen(context.st->msgServiceFg());
	_text.draw(p, {
		.position = _textRect.topLeft(),
		.outerWidth = outerWidth,
		.availableWidth = _textRect.width(),
		.align = style::al_top,
		.palette = &context.st->serviceTextPalette(),
		.spoiler = Ui::Text::DefaultSpoilerCache(),
		.now = context.now,
		.pausedEmoji = context.paused || On(PowerSaving::kEmojiChat),
		.pausedSpoiler = context.paused || On(PowerSaving::kChatSpoiler),
		.selection = covered()
			? TextSelection()
			: (context.selection == FullSelection)
			? AllTextSelection
			: context.selection,
	});
}

TextState GramTransferCommentPart::textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const {
	if (_textRect.isEmpty()) {
		return {};
	}
	auto textRequest = request.forText();
	textRequest.align = style::al_top;
	auto result = TextState(nullptr, _text.getState(
		point - _textRect.topLeft(),
		_textRect.width(),
		textRequest));
	if (covered()) {
		auto cover = TextState();
		if (_textRect.contains(point)) {
			cover.link = result.link;
		}
		return cover;
	}
	result.link = nullptr;
	if (!_textRect.contains(point)) {
		result.cursor = CursorState::None;
	}
	result.overMessageText = (result.cursor == CursorState::Text);
	return result;
}

uint16 GramTransferCommentPart::fullSelectionLength() const {
	return covered() ? 0 : _text.length();
}

TextSelection GramTransferCommentPart::adjustSelection(
		TextSelection selection,
		TextSelectType type) const {
	return covered()
		? TextSelection()
		: (selection == FullSelection)
		? selection
		: _text.adjustSelection(selection, type);
}

TextForMimeData GramTransferCommentPart::selectedText(
		TextSelection selection) const {
	return covered()
		? TextForMimeData()
		: _text.toTextForMimeData((selection == FullSelection)
			? AllTextSelection
			: selection);
}

}

GramReadLine::GramReadLine(Fn<void()> repaint)
: _repaint(std::move(repaint))
, _timer([=] {
	schedule(crl::now());
	_repaint();
}) {
}

GramReadLine::Turn GramReadLine::join(crl::time now) {
	if (!_turns.empty() && now >= _turns.back() + kReadSpacing) {
		_turns.clear();
	}
	const auto at = _turns.empty()
		? now
		: (_turns.back() + kReadSpacing);
	_turns.push_back(at);
	auto result = Turn{ .at = at };
	if (at > now) {
		schedule(now);
		result.waiting.add([weak = base::make_weak(this), at] {
			if (const auto strong = weak.get()) {
				strong->leave(at);
			}
		});
	}
	return result;
}

void GramReadLine::leave(crl::time at) {
	const auto now = crl::now();
	if (at <= now) {
		return; // a turn that came still spaces the cards after it
	}
	_turns.erase(ranges::remove(_turns, at), end(_turns));
	schedule(now);
}

void GramReadLine::schedule(crl::time now) {
	if (_timer.isActive() && !_timer.remainingTime()) {
		return;
	}
	const auto next = ranges::upper_bound(_turns, now);
	if (next == end(_turns)) {
		_timer.cancel();
	} else {
		_timer.callOnce(*next - now, Qt::PreciseTimer);
	}
}

std::unique_ptr<Media> CreateGramTransferMedia(
		not_null<Element*> parent,
		Element *replacing) {
	return std::make_unique<MediaGeneric>(
		parent,
		[parent, replacing](
				not_null<MediaGeneric*> media,
				Fn<void(std::unique_ptr<MediaGenericPart>)> push) {
			const auto item = parent->data();
			const auto transfer = item->Get<HistoryServiceGramTransfer>();
			if (!transfer) {
				return;
			}
			const auto origin = GramTransferOrigin{
				.session = base::make_weak(&item->history()->session()),
				.view = base::make_weak(parent),
				.media = base::make_weak(media),
				.action = SnapshotGramTransfer(item),
			};
			// WHY: the card is replaced both with the whole view, when the
			// view is refreshed, and in place, when an edit refreshes the
			// view's text; the media being replaced is |parent|'s own then.
			auto handover = GramTransferHandover();
			const auto source = replacing ? replacing : parent.get();
			const auto previous = dynamic_cast<MediaGeneric*>(
				source->media());
			const auto card = previous
				? dynamic_cast<GramTransferCardPart*>(previous->partAt(0))
				: nullptr;
			if (card && source->data() == item) {
				handover = card->takeHandover();
				if (replacing) {
					// The replaced view no longer holds what it registered.
					replacing->checkHeavyPart();
				}
			}
			push(std::make_unique<GramTransferCardPart>(
				origin,
				std::move(handover)));
			if (transfer->commentEncrypted || !transfer->comment.isEmpty()) {
				auto details = ResolveGramTransfer(
					origin.session.get(),
					origin.action);
				push(std::make_unique<GramTransferCommentPart>(
					origin,
					std::move(details.item),
					transfer->commentText()));
			}
		},
		MediaGenericDescriptor{
			.maxWidth = st::chatUniqueGiftMaxWidth,
			.service = true,
			.hideServiceText = false,
		});
}

}
