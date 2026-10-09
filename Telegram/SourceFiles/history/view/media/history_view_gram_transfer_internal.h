#pragma once

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "history/view/media/history_view_gram_transfer.h"

#include "chat_helpers/compose/compose_show.h"
#include "core/click_handler_types.h"
#include "data/data_histories.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "history/view/media/history_view_media_generic.h"
#include "history/view/history_view_cursor_state.h"
#include "history/view/history_view_element.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_item_components.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/main_account.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/controls/ton_common.h"
#include "ui/effects/star_burst.h"
#include "ui/text/text_utilities.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_amount_painter.h"
#include "wallet/wallet_card_angle.h"
#include "wallet/wallet_card_gradient.h"
#include "wallet/wallet_comment.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_panel.h"
#include "wallet/wallet_sending_effects.h"
#include "window/window_session_controller.h"

#include <QtCore/QLocale>

#include <limits>

#include "styles/style_chat.h"
#include "styles/style_polls.h"
#include "styles/style_wallet.h"

namespace HistoryView {

namespace GramTransferInternal {}

using namespace GramTransferInternal;

namespace GramTransferInternal {

struct BumpCurve {
	crl::time rise = 0;
	crl::time hold = 0;
	crl::time fall = 0;
	crl::time settle = 0;
	float64 fallEase = 1.;
	float64 undershoot = 0.;
};

[[nodiscard]] constexpr crl::time BumpDuration(const BumpCurve &curve) {
	return curve.rise + curve.hold + curve.fall + curve.settle;
}

constexpr auto kAddressGroupSize = 4;

constexpr auto kAddressGroupsPerLine = 6;

constexpr auto kGlareDuration = crl::time(1100);

constexpr auto kGlareTimeout = crl::time(400);

constexpr auto kRevealClockDuration = crl::time(43);

constexpr auto kRevealFillDuration = crl::time(160);

constexpr auto kRevealColorDelay = crl::time(20);

constexpr auto kRevealColorDuration = crl::time(200);

constexpr auto kRevealWordDelay = crl::time(33);

constexpr auto kRevealWordDuration = crl::time(140);

constexpr auto kRevealDuration = std::max({
	kRevealFillDuration,
	kRevealColorDelay + kRevealColorDuration,
	kRevealWordDelay + kRevealWordDuration,
});

constexpr auto kSettleBump = BumpCurve{
	.rise = crl::time(150),
	.hold = crl::time(33),
	.fall = crl::time(200),
	.settle = crl::time(200),
	.fallEase = 1.2,
	.undershoot = 0.043,
};

constexpr auto kBumpAmplitude = 0.07;

constexpr auto kReadRollFirst = crl::time(876);

constexpr auto kReadRollDuration = crl::time(1000);

constexpr auto kReadRollEase = 1.5;

constexpr auto kReadRollCycle = 10;

constexpr auto kReadSpacing = crl::time(1400);

constexpr auto kReadPopAmplitude = 0.03;

constexpr auto kReadPressDepth = 0.025;

constexpr auto kReadPress = BumpCurve{
	.rise = crl::time(33),
	.fall = crl::time(167),
	.settle = crl::time(300),
	.fallEase = 1.2,
	.undershoot = 0.64,
};

constexpr auto kSendingSpeed = 90.;

constexpr auto kSpinTurn = 360.;

constexpr auto kSpinDuration = crl::time(1700);

constexpr auto kSweepPeriod = 360.;

constexpr auto kBurstDelay = crl::time(90);

constexpr auto kBurstSpread = crl::time(250);

constexpr auto kBurstLifeMin = crl::time(850);

constexpr auto kBurstLifeMax = crl::time(1100);

constexpr auto kBurstDuration = kBurstDelay + kBurstSpread + kBurstLifeMax;

constexpr auto kBurstStarsPerSide = 48;

constexpr auto kBurstAppearTill = 0.2;

constexpr auto kBurstFadeAfter = 0.8;

constexpr auto kBurstDeformation = 0.1;

constexpr auto kTransitionDuration = std::max({
	kRevealDuration,
	BumpDuration(kSettleBump),
	BumpDuration(kReadPress),
	kGlareDuration - kReadRollDuration,
	kSpinDuration,
	kBurstDuration,
});

[[nodiscard]] QColor CardTickerFg();

[[nodiscard]] QColor CardAddressFg();

[[nodiscard]] QColor SentBadgeBg();

[[nodiscard]] QColor ReceivedBadgeBg();

[[nodiscard]] QWidget *PaintWidget(const QPainter &p);

struct GramTransferAction {
	FullMsgId itemId;
	int64 amount = 0;
	QString address;
	QString transactionId;
	QString comment;
	QString failReason;
	bool outgoing = false;
	bool encrypted = false;
	bool failed = false;

	friend bool operator==(
		const GramTransferAction &,
		const GramTransferAction &) = default;
};

struct GramTransferOrigin {
	base::weak_ptr<Main::Session> session;
	base::weak_ptr<Element> view;
	base::weak_ptr<MediaGeneric> media;
	GramTransferAction action;
};

struct GramTransferDetails {
	Wallet::TransferItem item;
	// True while the box has only what the message said, and the
	// transaction it names has not been served yet.
	bool partial = true;
};

struct TransferTag {
	QString text;
	QColor bg;
};

// The corner ribbon, laid out as ValidateRotatedBadge lays out a gift badge,
// except that the word gets a fixed area and is centered inside it.
struct RibbonGeometry {
	QPoint textpos;
	int textWidth = 0;
	int twidth = 0;
	int height = 0;
	int size = 0;
};

[[nodiscard]] Wallet::ClockStyle CardClockStyle();

[[nodiscard]] std::unique_ptr<Lottie::Icon> MakeCardMark(
		const QString &name);

struct SettleSpin {
	float64 from = 0.;
	float64 turn = 0.;
	float64 target = 0.;
};

struct ReadRoll {
	GramReadLine::Turn turn;
	std::unique_ptr<Lottie::Icon> fast;
	crl::time started = 0;
	float64 angle = 0.;
	bool settled = false;
};

// Lives from a read or the end of the sending look until its settle is over.
struct CardTransition {
	Ui::Animations::Basic animation;
	Wallet::GlareCycle glare;
	Wallet::ClockPose pose;
	SettleSpin spin;
	std::unique_ptr<Ui::StarBurst> burst;
	std::unique_ptr<Lottie::Icon> fast;
	std::optional<ReadRoll> read;
	QString toText;
	QImage toWord;
	QImage frame;
	Wallet::CardBackground background;
	crl::time started = 0;
	int wordsTextWidth = 0;
	bool fastStarted = false;
};

struct SendingClock {
	Ui::Animations::Basic animation;
	Wallet::GlareCycle glare;
	Wallet::CardBackground background;
	std::unique_ptr<Lottie::Icon> spare;
	std::unique_ptr<Lottie::Icon> fast;
	crl::time started = 0;
	crl::time loopStarted = 0;
	crl::time settleAt = 0;
	float64 angle = 0.;
	int loop = 0;
};

[[nodiscard]] bool PlayingFast(const CardTransition &transition);

[[nodiscard]] float64 SendingAngle(float64 from, crl::time elapsed);

[[nodiscard]] float64 ClockwiseRemainder(float64 degrees);

[[nodiscard]] SettleSpin StartSpin(float64 from, float64 target);

[[nodiscard]] float64 SpinAngle(
		const SettleSpin &spin,
		float64 target,
		crl::time elapsed);

[[nodiscard]] int64 LoopPosition(
		not_null<Lottie::Icon*> icon,
		crl::time started,
		crl::time now);

[[nodiscard]] crl::time NextLoopStart(
		not_null<Lottie::Icon*> icon,
		crl::time started,
		crl::time now);

[[nodiscard]] Ui::StarBurstDescriptor CardBurstDescriptor();

// What a card being replaced by a refreshed view passes to its successor.
struct GramTransferHandover {
	std::unique_ptr<Lottie::Icon> mark;
	std::unique_ptr<CardTransition> transition;
	std::unique_ptr<SendingClock> clock;
	base::weak_ptr<Wallet::CardAngle> angle;
	bool markStarted = false;
};

class GramTransferCardPart final
	: public MediaGenericPart
	, public base::has_weak_ptr {
public:
	GramTransferCardPart(
		GramTransferOrigin origin,
		GramTransferHandover handover);
	~GramTransferCardPart();

	[[nodiscard]] GramTransferHandover takeHandover();

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const override;

	[[nodiscard]] bool hasHeavyPart() override;
	void unloadHeavyPart() override;
	[[nodiscard]] Media::BubbleRoll bubbleRoll(QSize outer) const override;
	[[nodiscard]] QMargins bubbleRollRepaintMargins(
		QSize outer) const override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	struct Layout {
		QRect card;
		QString identity;
		QString badge;
		QColor badgeBg;
		int badgeTextWidth = 0;
		bool sending = false;
		QPointF clockCenter;
		QStringList addressLines;
		int markTop = 0;
		int amountTop = 0;
		int identityTop = 0;
		int addressTop = 0;
	};

	struct RibbonKey {
		QString text;
		QColor bg;
		int textWidth = 0;
		int ratio = 0;

		friend bool operator==(const RibbonKey &, const RibbonKey &) = default;
	};

	[[nodiscard]] int resolveLayout(int outerWidth);
	[[nodiscard]] bool sending() const;
	[[nodiscard]] bool looping() const;
	[[nodiscard]] bool waitingForLoop(crl::time now) const;
	[[nodiscard]] bool sendingLook(crl::time now) const;
	[[nodiscard]] std::optional<Wallet::GlareBand> glarePass(
		crl::time now) const;
	struct Sweep {
		float64 angle = 0.;
		Wallet::CardBackground *own = nullptr;
	};
	[[nodiscard]] Sweep sweep(
		crl::time now,
		crl::time frame,
		bool still) const;
	void paintBurst(QPainter &p, crl::time now) const;
	[[nodiscard]] bool transitionFinished(crl::time now) const;
	void adopt(GramTransferHandover &&handover);
	void watchRead();
	void markRead(bool shown);
	void validateRead(crl::time now, crl::time frame, bool paused) const;
	[[nodiscard]] bool holding() const;
	[[nodiscard]] bool rolling(crl::time now) const;
	[[nodiscard]] bool awaitingRead() const;
	[[nodiscard]] bool playingRead(crl::time now) const;
	void animateTransition() const;
	void startReveal(const SendingClock &clock, crl::time now) const;
	void attachClock() const;
	void validateReveal(crl::time now) const;
	void validateMark() const;
	[[nodiscard]] QRect markPaintRect() const;
	void validateClock(crl::time now) const;
	void validateLoop(crl::time now, bool paused) const;
	void advanceLoop(crl::time now) const;
	void advanceRead(crl::time now) const;
	void startFastMark() const;
	void validateBadge() const;
	void validateAngle(
		QPainter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context) const;
	void paintGlareBorder(QPainter &p, Wallet::GlareBand band) const;
	void paintSendingClock(QPainter &p, crl::time now) const;
	void paintReveal(QPainter &p, int cardWidth, crl::time now) const;
	void showDetails(const ClickContext &context);

	const GramTransferOrigin _origin;
	const ClickHandlerPtr _detailsLink;
	Wallet::AmountPainter _amount;
	const QString _address;
	const QString _identity;
	// Rows keep clear of the widest ribbon an outgoing card can settle to.
	const int _ribbonTextWidth = 0;
	Layout _layout;
	mutable std::unique_ptr<Lottie::Icon> _mark;
	mutable std::unique_ptr<SendingClock> _clock;
	mutable std::unique_ptr<CardTransition> _transition;
	mutable base::weak_ptr<Wallet::CardAngle> _angle;
	mutable bool _markStarted = false;
	mutable bool _heavyPending = false;
	mutable QImage _badge;
	mutable RibbonKey _badgeKey;
	rpl::event_stream<> _destroyed;
	rpl::lifetime _readLifetime;

};

class GramTransferCommentPart final
	: public MediaGenericPart
	, public base::has_weak_ptr {
public:
	GramTransferCommentPart(
		GramTransferOrigin origin,
		Wallet::TransferItem item,
		QString display);
	~GramTransferCommentPart();

	void draw(
		Painter &p,
		not_null<const MediaGeneric*> owner,
		const PaintContext &context,
		int outerWidth) const override;
	TextState textState(
		QPoint point,
		StateRequest request,
		int outerWidth) const override;

	void hideSpoilers() override;

	[[nodiscard]] uint16 fullSelectionLength() const override;
	[[nodiscard]] TextSelection adjustSelection(
		TextSelection selection,
		TextSelectType type) const override;
	[[nodiscard]] TextForMimeData selectedText(
		TextSelection selection) const override;

	QSize countOptimalSize() override;
	QSize countCurrentSize(int newWidth) override;

private:
	[[nodiscard]] int resolveLayout(int outerWidth);
	[[nodiscard]] bool covered() const;
	void createComment(Wallet::TransferItem item);
	void updateText();
	void invalidate();
	void activate(const ClickContext &context);

	const GramTransferOrigin _origin;
	const TextWithEntities _cover;
	std::unique_ptr<Wallet::TransferComment> _comment;
	std::optional<Wallet::TransferWalletIdentity> _commentIdentity;
	Ui::Text::String _text;
	QRect _textRect;
	bool _revealed = false;
	bool _retired = false;
	rpl::lifetime _commentLifetime;
	rpl::lifetime _lifetime;

};

[[nodiscard]] GramTransferAction SnapshotGramTransfer(
		not_null<HistoryItem*> item);

[[nodiscard]] HistoryItem *CurrentGramTransfer(
		const GramTransferOrigin &origin);

[[nodiscard]] rpl::producer<> GramTransferInvalidations(
		const GramTransferOrigin &origin);

[[nodiscard]] auto GramTransferShow(
		const GramTransferOrigin &origin,
		const ClickContext &context)
-> std::shared_ptr<Main::SessionShow>;

[[nodiscard]] GramTransferDetails ResolveGramTransfer(
		not_null<Main::Session*> session,
		const GramTransferAction &action);

[[nodiscard]] Wallet::AmountStyle CardAmountStyle();

[[nodiscard]] Wallet::AmountParts SignedAmount(int64 value, bool outgoing);

[[nodiscard]] TransferTag ResolveTag(bool outgoing, bool failed);

[[nodiscard]] int OutgoingRibbonTextWidth();

[[nodiscard]] RibbonGeometry ComputeRibbon(int textWidth);

[[nodiscard]] QPoint RibbonWordPosition(
		const RibbonGeometry &ribbon,
		const QString &text);

[[nodiscard]] QPointF RibbonWordCenter(
		const RibbonGeometry &ribbon,
		const QString &text);

[[nodiscard]] QImage RenderRibbonWord(
		const RibbonGeometry &ribbon,
		const QString &text);

[[nodiscard]] QRect RibbonBandRect(const RibbonGeometry &ribbon);

void PaintRibbonBand(
		QPainter &p,
		const RibbonGeometry &ribbon,
		const QColor &bg);

[[nodiscard]] QImage RenderRibbon(
		const RibbonGeometry &ribbon,
		const QString &text,
		const QColor &bg);

[[nodiscard]] float64 RibbonReach(
		const RibbonGeometry &ribbon,
		QPointF center);

[[nodiscard]] float64 RevealProgress(
		crl::time elapsed,
		crl::time delay,
		crl::time duration);

[[nodiscard]] float64 BumpShape(const BumpCurve &curve, crl::time elapsed);

[[nodiscard]] float64 BumpAmplitude(QSize outer, float64 amplitude);

[[nodiscard]] std::vector<float64> ReadRollPositions(
		const Wallet::AmountParts &parts,
		crl::time elapsed);

[[nodiscard]] QString FriendlyAddress(const QString &address);

[[nodiscard]] QString ReadableIdentity(
		not_null<HistoryItem*> item,
		bool hasAddress);

[[nodiscard]] QStringList AddressLines(
		const QString &address,
		int available);

[[nodiscard]] int GramTransferCardWidth(int outerWidth);

}

std::unique_ptr<Media> CreateGramTransferMedia(
		not_null<Element*> parent,
		Element *replacing);

}
