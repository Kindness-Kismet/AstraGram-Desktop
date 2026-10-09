#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

SendCommentBubble::SendCommentBubble(
	QWidget *parent,
	rpl::producer<SendComment> comment,
	rpl::producer<bool> clickable,
	Fn<void()> clicked)
: RpWidget(parent)
, _button(Ui::CreateChild<Ui::AbstractButton>(this)) {
	_button->setClickedCallback(std::move(clicked));
	_button->setPointerCursor(false);
	std::move(clickable) | rpl::on_next([=](bool value) {
		_clickable = value;
		_button->setPointerCursor(value);
		update();
	}, lifetime());
	std::move(comment) | rpl::map([](const SendComment &value) {
		return value.text;
	}) | rpl::distinct_until_changed() | rpl::on_next([this](
			const QString &text) {
		setText(text);
	}, lifetime());
}

int SendCommentBubble::resizeGetHeight(int newWidth) {
	if (!newWidth) {
		_button->setGeometry(QRect());
		return 0;
	}
	_layout = Ui::UniqueGiftMessageBubble::ResolveLayout(
		st::walletSendCommentBubble,
		style::margins(
			st::walletSendFieldMargin.left(),
			0,
			st::walletSendFieldMargin.right(),
			st::walletSendFieldMargin.bottom()),
		newWidth,
		_text);
	auto mirror = QTransform();
	mirror.translate(2 * QRectF(_layout.pathBounds).center().x(), 0.);
	mirror.scale(-1., 1.);
	_path = mirror.map(Ui::UniqueGiftMessageBubble::Path(
		st::walletSendCommentBubble,
		_layout));
	const auto stroke = st::lineWidth;
	_button->setGeometry(_path.boundingRect().toAlignedRect().marginsAdded(
		{ stroke, stroke, stroke, stroke }));
	const auto shift = -st::walletSendCommentBubble.tailSize.width();
	_layout.body.translate(shift, 0);
	_layout.text.translate(shift, 0);
	return _layout.sectionHeight;
}

void SendCommentBubble::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	{
		auto hq = PainterHighQualityEnabler(p);
		if (_clickable && _button->isOver()) {
			p.fillPath(_path, st::walletSendCommentBgOver);
		}
		p.setBrush(Qt::NoBrush);
		p.setPen(QPen(
			st::walletSendCommentOutline,
			st::lineWidth,
			Qt::SolidLine,
			Qt::RoundCap,
			Qt::RoundJoin));
		p.drawPath(_path);
	}
	p.setPen(st::walletSendCommentTextFg);
	_text.draw(p, {
		.position = _layout.text.topLeft(),
		.outerWidth = width(),
		.availableWidth = _layout.text.width(),
		.align = style::al_topleft,
		.elisionLines = 0,
	});
}

void SendCommentBubble::setText(const QString &text) {
	_text.setText(st::walletSendCommentTextStyle, text);
	if (width() > 0) {
		resizeToWidth(width());
	}
	update();
}

SendRecipientCard::SendRecipientCard(
	QWidget *parent,
	std::shared_ptr<Ui::Show> show,
	UserData *user,
	rpl::producer<QString> address,
	Fn<void()> about)
: RpWidget(parent)
, _user(user)
, _about(user
	? Ui::CreateChild<Ui::IconButton>(this, st::walletSendUserCardAbout)
	: nullptr)
, _copy(Ui::CreateChild<Ui::AbstractButton>(this))
, _nameStyle(st::defaultTextStyle)
, _usernameStyle(st::defaultTextStyle)
, _userpic(user ? user->createUserpicView() : Ui::PeerUserpicView()) {
	_nameStyle.font = st::walletSendUserCardNameFont;
	_usernameStyle.font = st::boxTextFont;
	if (_about) {
		_about->setClickedCallback(std::move(about));
		_about->hide();
	}
	_copy->setClickedCallback([this, show = std::move(show)] {
		CopyAddressCallback(show, _address)();
	});
	_copy->hide();

	auto name = user
		? Info::Profile::NameValue(user)
		: tr::lng_wallet_send_gram_wallet();
	std::move(name) | rpl::on_next([this](const QString &value) {
		_name.setText(_nameStyle, value, Ui::NameTextOptions());
		update();
	}, lifetime());

	if (user) {
		Info::Profile::UsernameValue(user) | rpl::on_next([this](
				const TextWithEntities &username) {
			_username.setText(
				_usernameStyle,
				username.text,
				Ui::NameTextOptions());
			update();
		}, lifetime());

		user->session().downloaderTaskFinished() | rpl::on_next([this] {
			update();
		}, lifetime());
	}

	std::move(address) | rpl::on_next([this](const QString &value) {
		setAddress(value);
	}, lifetime());
}

int SendRecipientCard::resizeGetHeight(int newWidth) {
	const auto &padding = st::walletSendUserCardPadding;
	const auto font = st::walletSendUserCardAddressFont->monospace();
	const auto lines = kAddressLength
		/ (kAddressGroup * kSendUserCardGroupsPerLine);
	const auto text = nameHeight()
		+ st::walletSendUserCardTextSkip
		+ lines * font->height;
	const auto inner = std::max(st::walletSendUserCardPhoto, text);
	if (_about) {
		_about->moveToRight(
			padding.right(),
			(padding.top() + inner + padding.bottom() - _about->height()) / 2,
			newWidth);
	}
	_copy->setGeometry(addressRect(newWidth));
	return padding.top() + inner + padding.bottom();
}

void SendRecipientCard::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(
		rect(),
		st::walletSendUserCardRadius,
		st::walletSendUserCardRadius);

	const auto &padding = st::walletSendUserCardPadding;
	const auto photo = st::walletSendUserCardPhoto;
	const auto inner = height() - padding.top() - padding.bottom();
	const auto photoTop = padding.top() + (inner - photo) / 2;
	if (_user) {
		_user->paintUserpicLeft(
			p,
			_userpic,
			padding.left(),
			photoTop,
			width(),
			photo);
	} else {
		Ui::EmptyUserpic::PaintCurrency(
			p,
			padding.left(),
			photoTop,
			width(),
			photo);
	}

	const auto left = padding.left()
		+ photo
		+ st::walletSendUserCardPhotoSkip;
	const auto about = _about
		? (_about->width() + st::walletSendUserCardAboutSkip)
		: 0;
	const auto available = width() - left - padding.right() - about;
	const auto skip = st::walletSendUserCardNameSkip;
	const auto handle = _username.isEmpty()
		? 0
		: std::min(_username.maxWidth(), (available - skip) / 2);
	const auto nameWidth = std::min(
		_name.maxWidth(),
		available - (handle ? (handle + skip) : 0));
	p.setPen(st::windowBoldFg);
	_name.drawLeftElided(p, left, padding.top(), nameWidth, width(), 1);
	if (handle) {
		p.setPen(st::windowSubTextFg);
		_username.drawLeftElided(
			p,
			left + nameWidth + skip,
			padding.top(),
			handle,
			width(),
			1);
	}
	if (!_address.isEmpty()) {
		PaintAddressGroups(
			p,
			st::walletSendUserCardAddressFont->monospace(),
			_address,
			addressRect(width()).topLeft(),
			kSendUserCardGroupsPerLine);
	}
}

int SendRecipientCard::nameHeight() const {
	const auto line = [](const style::TextStyle &text) {
		return text.lineHeight ? text.lineHeight : text.font->height;
	};
	return std::max(line(_nameStyle), line(_usernameStyle));
}

QRect SendRecipientCard::addressRect(int outerWidth) const {
	const auto &padding = st::walletSendUserCardPadding;
	const auto font = st::walletSendUserCardAddressFont->monospace();
	const auto left = padding.left()
		+ st::walletSendUserCardPhoto
		+ st::walletSendUserCardPhotoSkip;
	const auto top = padding.top()
		+ nameHeight()
		+ st::walletSendUserCardTextSkip;
	const auto blockWidth = AddressGroupsWidth(
		font,
		_address,
		kSendUserCardGroupsPerLine);
	const auto lines = kAddressLength
		/ (kAddressGroup * kSendUserCardGroupsPerLine);
	const auto x = style::RightToLeft()
		? (outerWidth - left - blockWidth)
		: left;
	return QRect(x, top, blockWidth, lines * font->height);
}

void SendRecipientCard::setAddress(const QString &address) {
	_address = address;
	if (_about) {
		_about->setVisible(!_address.isEmpty());
	}
	_copy->setGeometry(addressRect(width()));
	_copy->setVisible(!_address.isEmpty());
	update();
}

[[nodiscard]] std::optional<SendFlow> ParseRecipientFlow(
		const QString &text) {
	auto address = text;
	auto amountNano = int64(0);
	auto comment = QString();
	auto expiresAt = std::optional<uint64>();
	if (const auto link = ParseTransferLink(text)) {
		address = link->address;
		amountNano = link->amountNano;
		comment = link->comment;
		expiresAt = link->expiresAt;
	}
	const auto parsed = ParseAddress(address);
	if (!parsed || parsed->testnet) {
		return std::nullopt;
	}
	const auto friendly = FormatFriendly(parsed->raw, parsed->bounceable);
	if (friendly.isEmpty()) {
		return std::nullopt;
	}
	const auto draft = std::make_shared<SendDraft>();
	draft->comment = SendComment{ .text = std::move(comment) };
	return SendFlow{
		.destination = parsed->raw,
		.bounce = parsed->bounceable,
		.displayForm = (parsed->friendly ? address : friendly),
		.amountNano = amountNano,
		.expiresAt = expiresAt,
		.draft = draft,
	};
}

[[nodiscard]] QStringList SplitPhraseWords(const QString &text) {
	return text.simplified().split(QChar(' '), Qt::SkipEmptyParts);
}

[[nodiscard]] rpl::producer<QString> RecipientErrorText(
		RecipientError error) {
	switch (error) {
	case RecipientError::Invalid:
		return tr::lng_wallet_send_invalid_address();
	case RecipientError::NameNotFound:
		return tr::lng_wallet_send_name_not_found();
	case RecipientError::NameFailed:
	case RecipientError::LookupFailed:
		return tr::lng_wallet_send_user_load_error();
	case RecipientError::OwnWallet:
		return tr::lng_wallet_collectible_own_wallet();
	}
	Unexpected("RecipientError in RecipientErrorText.");
}

[[nodiscard]] bool IsTonDnsName(const QString &text) {
	if (text.isEmpty() || text.size() > 126) {
		return false;
	}
	const auto parts = text.split(QChar('.'));
	if (parts.size() < 2
		|| parts.back().compare(u"ton"_q, Qt::CaseInsensitive) != 0) {
		return false;
	}
	for (const auto &part : parts) {
		if (part.isEmpty()) {
			return false;
		}
		for (const auto ch : part) {
			const auto code = ch.unicode();
			const auto good = (code >= 'a' && code <= 'z')
				|| (code >= 'A' && code <= 'Z')
				|| (code >= '0' && code <= '9')
				|| (code == '-');
			if (!good) {
				return false;
			}
		}
	}
	return true;
}

[[nodiscard]] RecipientInput ClassifyRecipientInput(const QString &text) {
	using Kind = RecipientInputKind;
	if (text.isEmpty()) {
		return {};
	} else if (auto flow = ParseRecipientFlow(text)) {
		return { .kind = Kind::Address, .flow = std::move(flow) };
	} else if (IsTonDnsName(text)) {
		return { .kind = Kind::Name };
	} else if (text.contains(u"://"_q)
		|| text.startsWith(u"ton:"_q, Qt::CaseInsensitive)
		|| ParseAddress(text)
		|| ParseTransferLink(text)
		|| (text.size() > kRecipientSearchLimit)
		|| (SplitPhraseWords(text).size() >= kImportWordCountShort)) {
		// Rejected addresses and pasted secrets never reach contacts.search.
		return { .kind = Kind::Invalid };
	} else if (TextUtilities::PrepareSearchWords(text).isEmpty()) {
		return {};
	}
	return { .kind = Kind::Search };
}

[[nodiscard]] not_null<Ui::InputField*> AddCommentField(
		not_null<Ui::GenericBox*> box,
		const QString &comment) {
	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::walletCommentField,
			Ui::InputField::Mode::NoNewlines,
			tr::lng_wallet_send_comment_optional(),
			comment),
		st::walletCommentFieldMargin);
	ApplyCommentLimit(field);
	return field;
}

[[nodiscard]] not_null<Ui::InputField*> AddSendField(
		not_null<Ui::VerticalLayout*> container,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &value) {
	const auto field = container->add(
		object_ptr<Ui::InputField>(
			container,
			st,
			Ui::InputField::Mode::NoNewlines,
			std::move(placeholder),
			value),
		st::walletSendFieldMargin);
	const auto paste = Ui::CreateChild<Ui::RoundButton>(
		field,
		tr::lng_mac_menu_paste(),
		st::defaultTableSmallButton);
	paste->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	paste->setClickedCallback([=] {
		field->setFocusFast();
		field->setText(QGuiApplication::clipboard()->text().trimmed());
	});
	field->widthValue(
	) | rpl::on_next([=, &st](int) {
		paste->moveToRight(0, st.textMargins.top());
	}, paste->lifetime());
	const auto updatePaste = [=] {
		paste->setVisible(field->getLastText().isEmpty());
	};
	field->changes() | rpl::on_next(updatePaste, field->lifetime());
	updatePaste();
	return field;
}

void BindCommentField(
		not_null<Ui::InputField*> field,
		const std::shared_ptr<SendDraft> &draft) {
	field->changes() | rpl::on_next([=] {
		auto comment = draft->comment.current();
		comment.text = field->getLastText();
		draft->comment = std::move(comment);
	}, field->lifetime());
	draft->comment.value() | rpl::on_next([=](const SendComment &comment) {
		if (field->getLastText() != comment.text) {
			field->setText(comment.text);
		}
		if (!CommentFits(comment.text)) {
			field->showError();
		}
	}, field->lifetime());
}

void AddCommentPrivacy(
		not_null<Ui::VerticalLayout*> container,
		const std::shared_ptr<SendDraft> &draft,
		const style::margins &margin,
		rpl::producer<bool> encryptable) {
	const auto choice = container->add(
		object_ptr<Ui::SlideWrap<Ui::Checkbox>>(
			container,
			object_ptr<Ui::Checkbox>(
				container,
				tr::lng_wallet_comment_make_public(),
				draft->comment.current().isPublic,
				st::defaultBoxCheckbox),
			margin));
	choice->toggleOn(std::move(encryptable));
	choice->finishAnimating();
	const auto checkbox = choice->entity();
	checkbox->setAllowTextLines(0);
	checkbox->checkedChanges() | rpl::on_next([=](bool checked) {
		auto comment = draft->comment.current();
		comment.isPublic = checked;
		draft->comment = std::move(comment);
	}, checkbox->lifetime());
	draft->comment.value() | rpl::on_next([=](const SendComment &comment) {
		checkbox->setChecked(
			comment.isPublic,
			Ui::Checkbox::NotifyAboutChange::DontNotify);
	}, checkbox->lifetime());
	const auto warning = container->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			container,
			object_ptr<Ui::FlatLabel>(
				container,
				tr::lng_wallet_comment_public(),
				st::walletCommentCaptionLabel),
			st::walletCommentCaptionMargin));
	warning->toggleOn(draft->comment.value() | rpl::map([](
			const SendComment &comment) {
		return comment.isPublic;
	}));
	warning->finishAnimating();
}

void WalletSendCommentBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<SendDraft> draft,
		Fn<bool()> originValid,
		rpl::producer<bool> encryptable) {
	box->setWidth(st::boxWideWidth);
	box->setTitle(tr::lng_wallet_comment_title());
	const auto staged = std::make_shared<SendDraft>();
	staged->comment = draft->comment.current();
	const auto field = AddCommentField(box, staged->comment.current().text);
	BindCommentField(field, staged);
	AddCommentPrivacy(
		box->verticalLayout(),
		staged,
		st::walletCommentPrivacyMargin,
		std::move(encryptable));

	struct State {
		bool closed = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto weak = base::make_weak(box.get());
	box->boxClosing() | rpl::on_next([=] {
		state->closed = true;
	}, box->lifetime());
	const auto save = [=] {
		if (state->closed || !originValid()) {
			return;
		} else if (!CommentFits(staged->comment.current().text)) {
			field->showError();
			field->setFocusFast();
			return;
		}
		state->closed = true;
		auto comment = staged->comment.current();
		if (!draft->encryptable.current()) {
			comment.isPublic = true;
		}
		draft->comment = std::move(comment);
		if (const auto alive = weak.get()) {
			alive->closeBox();
		}
	};
	box->addButton(tr::lng_wallet_comment_add(), save);
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	field->submits() | rpl::on_next(save, field->lifetime());
	box->setFocusCallback([=] { field->setFocusFast(); });
	AddBoxCloseButton(box);
}

void ShowKeyChangedBox(
		std::shared_ptr<Main::SessionShow> show,
		v::text::data text) {
	show->showBox(Ui::MakeConfirmBox({
		.text = std::move(text),
		.confirmed = [=](Fn<void()> close) {
			close();
			RunKeyRequiringAction(show, [] {});
		},
		.confirmText = tr::lng_wallet_restore_title(),
		.title = tr::lng_wallet_send_key_changed_title(),
	}));
}

} // namespace ContentDetails

} // namespace Wallet
