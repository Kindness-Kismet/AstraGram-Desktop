#include "history/view/media/history_view_gram_transfer_internal.h"

namespace HistoryView {

namespace GramTransferInternal {}

using namespace GramTransferInternal;

namespace GramTransferInternal {

[[nodiscard]] QColor CardTickerFg() {
	return QColor(0x0f, 0xdd, 0xff);
}

[[nodiscard]] QColor CardAddressFg() {
	return QColor(0x00, 0x5e, 0xda);
}

[[nodiscard]] QColor SentBadgeBg() {
	return QColor(0x4a, 0xb4, 0x4a);
}

[[nodiscard]] QColor ReceivedBadgeBg() {
	return QColor(0x5e, 0xc2, 0xff);
}

[[nodiscard]] QWidget *PaintWidget(const QPainter &p) {
	const auto device = p.device();
	return (device && device->devType() == QInternal::Widget)
		? static_cast<QWidget*>(device)
		: nullptr;
}

[[nodiscard]] Wallet::ClockStyle CardClockStyle() {
	return {
		.size = st::walletChatCardClockSize,
		.stroke = st::walletChatCardClockStroke,
		.minuteHand = st::walletChatCardClockMinuteHand,
		.hourHand = st::walletChatCardClockHourHand,
	};
}

[[nodiscard]] std::unique_ptr<Lottie::Icon> MakeCardMark(
		const QString &name) {
	const auto size = st::walletChatCardMarkPaintSize;
	return Lottie::MakeIcon({
		.name = name,
		.sizeOverride = { size, size },
	});
}

[[nodiscard]] bool PlayingFast(const CardTransition &transition) {
	return transition.fast && transition.fast->animating();
}

[[nodiscard]] float64 SendingAngle(float64 from, crl::time elapsed) {
	return from + kSendingSpeed * elapsed / 1000.;
}

[[nodiscard]] float64 ClockwiseRemainder(float64 degrees) {
	return degrees - kSweepPeriod * std::floor(degrees / kSweepPeriod);
}

[[nodiscard]] SettleSpin StartSpin(float64 from, float64 target) {
	return {
		.from = from,
		.turn = kSpinTurn + ClockwiseRemainder(target - from),
		.target = target,
	};
}

// WHY: the live target's drift since the settle is added scaled by the
// eased progress, so the spin lands exactly where the mouse rule points now
// while a moving cursor never makes the sweep jump.
[[nodiscard]] float64 SpinAngle(
		const SettleSpin &spin,
		float64 target,
		crl::time elapsed) {
	const auto progress = std::clamp(
		elapsed / float64(kSpinDuration),
		0.,
		1.);
	return spin.from
		+ (spin.turn + target - spin.target) * anim::easeOutCubic(1., progress);
}

[[nodiscard]] int64 LoopPosition(
		not_null<Lottie::Icon*> icon,
		crl::time started,
		crl::time now) {
	const auto elapsed = std::max(now - started, crl::time(0));
	return int64(base::SafeRound(elapsed * icon->frameRate() / 1000.));
}

[[nodiscard]] crl::time NextLoopStart(
		not_null<Lottie::Icon*> icon,
		crl::time started,
		crl::time now) {
	const auto frames = icon->framesCount();
	const auto rate = icon->frameRate();
	const auto position = LoopPosition(icon, started, now);
	if (frames < 1 || rate < 1. || !(position % frames)) {
		return now;
	}
	const auto next = (position / frames + 1) * frames;
	return started + crl::time(std::ceil((next - 0.5) * 1000. / rate));
}

[[nodiscard]] Ui::StarBurstDescriptor CardBurstDescriptor() {
	const auto side = [](float64 sign) {
		return Ui::StarBurstSide{
			.sign = sign,
			.count = kBurstStarsPerSide,
			.angle = { -40., 12. },
			.reach = { 0.25, 0.65 },
		};
	};
	return {
		.sides = { side(-1.), side(1.) },
		.delay = kBurstDelay,
		.spread = kBurstSpread,
		.lifeMin = kBurstLifeMin,
		.lifeMax = kBurstLifeMax,
		.fall = { 0.20, 0.40 },
		.startX = { 0.35, 0.65 },
		.startY = { -0.25, 0.20 },
		.size = { 0.010, 0.034 },
		.alpha = { 0.40, 0.70 },
		.twinkle = { 0.1, 1.9 },
		.appearTill = kBurstAppearTill,
		.fadeAfter = kBurstFadeAfter,
		.deformation = kBurstDeformation,
	};
}

[[nodiscard]] GramTransferAction SnapshotGramTransfer(
		not_null<HistoryItem*> item) {
	const auto transfer = item->Get<HistoryServiceGramTransfer>();
	if (!transfer) {
		return {};
	}
	return {
		.itemId = item->fullId(),
		.amount = transfer->amount,
		.address = transfer->peerAddress,
		.transactionId = transfer->transactionId,
		.comment = transfer->comment,
		.failReason = transfer->failReason,
		.outgoing = item->out(),
		.encrypted = transfer->commentEncrypted,
		.failed = item->hasFailed(),
	};
}

[[nodiscard]] HistoryItem *CurrentGramTransfer(
		const GramTransferOrigin &origin) {
	if (!origin.session
		|| !origin.view
		|| !origin.media
		|| origin.session->account().loggingOut()
		|| origin.session->account().destroyingSession()
		|| origin.session->account().maybeSession() != origin.session.get()
		|| origin.view->media() != origin.media.get()) {
		return nullptr;
	}
	const auto item = origin.session->data().message(origin.action.itemId);
	return (item
		&& origin.view->data() == item
		&& item->Has<HistoryServiceGramTransfer>()
		&& SnapshotGramTransfer(item) == origin.action)
		? item
		: nullptr;
}

[[nodiscard]] rpl::producer<> GramTransferInvalidations(
		const GramTransferOrigin &origin) {
	const auto data = &origin.session->data();
	return rpl::merge(
		data->itemDataChanges() | rpl::filter([=](not_null<HistoryItem*> item) {
			return item->fullId() == origin.action.itemId
				&& !CurrentGramTransfer(origin);
		}) | rpl::to_empty,
		data->itemViewRefreshRequest(
		) | rpl::filter([=](not_null<const HistoryItem*> item) {
			return item->fullId() == origin.action.itemId
				&& !CurrentGramTransfer(origin);
		}) | rpl::to_empty,
		data->itemRemoved(origin.action.itemId) | rpl::to_empty,
		data->itemIdChanged() | rpl::filter([=](Data::Session::IdChange change) {
			return change.oldId == origin.action.itemId.msg
				&& change.newId.peer == origin.action.itemId.peer;
		}) | rpl::to_empty,
		data->viewAboutToBeRemoved(
		) | rpl::filter([=](const Data::ViewRemoval &removal) {
			return removal.view == origin.view.get();
		}) | rpl::to_empty,
		origin.session->account().sessionChanges(
		) | rpl::filter([=](Main::Session *session) {
			return session != origin.session.get();
		}) | rpl::to_empty);
}

[[nodiscard]] auto GramTransferShow(
		const GramTransferOrigin &origin,
		const ClickContext &context)
-> std::shared_ptr<Main::SessionShow> {
	const auto my = context.other.value<ClickHandlerContext>();
	const auto controller = my.sessionWindow.get();
	if (context.button != Qt::LeftButton
		|| !controller
		|| &controller->session() != origin.session.get()
		|| my.itemId != origin.action.itemId
		|| !CurrentGramTransfer(origin)) {
		return nullptr;
	}
	return controller->uiShow();
}

[[nodiscard]] GramTransferDetails ResolveGramTransfer(
		not_null<Main::Session*> session,
		const GramTransferAction &action) {
	auto result = GramTransferDetails();
	auto &item = result.item;
	item.source = Wallet::TransferItem::Source::Server;
	item.id = action.transactionId;
	// The id is the root of the transfer's trace, which the explorer shows.
	if (!action.transactionId.isEmpty()) {
		item.traceId = Wallet::TransactionHashFromServer(
			action.transactionId);
	}
	item.incoming = !action.outgoing;
	item.amountNano = (action.amount < 0
		&& action.amount != std::numeric_limits<int64>::min())
		? -action.amount
		: action.amount;
	if (const auto parsed = Wallet::ParseAddress(action.address)) {
		item.counterparty = parsed->raw;
		item.counterpartyBounceable = parsed->friendly && parsed->bounceable;
	}
	if (!session->data().peer(action.itemId.peer)->isNotificationsUser()) {
		// The chat names the counterparty before the served record does.
		item.kind = Wallet::TransferItem::Kind::PeerTransfer;
		item.counterpartyPeer = action.itemId.peer.value;
	}
	item.commentEncrypted = action.encrypted;
	if (!action.encrypted) {
		item.comment = action.comment;
	} else if (IsClientMsgId(action.itemId.msg)) {
		// The draft this device made for a transfer it is sending carries
		// the private comment the user typed: only a served transfer
		// carries a payload, because only the server assigns one.
		item.comment = action.comment;
	} else {
		item.encryptedPayload = Wallet::DecodeServerEncryptedComment(
			action.comment);
		if (!item.encryptedPayload.isEmpty()) {
			item.encryptedFormat
				= Wallet::TransferItem::EncryptedFormat::ServerPayload;
		}
	}
	// The message's own date stands in for the transaction's until the
	// served record names the moment the chain accepted it, which is the
	// same moment give or take the delivery.
	if (const auto message = session->data().message(action.itemId)) {
		item.date = message->date();
	}
	if (action.failed) {
		item.status = Wallet::TransferItem::Status::Failure;
		item.failureReason = action.failReason;
	}
	if (item.id.isEmpty() || item.counterparty.isEmpty()) {
		return result;
	}
	item.walletIdentity = session->wallet().transferWalletIdentity();
	const auto &history = session->wallet().history();
	const Wallet::TransferItem *match = nullptr;
	for (const auto &entry : history) {
		if (entry.source != Wallet::TransferItem::Source::Server
			|| entry.id != item.id) {
			continue;
		} else if (match) {
			return result;
		}
		match = &entry;
	}
	using Kind = Wallet::TransferItem::Kind;
	if (!match
		|| (match->kind != Kind::Transfer && match->kind != Kind::PeerTransfer)
		|| match->incoming != item.incoming
		|| match->amountNano != item.amountNano
		|| Wallet::CanonicalAddress(match->counterparty) != item.counterparty
		|| match->commentEncrypted != item.commentEncrypted
		|| (item.commentEncrypted
			? (match->encryptedFormat != item.encryptedFormat
				|| match->encryptedPayload != item.encryptedPayload)
			: (match->comment != item.comment))) {
		return result;
	}
	item = *match;
	result.partial = false;
	return result;
}

[[nodiscard]] Wallet::AmountStyle CardAmountStyle() {
	return {
		.big = st::walletCardBalanceMajorLabel.style.font,
		.small = st::walletCardBalanceMinorLabel.style.font,
		.tickerSkip = st::walletCardTickerSkip,
		.hinted = true,
	};
}

[[nodiscard]] Wallet::AmountParts SignedAmount(int64 value, bool outgoing) {
	const auto formatted = Ui::FormatTonAmount(value);
	auto whole = formatted.wholeString;
	const auto negativeSign = QString(QLocale::system().negativeSign());
	if (value < 0 && whole.startsWith(negativeSign)) {
		whole.remove(0, negativeSign.size());
	}
	return {
		.whole = (outgoing ? QChar(0x2212) : QChar('+')) + whole,
		.fraction = formatted.separator + formatted.nanoString,
		.ticker = tr::lng_action_gram_transfer_ticker(
			tr::now,
			lt_count,
			std::abs(value / float64(Ui::kNanosInOne))),
	};
}

[[nodiscard]] TransferTag ResolveTag(bool outgoing, bool failed) {
	if (!outgoing) {
		return {
			.text = tr::lng_action_gram_transfer_received_tag(tr::now),
			.bg = ReceivedBadgeBg(),
		};
	} else if (failed) {
		return {
			.text = tr::lng_action_gram_transfer_failed_tag(tr::now),
			.bg = Info::PeerGifts::BurnedBadgeBg(),
		};
	}
	return {
		.text = tr::lng_action_gram_transfer_sent_tag(tr::now),
		.bg = SentBadgeBg(),
	};
}

[[nodiscard]] int OutgoingRibbonTextWidth() {
	const auto &font = st::msgServiceGiftBoxBadgeFont;
	return std::max(
		font->width(tr::lng_action_gram_transfer_sent_tag(tr::now)),
		font->width(tr::lng_action_gram_transfer_failed_tag(tr::now)));
}

[[nodiscard]] RibbonGeometry ComputeRibbon(int textWidth) {
	const auto &font = st::msgServiceGiftBoxBadgeFont;
	const auto padding = st::chatUniqueGiftBadgePadding;
	auto result = RibbonGeometry();
	result.textWidth = textWidth;
	result.twidth = textWidth + padding.left() + padding.right();
	result.height = padding.top() + font->height + padding.bottom();
	result.size = result.twidth + font->height * 2;
	const auto skip = int(std::ceil(result.twidth / M_SQRT2));
	result.textpos = QPoint(result.size - skip, padding.top());
	return result;
}

[[nodiscard]] QPoint RibbonWordPosition(
		const RibbonGeometry &ribbon,
		const QString &text) {
	const auto &font = st::msgServiceGiftBoxBadgeFont;
	const auto padding = st::chatUniqueGiftBadgePadding;
	return QPoint(
		padding.left() + (ribbon.textWidth - font->width(text)) / 2,
		padding.top() + font->ascent);
}

[[nodiscard]] QPointF RibbonWordCenter(
		const RibbonGeometry &ribbon,
		const QString &text) {
	const auto &font = st::msgServiceGiftBoxBadgeFont;
	const auto origin = QPointF(RibbonWordPosition(ribbon, text))
		+ font->metrics().tightBoundingRect(text).center();
	return QTransform()
		.translate(ribbon.textpos.x(), ribbon.textpos.y())
		.rotate(45.)
		.map(origin);
}

// The word alone, rotated and supersampled the way the gift badges are.
[[nodiscard]] QImage RenderRibbonWord(
		const RibbonGeometry &ribbon,
		const QString &text) {
	const auto &font = st::msgServiceGiftBoxBadgeFont;
	const auto ratio = style::DevicePixelRatio();
	const auto multiplier = ratio * 3;
	const auto size = QSize(ribbon.size, ribbon.size);
	auto image = QImage(size * multiplier, QImage::Format_ARGB32_Premultiplied);
	image.fill(Qt::transparent);
	image.setDevicePixelRatio(multiplier);
	{
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);
		p.translate(ribbon.textpos);
		p.rotate(45.);
		p.setFont(font);
		p.setPen(st::activeButtonFg);
		p.drawText(RibbonWordPosition(ribbon, text), text);
	}
	auto result = image.scaled(
		size * ratio,
		Qt::IgnoreAspectRatio,
		Qt::SmoothTransformation);
	result.setDevicePixelRatio(ratio);
	return result;
}

[[nodiscard]] QRect RibbonBandRect(const RibbonGeometry &ribbon) {
	return QRect(-5 * ribbon.twidth, 0, ribbon.twidth * 12, ribbon.height);
}

void PaintRibbonBand(
		QPainter &p,
		const RibbonGeometry &ribbon,
		const QColor &bg) {
	p.save();
	p.translate(ribbon.textpos);
	p.rotate(45.);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRect(RibbonBandRect(ribbon));
	p.restore();
}

[[nodiscard]] QImage RenderRibbon(
		const RibbonGeometry &ribbon,
		const QString &text,
		const QColor &bg) {
	const auto ratio = style::DevicePixelRatio();
	auto result = QImage(
		QSize(ribbon.size, ribbon.size) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	result.setDevicePixelRatio(ratio);
	result.fill(Qt::transparent);
	{
		auto p = QPainter(&result);
		auto hq = PainterHighQualityEnabler(p);
		PaintRibbonBand(p, ribbon, bg);
		p.drawImage(0, 0, RenderRibbonWord(ribbon, text));
	}
	return result;
}

[[nodiscard]] float64 RibbonReach(
		const RibbonGeometry &ribbon,
		QPointF center) {
	const auto band = QTransform()
		.translate(ribbon.textpos.x(), ribbon.textpos.y())
		.rotate(45.)
		.map(QPolygonF(QRectF(RibbonBandRect(ribbon))));
	const auto inside = band.intersected(
		QPolygonF(QRectF(0, 0, ribbon.size, ribbon.size)));
	auto result = 0.;
	for (const auto &point : inside) {
		accumulate_max(result, QLineF(center, point).length());
	}
	return result;
}

[[nodiscard]] float64 RevealProgress(
		crl::time elapsed,
		crl::time delay,
		crl::time duration) {
	return std::clamp((elapsed - delay) / float64(duration), 0., 1.);
}

[[nodiscard]] float64 BumpShape(const BumpCurve &curve, crl::time elapsed) {
	if (elapsed <= 0 || elapsed >= BumpDuration(curve)) {
		return 0.;
	} else if (elapsed < curve.rise) {
		return elapsed / float64(curve.rise);
	}
	const auto fall = elapsed - curve.rise - curve.hold;
	if (fall < 0) {
		return 1.;
	} else if (fall < curve.fall) {
		const auto progress = fall / float64(curve.fall);
		return std::pow(1. - progress, curve.fallEase);
	}
	const auto settle = (fall - curve.fall) / float64(curve.settle);
	return -curve.undershoot * std::sin(M_PI * settle);
}

// The service sentence sits msgServiceMargin.top() above the whole block.
[[nodiscard]] float64 BumpAmplitude(QSize outer, float64 amplitude) {
	return outer.isEmpty()
		? 0.
		: std::min(
			amplitude,
			2. * st::msgServiceMargin.top() / outer.height());
}

[[nodiscard]] std::vector<float64> ReadRollPositions(
		const Wallet::AmountParts &parts,
		crl::time elapsed) {
	auto steps = std::vector<int>();
	auto rolling = 0;
	for (const auto &text : { parts.whole, parts.fraction }) {
		for (const auto &ch : text) {
			if (!ch.isDigit()) {
				continue;
			}
			const auto digit = ch.digitValue();
			if (!digit && !rolling) {
				steps.push_back(0);
			} else {
				steps.push_back(kReadRollCycle + digit);
				++rolling;
			}
		}
	}
	auto result = std::vector<float64>();
	result.reserve(steps.size());
	auto index = 0;
	for (const auto count : steps) {
		if (!count) {
			result.push_back(0.);
			continue;
		}
		const auto early = (rolling - 1 - index++)
			/ float64(std::max(rolling - 1, 1));
		const auto lands = kReadRollDuration
			- (kReadRollDuration - kReadRollFirst) * early;
		const auto progress = std::clamp(elapsed / lands, 0., 1.);
		result.push_back(
			count * (1. - std::pow(1. - progress, kReadRollEase)));
	}
	return result;
}

[[nodiscard]] QString FriendlyAddress(const QString &address) {
	const auto parsed = Wallet::ParseAddress(address);
	return parsed
		? Wallet::FormatFriendly(
			parsed->raw,
			false,
			parsed->testnet)
		: QString();
}

[[nodiscard]] QString ReadableIdentity(
		not_null<HistoryItem*> item,
		bool hasAddress) {
	if (item->history()->peer->isNotificationsUser()) {
		return tr::lng_credits_box_history_entry_anonymous(tr::now);
	}
	const auto peer = item->out() ? item->history()->peer : item->from();
	const auto user = item->history()->owner().userLoaded(peerToUser(peer->id));
	if (user) {
		const auto name = TextUtilities::SingleLine(user->name());
		if (!name.isEmpty()) {
			return name;
		}
	}
	return hasAddress
		? (item->out()
			? tr::lng_wallet_details_recipient
			: tr::lng_wallet_details_sender)(tr::now)
		: (item->out()
			? tr::lng_wallet_row_outgoing
			: tr::lng_wallet_row_incoming)(tr::now);
}

[[nodiscard]] QStringList AddressLines(
		const QString &address,
		int available) {
	if (address.isEmpty()) {
		return {};
	}
	const auto font = st::walletDetailsCollectionLabel.style.font->monospace();
	auto groups = QStringList();
	for (auto i = 0; i < address.size(); i += kAddressGroupSize) {
		groups.push_back(address.mid(i, kAddressGroupSize));
	}
	const auto linesFor = [&](int perLine) {
		auto result = QStringList();
		for (auto i = 0; i < groups.size(); i += perLine) {
			result.push_back(groups.mid(i, perLine).join(QChar(' ')));
		}
		return result;
	};
	for (auto perLine = kAddressGroupsPerLine; perLine > 1; --perLine) {
		auto lines = linesFor(perLine);
		const auto fits = ranges::all_of(lines, [&](const QString &line) {
			return font->width(line) <= available;
		});
		if (fits) {
			return lines;
		}
	}
	return groups;
}

[[nodiscard]] int GramTransferCardWidth(int outerWidth) {
	return std::max(outerWidth - 2 * st::chatUniqueGiftBorder, 0);
}

}

}
