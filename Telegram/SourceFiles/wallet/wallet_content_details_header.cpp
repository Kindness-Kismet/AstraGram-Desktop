#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

void AddDetailsAmountHeader(
		not_null<Ui::VerticalLayout*> layout,
		const TransferItem &item,
		int topSkip,
		int bottomSkip,
		rpl::producer<FiatRate> rate) {
	const auto container = layout->add(
		object_ptr<Ui::RpWidget>(layout),
		style::margins(0, topSkip, 0, bottomSkip),
		style::al_top);
	auto formatted = Ui::FormatTonAmount(item.amountNano);
	const auto negativeSign = QString(QLocale::system().negativeSign());
	if (item.amountNano < 0 && formatted.wholeString.startsWith(negativeSign)) {
		formatted.wholeString.remove(0, negativeSign.size());
	}
	const auto major = Ui::CreateChild<Ui::FlatLabel>(
		container,
		(!item.amountNano
			? QString()
			: item.incoming
			? u"+"_q
			: QString(kMinus)) + formatted.wholeString,
		st::walletDetailsAmountMajorLabel);
	const auto minor = Ui::CreateChild<Ui::FlatLabel>(
		container,
		formatted.nanoString.isEmpty()
			? QString()
			: (formatted.separator + formatted.nanoString),
		st::walletDetailsAmountMinorLabel);
	// The unit is spelled out instead of drawn as the currency mark: the
	// mark is the animation above the amount now, and saying it twice in
	// one header only makes the line harder to read.
	const auto ticker = Ui::CreateChild<Ui::FlatLabel>(
		container,
		tr::lng_action_gram_transfer_ticker(
			tr::now,
			lt_count,
			std::abs(item.amountNano / float64(Ui::kNanosInOne))).toUpper(),
		st::walletDetailsTickerLabel);
	const auto subdued = (item.status == TransferItem::Status::Pending)
		|| (item.status == TransferItem::Status::Failure);
	const auto &color = subdued
		? st::windowSubTextFg
		: item.incoming
		? st::boxTextFgGood
		: st::windowFg;
	rpl::single(rpl::empty) | rpl::then(
		style::PaletteChanged()
	) | rpl::on_next([=] {
		const auto scope = WindowPaletteScope(container);
		major->setTextColorOverride(color->c);
		minor->setTextColorOverride(color->c);
	}, container->lifetime());
	const auto amountNano = item.amountNano;
	const auto fiat = rate
		? Ui::CreateChild<Ui::FlatLabel>(
			container,
			st::walletDetailsFiatLabel)
		: nullptr;
	const auto relayout = [=] {
		const auto majorSize = major->size();
		const auto minorSize = minor->size();
		const auto tickerSize = ticker->size();
		const auto tickerSkip = st::walletDetailsTickerSkip;
		const auto amountWidth = majorSize.width()
			+ minorSize.width()
			+ tickerSkip
			+ tickerSize.width();
		// The smaller labels are dropped by the difference in font size so
		// that all three sit on one baseline with the whole amount.
		const auto minorSkip = st::walletDetailsAmountMinorSkip;
		const auto amountHeight = std::max({
			majorSize.height(),
			minorSkip + minorSize.height(),
			minorSkip + tickerSize.height(),
		});
		const auto withFiat = (fiat != nullptr);
		const auto width = std::max(
			amountWidth,
			withFiat ? fiat->width() : 0);
		const auto height = amountHeight + (withFiat
			? st::walletDetailsFiatSkip + fiat->height()
			: 0);
		container->resize(width, height);
		container->setNaturalWidth(width);
		const auto left = (width - amountWidth) / 2;
		major->moveToLeft(left, 0, width);
		minor->moveToLeft(left + majorSize.width(), minorSkip, width);
		ticker->moveToLeft(
			left + majorSize.width() + minorSize.width() + tickerSkip,
			minorSkip,
			width);
		if (withFiat) {
			fiat->moveToLeft(
				(width - fiat->width()) / 2,
				amountHeight + st::walletDetailsFiatSkip,
				width);
		}
	};
	if (fiat) {
		std::move(rate) | rpl::on_next([=](const FiatRate &value) {
			fiat->setText(FormatFiat(amountNano, value));
			relayout();
		}, fiat->lifetime());
	}
	rpl::combine(
		major->sizeValue(),
		minor->sizeValue(),
		ticker->sizeValue()
	) | rpl::on_next(relayout, container->lifetime());
}

void AddDetailsCollectibleHeader(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Main::Session*> session,
		std::shared_ptr<CollectibleMedia> media,
		const TransferItem &item,
		int bottomSkip,
		bool withCollection) {
	const auto container = layout->add(
		object_ptr<Ui::RpWidget>(layout),
		style::margins(0, st::walletDetailsAmountTopSkip, 0, bottomSkip),
		style::al_top);
	const auto address = item.collectible;
	const auto available = st::boxWideWidth
		- st::giveawayGiftCodeTableMargin.left()
		- st::giveawayGiftCodeTableMargin.right();
	const auto arrowWidth = st::walletDetailsCollectionArrowSkip
		+ st::walletDetailsCollectionArrow.width();
	const auto artwork = Ui::CreateChild<Ui::RpWidget>(container);
	artwork->resize(
		st::walletDetailsCollectibleSize,
		st::walletDetailsCollectibleSize);
	artwork->setAttribute(Qt::WA_TransparentForMouseEvents);
	artwork->paintRequest(
	) | rpl::on_next([=] {
		auto p = Painter(artwork);
		media->paint(
			p,
			address,
			artwork->rect(),
			artwork->width(),
			st::walletDetailsCollectibleRadius);
	}, artwork->lifetime());
	const auto name = Ui::CreateChild<Ui::FlatLabel>(
		container,
		st::walletCollectibleTitleLabel);
	name->setAttribute(Qt::WA_TransparentForMouseEvents);
	const auto collection = Ui::CreateChild<Ui::AbstractButton>(container);
	const auto collectionLabel = Ui::CreateChild<Ui::FlatLabel>(
		collection,
		st::walletDetailsCollectionLabel);
	collectionLabel->setAttribute(Qt::WA_TransparentForMouseEvents);
	collection->hide();
	collection->setClickedCallback([=] {
		const auto contract = media->collection(address);
		if (!contract.isEmpty()) {
			UrlClickHandler::Open(Core::TonExplorerUrl(
				session,
				FormatFriendly(contract, true)));
		}
	});
	collection->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(collection);
		const auto &arrow = st::walletDetailsCollectionArrow;
		arrow.paint(
			p,
			collectionLabel->width() + st::walletDetailsCollectionArrowSkip,
			(collection->height() - arrow.height()) / 2,
			collection->width());
	}, collection->lifetime());
	collectionLabel->sizeValue(
	) | rpl::on_next([=](QSize size) {
		collection->resize(size.width() + arrowWidth, size.height());
		collectionLabel->moveToLeft(0, 0, collection->width());
	}, collection->lifetime());
	const auto relayout = [=] {
		const auto hasCollection = withCollection
			&& !media->collection(address).isEmpty();
		collection->setVisible(hasCollection);
		const auto nameTop = st::walletDetailsCollectibleSize
			+ st::walletDetailsCollectibleNameSkip;
		const auto collectionTop = nameTop
			+ name->height()
			+ st::walletDetailsCollectionSkip;
		const auto height = hasCollection
			? (collectionTop + collection->height())
			: (nameTop + name->height());
		container->resize(available, height);
		container->setNaturalWidth(available);
		artwork->moveToLeft((available - artwork->width()) / 2, 0, available);
		name->moveToLeft((available - name->width()) / 2, nameTop, available);
		if (hasCollection) {
			collection->moveToLeft(
				(available - collection->width()) / 2,
				collectionTop,
				available);
		}
	};
	const auto apply = [=] {
		const auto view = media->view(address);
		name->setMarkedText(CollectibleTitleText(view));
		name->resizeToNaturalWidth(available);
		const auto contract = media->collection(address);
		if (!contract.isEmpty()) {
			collectionLabel->setText(view.collectionName.isEmpty()
				? ShortAddress(contract)
				: view.collectionName);
			collectionLabel->resizeToNaturalWidth(available - arrowWidth);
		}
		relayout();
	};
	const auto mine = [=](const QString &changed) {
		return (changed == address);
	};
	media->changed(
	) | rpl::filter(mine) | rpl::on_next(apply, container->lifetime());
	media->repaint(
	) | rpl::filter(mine) | rpl::on_next([=] {
		artwork->update();
	}, container->lifetime());
	rpl::combine(
		name->sizeValue(),
		collection->sizeValue()
	) | rpl::on_next(relayout, container->lifetime());
	apply();
}

[[nodiscard]] RowAvatar ActionRowAvatar(ActionRowIcon icon) {
	switch (icon) {
	case ActionRowIcon::Incoming: return RowAvatar::In;
	case ActionRowIcon::Outgoing: return RowAvatar::Out;
	case ActionRowIcon::Gear: return RowAvatar::Gear;
	}
	Unexpected("Icon in ActionRowAvatar.");
}

ActionRow::ActionRow(QWidget *parent, ActionRowArgs &&args)
: RpWidget(parent)
, _avatar(ActionRowAvatar(args.icon))
, _title(Ui::CreateChild<Ui::FlatLabel>(
	this,
	args.address.isEmpty() ? args.kind : ShortAddressForm(args.address),
	st::walletRowTitleLabel)) {
	_title->setBreakEverywhere(true);
	_title->setAttribute(Qt::WA_TransparentForMouseEvents);
	if (!args.address.isEmpty()) {
		_subtitle = Ui::CreateChild<Ui::FlatLabel>(
			this,
			args.kind,
			st::walletRowSubtitleLabel);
		_subtitle->setBreakEverywhere(true);
		_subtitle->setAttribute(Qt::WA_TransparentForMouseEvents);
	}
	if (const auto amount = args.amountNano) {
		_major = Ui::CreateChild<Ui::FlatLabel>(
			this,
			st::walletRowAmountMajorLabel);
		_major->setAttribute(Qt::WA_TransparentForMouseEvents);
		_minor = Ui::CreateChild<Ui::FlatLabel>(
			this,
			st::walletRowAmountMinorLabel);
		_minor->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto plus = (args.sign == ActionRowSign::Plus);
		const auto minus = (args.sign == ActionRowSign::Minus);
		SetRowAmountText(
			_major,
			_minor,
			*amount,
			plus ? u"+"_q : minus ? QString(kMinus) : QString());
		SetAmountColor(
			_major,
			_minor,
			(plus
				? st::boxTextFgGood
				: minus
				? st::windowBoldFg
				: st::windowSubTextFg));
	}
}

int ActionRow::resizeGetHeight(int newWidth) {
	const auto &padding = st::walletConnectActionPadding;
	const auto amountWidth = _major
		? (_major->width() + _minor->width() + st::walletRowSkip)
		: 0;
	const auto textWidth = newWidth - padding.left() - padding.right();
	_title->resizeToWidth(std::max(textWidth - amountWidth, 1));
	if (_subtitle) {
		_subtitle->resizeToWidth(std::max(textWidth, 1));
	}
	const auto textHeight = _title->height()
		+ (_subtitle ? (st::walletRowSkip + _subtitle->height()) : 0);
	const auto result = padding.top()
		+ std::max(st::walletRowIconSize, textHeight)
		+ padding.bottom();
	const auto textTop = (result - textHeight) / 2;
	_title->moveToLeft(padding.left(), textTop, newWidth);
	if (_subtitle) {
		_subtitle->moveToLeft(
			padding.left(),
			textTop + _title->height() + st::walletRowSkip,
			newWidth);
	}
	if (_major) {
		const auto lineHeight = st::walletRowTitleLabel.style.font->height;
		const auto majorTop = textTop + (lineHeight - _major->height()) / 2;
		_minor->moveToRight(
			padding.right(),
			majorTop + st::walletRowAmountMinorSkip,
			newWidth);
		_major->moveToRight(
			padding.right() + _minor->width(),
			majorTop,
			newWidth);
	}
	_circleTop = (result - st::walletRowIconSize) / 2;
	return result;
}

void ActionRow::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto size = st::walletRowIconSize;
	PaintRowAvatar(
		p,
		style::rtlrect(
			st::walletConnectActionIconLeft,
			_circleTop,
			size,
			size,
			width()),
		_avatar);
}

} // namespace ContentDetails

} // namespace Wallet
