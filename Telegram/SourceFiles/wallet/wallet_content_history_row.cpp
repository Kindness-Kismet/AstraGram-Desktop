#include "wallet/wallet_content_internal.h"

#include "extras/features/window_material/window_material.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

[[nodiscard]] object_ptr<Ui::FlatLabel> NameValueLabel(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Ui::Show> show,
		const QString &name,
		const QString &address) {
	auto result = object_ptr<Ui::FlatLabel>(
		parent,
		rpl::single(tr::link(name)),
		st::defaultTableValue);
	const auto copy = CopyAddressCallback(std::move(show), address);
	result->setClickHandlerFilter([=](const auto &...) {
		copy();
		return false;
	});
	return result;
}

[[nodiscard]] QString OnrampProvider(const TransferItem &item) {
	return (item.kind == TransferItem::Kind::Onramp)
		? item.provider
		: QString();
}

[[nodiscard]] QString ShortAddressForm(
		const QString &full,
		int chars) {
	return full.left(chars) + QChar(0x2026) + full.right(chars);
}

[[nodiscard]] QString ShortAddress(const QString &address) {
	if (address.isEmpty()) {
		return QString();
	}
	const auto full = FormatFriendly(address, true);
	return ShortAddressForm(full);
}

[[nodiscard]] QString CounterpartyAddress(const TransferItem &item) {
	return item.counterparty.isEmpty()
		? QString()
		: FormatFriendly(item.counterparty, item.counterpartyBounceable);
}

void SetAmountColor(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		const style::color &color) {
	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(major);
		major->setTextColorOverride(color->c);
		minor->setTextColorOverride(color->c);
	}, major->lifetime());
}

[[nodiscard]] QString GramMajorPart(int64 amountNano) {
	return QString::number(amountNano / Ui::kNanosInOne);
}

[[nodiscard]] QString GramMinorPart(int64 amountNano) {
	const auto tiny = TinyAmountFraction(amountNano, kGramDigits);
	if (!tiny.isEmpty()) {
		return QString(QLocale().decimalPoint()) + tiny;
	}
	const auto cents = std::abs(amountNano % Ui::kNanosInOne)
		/ (Ui::kNanosInOne / 100);
	return QString(QLocale().decimalPoint())
		+ u"%1"_q.arg(cents, 2, 10, QChar('0'));
}

[[nodiscard]] QString RowAmountSign(bool incoming) {
	return incoming ? u"+"_q : QString(kMinus);
}

[[nodiscard]] QString RowAmountWhole(int64 amountNano, const QString &sign) {
	return (amountNano ? sign : QString()) + GramMajorPart(amountNano);
}

[[nodiscard]] RowAmountText PrepareRowAmountText(
		int64 amountNano,
		const QString &sign) {
	auto helper = Ui::Text::CustomEmojiHelper();
	auto minor = tr::marked(GramMinorPart(amountNano));
	minor.append(helper.paletteDependent({
		.factory = [] {
			return Ui::Earn::IconCurrencyTwoTone(
				st::walletRowMarkSize,
				st::windowActiveTextFg->c);
		},
		.margin = st::walletRowIconMargin,
	}));
	return {
		.major = RowAmountWhole(amountNano, sign),
		.minor = std::move(minor),
		.context = helper.context(),
	};
}

[[nodiscard]] RowAmountText PrepareRowItemAmountText(bool incoming) {
	return {
		.major = ((incoming ? QChar('+') : kMinus)
			+ tr::lng_wallet_row_items(tr::now, lt_count, 1)),
		.minor = Ui::Text::IconEmoji(incoming
			? &st::walletRowItemMarkIn
			: &st::walletRowItemMarkOut),
	};
}

[[nodiscard]] const style::color &RowAmountColor(
		bool incoming,
		bool pending,
		bool failed) {
	return (pending || failed)
		? st::windowSubTextFg
		: incoming
		? st::boxTextFgGood
		: st::windowBoldFg;
}

void SetRowAmountText(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		int64 amountNano,
		const QString &sign) {
	auto text = PrepareRowAmountText(amountNano, sign);
	major->setText(text.major);
	minor->setMarkedText(std::move(text.minor), std::move(text.context));
}

void SetRowAmount(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		int64 amountNano,
		bool incoming,
		bool pending,
		bool failed) {
	SetRowAmountText(
		major,
		minor,
		amountNano,
		RowAmountSign(incoming));
	SetAmountColor(major, minor, RowAmountColor(incoming, pending, failed));
}

void SetRowItemAmount(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		bool incoming,
		bool failed) {
	auto text = PrepareRowItemAmountText(incoming);
	major->setText(text.major);
	minor->setMarkedText(std::move(text.minor), std::move(text.context));
	SetAmountColor(major, minor, RowAmountColor(incoming, false, failed));
}

void PaintRowAvatar(Painter &p, QRect rect, RowAvatar avatar) {
	auto hq = PainterHighQualityEnabler(p);
	const auto in = (avatar == RowAvatar::In);
	const auto flat = (avatar == RowAvatar::KeyChange)
		|| (avatar == RowAvatar::Gear);
	if (flat) {
		p.setBrush(st::historyPeerArchiveUserpicBg);
	} else {
		const auto &top = in
			? st::historyPeer2UserpicBg
			: st::historyPeer4UserpicBg;
		const auto &bottom = in
			? st::historyPeer2UserpicBg2
			: st::historyPeer4UserpicBg2;
		auto gradient = QLinearGradient(
			rect.topLeft(),
			rect.bottomLeft());
		gradient.setStops({ { 0., top->c }, { 1., bottom->c } });
		p.setBrush(gradient);
	}
	p.setPen(Qt::NoPen);
	p.drawEllipse(rect);
	const auto icon = in
		? &st::walletRowArrowIn
		: (avatar == RowAvatar::KeyChange)
		? &st::walletRowKeyIcon
		: (avatar == RowAvatar::Gear)
		? &st::walletRowGearIcon
		: &st::walletRowArrowOut;
	icon->paintInCenter(p, rect);
}

[[nodiscard]] int LabelLineHeight(const style::FlatLabel &st) {
	return std::max(st.style.font->height, st.style.lineHeight);
}

[[nodiscard]] int HistoryRowTitleSkip(int amountWidth) {
	return amountWidth + st::walletRowSkip;
}

[[nodiscard]] HistoryRowLayout ComputeHistoryRowLayout(
		const HistoryRowHeights &heights) {
	const auto &padding = st::walletRowPadding;
	auto result = HistoryRowLayout();
	auto top = padding.top();
	result.title = {
		.top = top,
		.lines = heights.title / LabelLineHeight(st::walletRowTitleLabel),
	};
	top += heights.title;
	if (heights.subtitle) {
		top += st::walletRowSkip;
		result.subtitle = {
			.top = top,
			.lines = (*heights.subtitle
				/ LabelLineHeight(st::walletRowSubtitleLabel)),
		};
		top += *heights.subtitle;
	}
	top += st::walletRowSkip;
	result.date = {
		.top = top,
		.lines = heights.date / LabelLineHeight(st::walletRowDateLabel),
	};
	result.height = top + heights.date + padding.bottom();
	result.avatarCenter = heights.subtitle
		? (padding.top()
			+ (heights.title + st::walletRowSkip + *heights.subtitle) / 2)
		: (result.height / 2);
	result.amountTop = padding.top() + (heights.title - heights.amount) / 2;
	return result;
}

// Built as Ui::FlatLabel builds its text, so it measures as the label does.
[[nodiscard]] Ui::Text::String HistoryRowLabelText(
		const style::FlatLabel &st,
		const QString &text) {
	return Ui::Text::String(
		st.style,
		text,
		kPlainTextOptions,
		st.minWidth ? st.minWidth : Ui::kQFixedMax);
}

[[nodiscard]] int HistoryRowLabelHeight(
		const style::FlatLabel &st,
		const Ui::Text::String &text,
		int width,
		bool breakEverywhere) {
	const auto full = text.countHeight(width, breakEverywhere);
	return st.maxHeight ? std::min(full, st.maxHeight) : full;
}

[[nodiscard]] HistoryRowText PrepareHistoryRowText(
		const HistoryRowContent &content,
		Fn<void()> repaint) {
	auto amount = content.itemAmount
		? PrepareRowItemAmountText(content.incoming)
		: PrepareRowAmountText(
			content.amountNano,
			RowAmountSign(content.incoming));
	if (repaint) {
		amount.context.repaint = std::move(repaint);
	}
	auto result = HistoryRowText{
		.title = HistoryRowLabelText(st::walletRowTitleLabel, content.title),
		.subtitle = HistoryRowLabelText(
			st::walletRowSubtitleLabel,
			content.subtitle),
		.date = HistoryRowLabelText(st::walletRowDateLabel, content.date),
		.major = HistoryRowLabelText(
			st::walletRowAmountMajorLabel,
			amount.major),
		.subtitleShown = !content.subtitle.isEmpty(),
		.chip = content.itemAmount,
	};
	result.minor.setMarkedText(
		st::walletRowAmountMinorLabel.style,
		amount.minor,
		kMarkupTextOptions,
		amount.context);
	return result;
}

[[nodiscard]] HistoryRowLayout MeasureHistoryRow(
		const HistoryRowText &text,
		int width) {
	const auto &padding = st::walletRowPadding;
	const auto textWidth = std::max(
		width - padding.left() - padding.right(),
		0);
	const auto titleWidth = std::max(
		textWidth - HistoryRowTitleSkip(
			text.major.maxWidth() + text.minor.maxWidth()),
		0);
	auto result = ComputeHistoryRowLayout({
		.title = HistoryRowLabelHeight(
			st::walletRowTitleLabel,
			text.title,
			titleWidth,
			true),
		.subtitle = (text.subtitleShown
			? std::make_optional(HistoryRowLabelHeight(
				st::walletRowSubtitleLabel,
				text.subtitle,
				textWidth,
				true))
			: std::nullopt),
		.date = HistoryRowLabelHeight(
			st::walletRowDateLabel,
			text.date,
			textWidth,
			false),
		.amount = HistoryRowLabelHeight(
			st::walletRowAmountMajorLabel,
			text.major,
			text.major.maxWidth(),
			false),
	});
	result.titleWidth = titleWidth;
	result.textWidth = textWidth;
	if (text.chip) {
		result.chipTop = result.height
			- padding.bottom()
			+ st::walletChipTopSkip;
		result.height += st::walletChipTopSkip + st::walletRowIconSize;
	}
	return result;
}

[[nodiscard]] RowAmountPlacement PlaceRowAmount(
		const HistoryRowText &text,
		int amountTop,
		int width) {
	const auto rtl = style::RightToLeft();
	const auto mirror = [&](int left, int size) {
		return rtl ? (width - left - size) : left;
	};
	const auto majorWidth = text.major.maxWidth();
	const auto minorWidth = text.minor.maxWidth();
	const auto minorLeft = width - st::walletRowPadding.right() - minorWidth;
	return {
		.major = QPoint(
			mirror(minorLeft - majorWidth, majorWidth),
			amountTop),
		.minor = QPoint(
			mirror(minorLeft, minorWidth),
			amountTop + st::walletRowAmountMinorSkip),
	};
}

void HistoryRowButton::setPaintUnderRipple(Fn<void(Painter&)> paint) {
	_paintUnderRipple = std::move(paint);
	update();
}

void HistoryRowButton::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto over = (isOver() || isDown()) && !isDisabled();
	paintBg(p, e->rect(), over);
	if (_paintUnderRipple) {
		_paintUnderRipple(p);
	}
	paintRipple(p, 0, 0);
	const auto outerw = width();
	paintText(p, over, outerw);
	paintToggle(p, outerw);
}

void PaintHistoryRowChipPlate(
		Painter &p,
		int outerWidth,
		const HistoryRowChipState &state) {
	const auto side = st::walletRowIconSize;
	const auto radius = st::walletCollectibleThumbRadius;
	const auto plate = std::min(state.natural, outerWidth);
	const auto plateLeft = style::RightToLeft() ? (outerWidth - plate) : 0;
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::windowBgOver);
	p.drawRoundedRect(
		QRect(plateLeft, 0, plate, side),
		radius,
		radius);
}

void PaintHistoryRowChipArtwork(
		Painter &p,
		int outerWidth,
		const HistoryRowChipState &state,
		const std::shared_ptr<CollectibleMedia> &media,
		const QString &address) {
	const auto side = st::walletRowIconSize;
	const auto radius = st::walletCollectibleThumbRadius;
	const auto rtl = style::RightToLeft();
	const auto square = QRect(rtl ? (outerWidth - side) : 0, 0, side, side);
	const auto dark = (state.kind == Gram::NftKind::TelegramUsername)
		|| (state.kind == Gram::NftKind::TelegramNumber);
	if (dark) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::callBgOpaque);
		p.drawRoundedRect(square, radius, radius);
	}
	if (state.kind == Gram::NftKind::TelegramUsername) {
		st::walletChipUsernameIcon.paintInCenter(p, square);
	} else if (state.kind == Gram::NftKind::TelegramNumber) {
		st::walletChipNumberIcon.paintInCenter(p, square);
	} else {
		media->paintArtwork(p, address, square, outerWidth, radius);
	}
}

void PaintHistoryRowChipText(
		Painter &p,
		int outerWidth,
		const HistoryRowChipState &state) {
	const auto side = st::walletRowIconSize;
	const auto plate = std::min(state.natural, outerWidth);
	const auto rtl = style::RightToLeft();
	const auto plateLeft = rtl ? (outerWidth - plate) : 0;
	const auto available = plate
		- side
		- st::walletChipTextSkip
		- st::walletChipPadding.right();
	if (available <= 0) {
		return;
	}
	const auto textLeft = rtl
		? (plateLeft + st::walletChipPadding.right())
		: (side + st::walletChipTextSkip);
	const auto titleHeight = st::walletCollectibleTitleStyle.font->height;
	const auto subtitleHeight = st::walletRowDateLabel.style.font->height;
	const auto top = (side
		- titleHeight
		- st::walletRowSkip
		- subtitleHeight) / 2;
	p.setPen(st::windowBoldFg);
	state.title.draw(p, {
		.position = { textLeft, top },
		.outerWidth = outerWidth,
		.availableWidth = available,
		.palette = &st::walletCollectibleTitlePalette,
		.elisionLines = 1,
	});
	p.setPen(st::windowSubTextFg);
	state.subtitle.draw(p, {
		.position = { textLeft, top + titleHeight + st::walletRowSkip },
		.outerWidth = outerWidth,
		.availableWidth = available,
		.elisionLines = 1,
	});
}

void TrackHistoryRowChip(
		not_null<HistoryRowChipState*> state,
		std::shared_ptr<CollectibleMedia> media,
		QString address,
		Fn<void()> repaint,
		rpl::lifetime &lifetime) {
	const auto refresh = [=] {
		const auto view = media->view(address);
		state->kind = view.kind;
		using Kind = Gram::NftKind;
		state->title.setMarkedText(
			st::walletCollectibleTitleStyle,
			((view.kind == Kind::TelegramUsername)
				? Ui::Text::Semibold('@' + view.key)
				: (view.kind == Kind::TelegramNumber)
				? Ui::Text::Semibold(Ui::FormatPhone(view.key))
				: CollectibleTitleText(view)));
		state->subtitle.setText(
			st::walletRowDateLabel.style,
			CollectibleKindText(view));
		state->natural = st::walletRowIconSize
			+ st::walletChipTextSkip
			+ std::max(state->title.maxWidth(), state->subtitle.maxWidth())
			+ st::walletChipPadding.right();
		repaint();
	};
	const auto mine = [=](const QString &changed) {
		return (changed == address);
	};
	media->changed(
	) | rpl::filter(mine) | rpl::on_next(refresh, lifetime);
	media->repaint(
	) | rpl::filter(mine) | rpl::on_next(repaint, lifetime);
	media->resolveBackground(address);
	refresh();
}

void AddHistoryRowChip(
		not_null<Ui::VerticalLayout*> inner,
		not_null<HistoryRowButton*> button,
		std::shared_ptr<CollectibleMedia> media,
		QString address) {
	Ui::AddSkip(inner, st::walletChipTopSkip);
	const auto chip = inner->add(object_ptr<Ui::FixedHeightWidget>(
		inner,
		st::walletRowIconSize));
	chip->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto state = chip->lifetime().make_state<HistoryRowChipState>();
	const auto repaint = [=] {
		chip->update();
		button->update(Ui::MapFrom(button, chip, chip->rect()));
	};
	chip->paintRequest(
	) | rpl::on_next([=] {
		auto p = Painter(chip);
		PaintHistoryRowChipArtwork(p, chip->width(), *state, media, address);
		PaintHistoryRowChipText(p, chip->width(), *state);
	}, chip->lifetime());
	button->setPaintUnderRipple([=](Painter &p) {
		const auto origin = Ui::MapFrom(button, chip, QPoint());
		p.translate(origin);
		PaintHistoryRowChipPlate(p, chip->width(), *state);
		p.translate(-origin);
	});
	TrackHistoryRowChip(state, media, address, repaint, chip->lifetime());
}

not_null<Ui::RpWidget*> AddHistoryRow(
		not_null<Ui::VerticalLayout*> list,
		const HistoryRowContent &content,
		Fn<void()> clicked,
		std::shared_ptr<CollectibleMedia> media) {
	const auto wrap = list->add(
		object_ptr<Ui::PaddingWrap<Ui::VerticalLayout>>(
			list,
			object_ptr<Ui::VerticalLayout>(list),
			st::walletRowPadding));
	const auto inner = wrap->entity();
	inner->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto button = Ui::CreateChild<HistoryRowButton>(
		wrap,
		rpl::single(QString()));
	ExtrasFeatures::WindowMaterial::watchSurface(button);
	button->setClickedCallback(std::move(clicked));
	const auto major = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		st::walletRowAmountMajorLabel);
	major->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto minor = Ui::CreateChild<Ui::FlatLabel>(
		wrap,
		st::walletRowAmountMinorLabel);
	minor->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (content.itemAmount) {
		SetRowItemAmount(major, minor, content.incoming, content.failed);
	} else {
		SetRowAmount(
			major,
			minor,
			content.amountNano,
			content.incoming,
			content.pending,
			content.failed);
	}
	const auto title = inner->add(
		object_ptr<Ui::FlatLabel>(
			inner,
			content.title,
			st::walletRowTitleLabel),
		{ 0, 0, HistoryRowTitleSkip(major->width() + minor->width()), 0 });
	title->setBreakEverywhere(true);
	auto subtitle = (Ui::FlatLabel*)nullptr;
	if (!content.subtitle.isEmpty()) {
		Ui::AddSkip(inner, st::walletRowSkip);
		subtitle = inner->add(object_ptr<Ui::FlatLabel>(
			inner,
			content.subtitle,
			st::walletRowSubtitleLabel));
		subtitle->setBreakEverywhere(true);
	}
	Ui::AddSkip(inner, st::walletRowSkip);
	const auto date = inner->add(object_ptr<Ui::FlatLabel>(
		inner,
		content.date,
		st::walletRowDateLabel));
	const auto hasChip = content.itemAmount && (media != nullptr);
	if (hasChip) {
		AddHistoryRowChip(inner, button, media, content.collectible);
	}

	const auto circle = Ui::CreateChild<Ui::RpWidget>(wrap);
	circle->resize(st::walletRowIconSize, st::walletRowIconSize);
	circle->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (const auto peer = content.peer) {
		const auto userpic = circle->lifetime().make_state<
			Ui::PeerUserpicView>(peer->createUserpicView());
		peer->session().downloaderTaskFinished(
		) | rpl::on_next([=] {
			circle->update();
		}, circle->lifetime());
		circle->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(circle);
			peer->paintUserpicLeft(
				p,
				*userpic,
				0,
				0,
				circle->width(),
				circle->width());
		}, circle->lifetime());
	} else {
		const auto avatar = content.avatar;
		circle->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(circle);
			PaintRowAvatar(p, circle->rect(), avatar);
		}, circle->lifetime());
	}
	Ui::ToggleChildrenVisibility(wrap, true);
	wrap->geometryValue(
	) | rpl::on_next([=](const QRect &g) {
		const auto layout = ComputeHistoryRowLayout({
			.title = title->height(),
			.subtitle = (subtitle
				? std::make_optional(subtitle->height())
				: std::nullopt),
			.date = date->height(),
			.amount = major->height(),
		});
		circle->moveToLeft(
			st::walletRowIconLeft,
			layout.avatarCenter - circle->height() / 2);
		minor->moveToRight(
			st::walletRowPadding.right(),
			layout.amountTop + st::walletRowAmountMinorSkip);
		major->moveToRight(
			st::walletRowPadding.right() + minor->width(),
			layout.amountTop);
		button->resize(g.size());
		button->lower();
	}, wrap->lifetime());
	return wrap;
}

[[nodiscard]] HistoryRowContent RowContentFromItem(
		const TransferItem &item,
		not_null<Main::Session*> session) {
	using Kind = TransferItem::Kind;
	const auto date = item.date
		? langDateTime(base::unixtime::parse(*item.date))
		: QString();
	if (ShowsCollectible(item)) {
		const auto peer = item.counterpartyPeer
			? session->data().peerLoaded(PeerId(item.counterpartyPeer))
			: nullptr;
		const auto hasCounterparty = !item.counterparty.isEmpty();
		const auto domain = hasCounterparty
			? item.counterpartyName.trimmed()
			: QString();
		const auto kindText = item.incoming
			? tr::lng_wallet_row_collectible_in(tr::now)
			: tr::lng_wallet_row_collectible_out(tr::now);
		const auto titleIsKind = !peer && !hasCounterparty;
		const auto statusText = RowStatusSubtitle(item.status);
		return {
			.title = (peer
				? peer->name()
				: !domain.isEmpty()
				? domain
				: hasCounterparty
				? ShortAddressForm(CounterpartyAddress(item))
				: kindText),
			.subtitle = (!statusText.isEmpty()
				? statusText
				: titleIsKind
				? QString()
				: kindText),
			.date = date,
			.incoming = item.incoming,
			.failed = (item.status == TransferItem::Status::Failure),
			.avatar = (peer
				? RowAvatar::Peer
				: item.incoming
				? RowAvatar::In
				: RowAvatar::Out),
			.peer = peer,
			.itemAmount = true,
			.collectible = item.collectible,
		};
	}
	const auto pending
		= (item.status == TransferItem::Status::Pending);
	const auto failed
		= (item.status == TransferItem::Status::Failure);
	const auto statusText = RowStatusSubtitle(item.status);
	const auto transfer = (item.kind == Kind::Transfer)
		|| (item.kind == Kind::PeerTransfer)
		|| (item.kind == Kind::Onramp);
	const auto address = transfer
		? CounterpartyAddress(item)
		: QString();
	const auto domain = !address.isEmpty()
		? item.counterpartyName.trimmed()
		: QString();
	const auto provider = !address.isEmpty()
		? OnrampProvider(item)
		: QString();
	if (!provider.isEmpty()) {
		return {
			.title = provider,
			.subtitle = (!statusText.isEmpty()
				? statusText
				: item.incoming
				? tr::lng_wallet_row_topup(tr::now)
				: tr::lng_wallet_row_withdrawal(tr::now)),
			.date = date,
			.amountNano = item.amountNano,
			.incoming = item.incoming,
			.pending = pending,
			.failed = failed,
			.avatar = (item.incoming ? RowAvatar::In : RowAvatar::Out),
		};
	}
	if (item.kind == Kind::PeerTransfer && item.counterpartyPeer) {
		const auto peer = session->data().peerLoaded(
			PeerId(item.counterpartyPeer));
		if (peer) {
			return {
				.title = peer->name(),
				.subtitle = (!statusText.isEmpty()
					? statusText
					: !domain.isEmpty()
					? domain
					: item.incoming
					? tr::lng_wallet_row_incoming(tr::now)
					: tr::lng_wallet_row_outgoing(tr::now)),
				.date = date,
				.amountNano = item.amountNano,
				.incoming = item.incoming,
				.pending = pending,
				.failed = failed,
				.avatar = RowAvatar::Peer,
				.peer = peer,
			};
		}
	}
	if (item.kind == Kind::KeyChange) {
		// A zero served amount leaves the paid fee as the wallet's outflow.
		const auto outflow = (!item.amountNano
			&& item.feeNano
			&& *item.feeNano > 0)
			? *item.feeNano
			: item.amountNano;
		return {
			.title = tr::lng_wallet_row_key_change(tr::now),
			.subtitle = statusText,
			.date = date,
			.amountNano = outflow,
			.incoming = item.incoming,
			.pending = pending,
			.failed = failed,
			.avatar = RowAvatar::KeyChange,
		};
	}
	const auto collectible = (item.kind == Kind::Collectible);
	const auto hasCounterparty = transfer
		? !address.isEmpty()
		: !item.counterparty.isEmpty();
	const auto kindText = collectible
		? (item.incoming
			? tr::lng_wallet_row_collectible_in(tr::now)
			: tr::lng_wallet_row_collectible_out(tr::now))
		: item.incoming
		? tr::lng_wallet_row_deposit(tr::now)
		: tr::lng_wallet_row_withdrawal(tr::now);
	return {
		.title = (hasCounterparty
			? (!domain.isEmpty()
				? domain
				: ShortAddressForm(transfer
					? address
					: CounterpartyAddress(item)))
			: kindText),
		.subtitle = (!statusText.isEmpty()
			? statusText
			: hasCounterparty
			? kindText
			: QString()),
		.date = date,
		.amountNano = item.amountNano,
		.incoming = item.incoming,
		.pending = pending,
		.failed = failed,
		.avatar = (item.incoming ? RowAvatar::In : RowAvatar::Out),
	};
}

// WHY: it plays once when the box finishes showing and a click replays it
// once it has stopped, with no pointer cursor or anything else saying so,
// because finding that out is the whole of it.
void AddWalletLottie(
		not_null<Ui::GenericBox*> box,
		const style::margins &margin,
		Ui::VerticalLayout *container) {
	const auto into = container ? container : box->verticalLayout().get();
	const auto size = st::walletDetailsLottieSize;
	auto icon = Settings::CreateLottieIcon(
		into,
		{
			.name = u"gram"_q,
			.sizeOverride = { size, size },
		},
		margin);
	const auto raw = icon.widget.data();
	const auto animate = icon.animate;
	const auto animating = icon.animating;
	into->add(std::move(icon.widget));
	const auto replay = Ui::CreateChild<Ui::AbstractButton>(raw);
	replay->setPointerCursor(false);
	replay->setClickedCallback([=] {
		if (!animating()) {
			animate(anim::repeat::once);
		}
	});
	// The rect the icon paints into: centered in the row, under the top margin.
	raw->sizeValue() | rpl::on_next([=](QSize outer) {
		replay->setGeometry(
			(outer.width() - size) / 2,
			margin.top(),
			size,
			size);
	}, replay->lifetime());
	box->showFinishes() | rpl::on_next([=] {
		animate(anim::repeat::once);
	}, raw->lifetime());
}

} // namespace ContentDetails

} // namespace Wallet
