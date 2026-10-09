#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

[[nodiscard]] int PillRadius(QRect pill) {
	return std::min({
		st::walletCardRadius,
		pill.width() / 2,
		pill.height() / 2,
	});
}

InfoIslandEntry::InfoIslandEntry(
	QWidget *parent,
	rpl::producer<QString> text,
	const style::SettingsButton &st)
: Ui::SettingsButton(parent, std::move(text), st) {
}

InfoIslandEntry::InfoIslandEntry(
	QWidget *parent,
	std::nullptr_t,
	const style::SettingsButton &st)
: Ui::SettingsButton(parent, nullptr, st) {
}

void InfoIslandEntry::setRounding(RectParts corners, int radius) {
	if (_corners == corners && _radius == radius) {
		return;
	}
	_corners = corners;
	_radius = radius;
	finishAnimating();
	update();
}

void InfoIslandEntry::setMinimalHeight(int height) {
	if (_minimalHeight == height) {
		return;
	}
	_minimalHeight = height;
	resizeToWidth(width());
}

int InfoIslandEntry::resizeGetHeight(int newWidth) {
	return std::max(
		Ui::SettingsButton::resizeGetHeight(newWidth),
		_minimalHeight);
}

QImage InfoIslandEntry::prepareRippleMask() const {
	if (_radius <= 0 || !_corners) {
		return Ui::RippleAnimation::RectMask(size());
	}
	// The filled RoundRectMask overload starts from a fully opaque mask and
	// only cuts out the corners it is handed, so a null one stays square.
	// Images::CornersMaskRef holds bare pointers into the array it is built
	// from, which is why that array must outlive the call reading them.
	const auto masks = Images::CornersMask(_radius);
	auto corners = Images::CornersMaskRef();
	const auto fill = [&](RectPart corner, int index) {
		if (_corners & corner) {
			corners.p[index] = &masks[index];
		}
	};
	fill(RectPart::TopLeft, Images::kTopLeft);
	fill(RectPart::TopRight, Images::kTopRight);
	fill(RectPart::BottomLeft, Images::kBottomLeft);
	fill(RectPart::BottomRight, Images::kBottomRight);
	return Ui::RippleAnimation::RoundRectMask(size(), corners);
}

InfoIsland::InfoIsland(QWidget *parent)
: Ui::VerticalLayout(parent)
, _shadow(st::walletInfoIslandShadow)
, _extend(_shadow.extend()) {
	Ui::AddSkip(this, _extend.top());
	Ui::AddSkip(this, _extend.bottom());
	paintOn([=](QPainter &p) {
		paintPill(p);
	});
}

not_null<Ui::SlideWrap<InfoIslandEntry>*> InfoIsland::add(
		object_ptr<InfoIslandEntry> entry) {
	// The row margins are vertically zero on purpose: VerticalLayout counts
	// a row margin even for a zero-height child, so any non-zero one would
	// leak height from a hidden entry and grow the island where it must
	// contribute nothing at all.
	const auto wrap = insert(
		count() - 1,
		object_ptr<Ui::SlideWrap<InfoIslandEntry>>(this, std::move(entry)),
		style::margins(
			st::walletIslandMargin.left(),
			0,
			st::walletIslandMargin.right(),
			0));
	_entries.push_back(wrap);
	_tracker.track(wrap);

	// VerticalLayout registers its own heightValue handler inside insert,
	// so that one runs first and has already repositioned the rows and
	// resized the island by the time this one does, which is what makes
	// the wrap's height and y current here. heightValue also emits once
	// on subscription, and refreshing the rounding is idempotent.
	wrap->heightValue(
	) | rpl::on_next([=] {
		refreshRounding();
	}, wrap->lifetime());

	return wrap;
}

rpl::producer<bool> InfoIsland::anyShownValue() const {
	return _tracker.atLeastOneShownValue();
}

int InfoIsland::resizeGetHeight(int newWidth) {
	const auto result = Ui::VerticalLayout::resizeGetHeight(newWidth);
	refreshRounding();
	return result;
}

void InfoIsland::refreshRounding() {
	auto visible = std::vector<not_null<InfoIslandEntry*>>();
	visible.reserve(_entries.size());
	for (const auto &wrap : _entries) {
		if (wrap->height() > 0) {
			visible.push_back(wrap->entity());
		}
	}
	const auto shown = int(visible.size());
	const auto radius = PillRadius(pillRect());
	for (auto i = 0; i != shown; ++i) {
		const auto first = !i;
		const auto last = (i == shown - 1);
		visible[i]->setRounding((first && last)
			? RectParts(RectPart::AllCorners)
			: first
			? (RectPart::TopLeft | RectPart::TopRight)
			: last
			? (RectPart::BottomLeft | RectPart::BottomRight)
			: RectParts(), radius);
	}
	update();
}

QRect InfoIsland::pillRect() const {
	const auto &margin = st::walletIslandMargin;
	return QRect(
		margin.left(),
		_extend.top(),
		width() - margin.left() - margin.right(),
		height() - _extend.top() - _extend.bottom());
}

void InfoIsland::paintPill(QPainter &p) {
	const auto pill = pillRect();
	if (pill.isEmpty()) {
		return;
	}
	Dialogs::PaintPillBackground(p, _shadow, pill, PillRadius(pill));
	auto first = true;
	for (const auto &wrap : _entries) {
		if (wrap->height() <= 0) {
			continue;
		} else if (first) {
			first = false;
			continue;
		}
		p.fillRect(
			pill.x(),
			wrap->y(),
			pill.width(),
			st::lineWidth,
			st::shadowFg);
	}
}

KeyContext::KeyContext(
	std::shared_ptr<Main::SessionShow> show,
	std::shared_ptr<CommentScope> scope,
	Fn<bool()> current,
	Fn<void(KeyAuthorization)> done)
: _show(std::move(show))
, _session(base::make_weak(&_show->session()))
, _scope(std::move(scope))
, _current(std::move(current))
, _done(std::move(done)) {
}

void KeyContext::showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions,
		anim::type animated) const {
	const auto self = std::const_pointer_cast<KeyContext>(
		shared_from_this());
	if (!valid()) {
		self->cancel();
		return;
	}
	const auto content = std::get_if<object_ptr<Ui::BoxContent>>(&layer);
	if (!content || !*content) {
		self->cancel();
		return;
	}
	const auto prompt = std::make_shared<Prompt>();
	self->acceptClosed();
	prompt->box = content->data();
	_prompts.push_back(prompt);
	const auto weak = std::weak_ptr(self);
	// Native gates report success either from boxClosing() or immediately
	// after closeBox() returns. Register before their deferred preparation,
	// then let the native continuation accept its closed prompts before
	// deciding whether dismissal was terminal. Cloud gates may also show
	// their successor before closing, while an uncontinued dismissal retires
	// the entire comment attempt and closes its remaining owned prompts.
	const auto closed = [weak, prompt] {
		prompt->closing = true;
		if (prompt->cancelOnClose) {
			if (const auto strong = weak.lock()) {
				strong->promptClosed(prompt);
			}
			return;
		}
		crl::on_main([weak, prompt] {
			if (const auto strong = weak.lock()) {
				strong->promptClosed(prompt);
			}
		});
	};
	prompt->box->boxClosing() | rpl::on_next(
		closed,
		prompt->box->lifetime());
	prompt->box->lifetime().add(closed);
	_show->showOrHideBoxOrLayer(
		std::move(layer),
		Ui::LayerOption::KeepOther,
		animated);
}

not_null<QWidget*> KeyContext::toastParent() const {
	return _show->toastParent();
}

bool KeyContext::valid() const {
	return !_finished
		&& _session
		&& _show->valid()
		&& _current()
		&& (!_scope || _session->wallet().commentScopeCurrent(_scope));
}

KeyContext::operator bool() const {
	return valid();
}

Main::Session &KeyContext::session() const {
	Expects(_session != nullptr);

	return *_session;
}

std::shared_ptr<CommentScope> KeyContext::scope() const {
	return _scope;
}

std::shared_ptr<Main::SessionShow> KeyContext::plain() const {
	return _show;
}

CustodyInstaller KeyContext::installer() {
	const auto self = shared_from_this();
	// WHY: the ladder stores the key on this device, which is right to
	// finish even once the action that asked for it has expired, so it is
	// judged by the window it lives in and not by that action. Handing it
	// this context instead made every unrelated wallet event close the
	// chooser with nothing stored and nothing said, and the next press
	// started the whole restore again.
	const auto native = MakeCustodyInstaller(_show);
	return [=](CustodyInstallRequest request) {
		if (!self->valid()) {
			request.ready({});
			self->cancel();
			return;
		}
		self->_installing = true;
		request.passcodeCreated = [
			self,
			created = std::move(request.passcodeCreated)
		](quint32 previousEpoch, quint32 epoch) {
			// Judged like the rest of the ladder: the vault transition has
			// to be sound, and whether the action that asked for the key is
			// still there decides nothing about storing it.
			if (!created || !created(previousEpoch, epoch)) {
				return false;
			}
			self->acceptClosed();
			return true;
		};
		request.ready = [=, ready = std::move(request.ready)](
				CustodyInstall result) {
			self->_installing = false;
			self->acceptClosed();
			const auto installed = result.grant != nullptr;
			ready(std::move(result));
			if (!installed) {
				self->cancel();
			}
		};
		native(std::move(request));
	};
}

void KeyContext::acceptClosed() {
	for (const auto &prompt : _prompts) {
		if (prompt->closing) {
			prompt->accepted = true;
		}
	}
}

void KeyContext::allowPromptRetry(
		base::weak_qptr<Ui::BoxContent> box) {
	for (const auto &prompt : _prompts) {
		if (prompt->box == box) {
			prompt->accepted = true;
			return;
		}
	}
}

void KeyContext::cancelOnClose(
		base::weak_qptr<Ui::BoxContent> box,
		bool allowSuccessor) {
	if (!box) {
		cancel();
		return;
	}
	for (const auto &prompt : _prompts) {
		if (prompt->box == box) {
			prompt->cancelOnClose = true;
			prompt->allowSuccessor = allowSuccessor;
			if (prompt->closing) {
				promptClosed(prompt);
			}
			return;
		}
	}
}

void KeyContext::closePrompt(base::weak_qptr<Ui::BoxContent> box) {
	for (const auto &prompt : _prompts) {
		if (prompt->box == box) {
			prompt->accepted = true;
			if (box && !prompt->closing && box->hasDelegate()) {
				box->closeBox();
			}
			return;
		}
	}
}

void KeyContext::ready(KeyAuthorization auth) {
	if (!valid() || !auth.grant) {
		cancel();
		return;
	}
	finish(std::move(auth));
}

void KeyContext::cancel() {
	finish({});
}

void KeyContext::finish(KeyAuthorization auth) {
	if (_finished) {
		return;
	}
	_finished = true;
	const auto scope = _scope;
	auto lifetime = base::take(_lifetime);
	const auto done = base::take(_done);
	const auto prompts = base::take(_prompts);
	if (!auth.grant && scope) {
		scope->cancel();
	}
	lifetime.destroy();
	for (const auto &prompt : ranges::views::reverse(prompts)) {
		prompt->accepted = true;
		if (const auto box = prompt->box.get()) {
			if (!prompt->closing && box->hasDelegate()) {
				box->closeBox();
			}
		}
	}
	if (done) {
		done(std::move(auth));
	}
}

rpl::lifetime &KeyContext::lifetime() {
	return _lifetime;
}

void KeyContext::promptClosed(const std::shared_ptr<Prompt> &prompt) {
	if (_finished || prompt->accepted) {
		return;
	} else if (_installing) {
		// The install ladder is this press continuing, not the user
		// abandoning it, and it owns no prompt of this context any more.
		prompt->accepted = true;
		return;
	}
	auto later = false;
	for (const auto &other : _prompts) {
		if (prompt->allowSuccessor
			&& later
			&& other->box
			&& !other->closing) {
			prompt->accepted = true;
			return;
		}
		later = later || (other == prompt);
	}
	cancel();
}

EncryptedCommentLabel::EncryptedCommentLabel(
	QWidget *parent,
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	TransferItem item,
	Fn<bool()> originCurrent)
: FlatLabel(parent, st::walletCommentLabel)
, _cover(TransferCommentCover(item))
, _revealable(EncryptedCommentRevealable(item))
, _comment(&show->session(), std::move(item), [
		this,
		originCurrent = std::move(originCurrent)] {
	return !_closed && (!originCurrent || originCurrent());
}) {
	setContextCopyText(QString());
	setSelectable(!_revealable);
	setMarkedText(_cover);
	setContextMenuHook([weak = base::make_weak(this)](ContextMenuRequest request) {
		if (!weak || !weak->_comment.plaintext()) {
			return;
		}
		request.menu->addAction(tr::lng_context_copy_text(tr::now), [weak] {
			if (weak) {
				if (const auto &text = weak->_comment.plaintext()) {
					TextUtilities::SetClipboardText(TextForMimeData::Simple(*text));
				}
			}
		});
	});
	setClickHandlerFilter([=](const ClickHandlerPtr &, Qt::MouseButton button) {
		if (button != Qt::LeftButton) {
			return false;
		} else if (!_revealable) {
			return true;
		} else if (!_comment.plaintext()) {
			_comment.activate(show);
		}
		return false;
	});
	setAnimationsPausedCallback([] {
		return On(PowerSaving::kChatSpoiler)
			? WhichAnimationsPaused::Spoiler
			: WhichAnimationsPaused::None;
	});
	_comment.changes() | rpl::on_next([=] {
		if (_closed) {
			return; // the closing reset must not flash the cover as it fades
		}
		const auto revealed = _comment.plaintext().has_value();
		if (revealed == _revealed) {
			return;
		}
		_revealed = revealed;
		if (const auto &text = _comment.plaintext()) {
			setText(*text);
			setSelectable(true);
		} else {
			setSelectable(false);
			setContextCopyText(QString());
			setMarkedText(_cover);
		}
	}, lifetime());
	box->boxClosing() | rpl::on_next([=] {
		_closed = true;
		_comment.reset();
	}, lifetime());
}

QString EncryptedCommentLabel::accessibilityName() {
	const auto &text = _comment.plaintext();
	return text
		? *text
		: _revealable
		? tr::lng_action_gram_transfer_encrypted_comment(tr::now)
		: _cover.text;
}

[[nodiscard]] int PanelCardWidth() {
	return st::walletPanelSize.width()
		- st::walletCardMargin.left()
		- st::walletCardMargin.right();
}

[[nodiscard]] QRect CardQrRect(int cardWidth) {
	return QRect(
		cardWidth - st::walletCardQrRight - st::walletCardQrSize.width(),
		st::walletCardQrTop,
		st::walletCardQrSize.width(),
		st::walletCardQrSize.height());
}

[[nodiscard]] QRect TransferCardInfoRect(int cardWidth) {
	return QRect(
		cardWidth - st::walletCardContentLeft - st::walletCardQrSize.width(),
		st::walletCardQrTop,
		st::walletCardQrSize.width(),
		st::walletCardQrSize.height());
}

// WHY: the plate under this glyph is fixed brand appearance no theme can
// move, and windowSubTextFg over it measured 1.90:1 in the day theme;
// this grey clears 2.0:1 over every point of the plate the glyph covers.
[[nodiscard]] QColor CardQrIconFg() {
	return QColor(0x73, 0x73, 0x73);
}

[[nodiscard]] QString GroupedAddressLine(
		const QString &address,
		int offset) {
	auto groups = QStringList();
	for (auto i = 0; i != kAddressGroupsPerLine; ++i) {
		groups.append(address.mid(
			offset + i * kAddressGroup,
			kAddressGroup));
	}
	return groups.join(QChar(' '));
}

[[nodiscard]] QStringList TransferCardLines(
		const QString &destination,
		int recipients) {
	if (recipients >= 2) {
		const auto summary = tr::lng_wallet_connect_request_recipients(
			tr::now,
			lt_count,
			recipients);
		return { summary };
	}
	auto result = QStringList();
	const auto perLine = kAddressGroup * kAddressGroupsPerLine;
	for (auto offset = 0; offset < destination.size(); offset += perLine) {
		result.append(GroupedAddressLine(destination, offset).trimmed());
	}
	return result;
}

// The sheet's address presentation is the friendly form, so a raw address
// the engine cannot convert is a reading this sheet does not have and
// builds no row. Substituting the raw form would show a different kind of
// address without saying so.
[[nodiscard]] std::optional<QString> DetailsFriendlyAddress(
		const TransferItem &item) {
	const auto friendly = FormatFriendly(
		item.counterparty,
		item.counterpartyBounceable);
	if (friendly.isEmpty()) {
		return std::nullopt;
	}
	return friendly;
}

[[nodiscard]] TextWithEntities DetailsAddressValue(
		const QString &address) {
	auto groups = QStringList();
	for (auto offset = 0; offset < address.size(); offset += kAddressGroup) {
		groups.append(address.mid(offset, kAddressGroup));
	}
	return Ui::Text::Wrapped(
		{ groups.join(QChar(' ')) },
		EntityType::Code,
		{});
}

[[nodiscard]] Fn<void()> CopyAddressCallback(
		std::shared_ptr<Ui::Show> show,
		const QString &address) {
	return CopyTextCallback(
		std::move(show),
		address,
		tr::lng_gift_unique_address_copied(tr::now));
}

} // namespace ContentDetails

} // namespace Wallet
