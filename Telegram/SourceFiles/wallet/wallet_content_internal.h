#pragma once

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_content.h"

#include "api/api_cloud_password.h"
#include "api/api_common.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/debug_log.h"
#include "base/event_filter.h"
#include "base/invoke_queued.h"
#include "base/qthelp_regex.h"
#include "base/qthelp_url.h"
#include "base/random.h"
#include "base/timer.h"
#include "base/unique_qptr.h"
#include "base/unixtime.h"
#include "boxes/passcode_box.h"
#include "boxes/peer_list_box.h"
#include "boxes/peer_list_controllers.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/credits_amount.h"
#include "core/local_url_handlers.h"
#include "core/ton_explorer_url.h"
#include "core/ui_integration.h"
#include "data/components/credits.h"
#include "data/components/promo_suggestions.h"
#include "data/components/recent_money_recipients.h"
#include "data/components/recent_peers.h"
#include "data/components/top_peers.h"
#include "data/data_changes.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "dialogs/ui/dialogs_pill.h"
#include "history/history.h"
#include "info/channel_statistics/boosts/giveaway/boost_badge.h" // InfiniteRadialAnimationWidget.
#include "info/channel_statistics/earn/earn_icons.h"
#include "info/profile/info_profile_values.h"
#include "inline_bots/bot_attach_web_view.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_keys.h"
#include "lottie/lottie_icon.h"
#include "main/session/session_show.h"
#include "main/main_app_config.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "qr/qr_generate.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/sections/settings_credits.h"
#include "settings/settings_common.h"
#include "storage/storage_account.h"
#include "storage/storage_domain.h"
#include "ui/boxes/confirm_box.h"
#include "ui/controls/feature_list.h"
#include "ui/controls/table_rows.h"
#include "ui/controls/ton_common.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/star_burst.h"
#include "ui/effects/unique_gift_message_bubble.h"
#include "ui/image/image_prepare.h"
#include "ui/layers/generic_box.h"
#include "ui/text/custom_emoji_helper.h"
#include "ui/text/format_values.h"
#include "ui/text/text_custom_emoji.h"
#include "ui/text/text_options.h"
#include "ui/text/text_utilities.h"
#include "ui/toast/toast.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/discrete_sliders.h"
#include "ui/widgets/glare_tooltip.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/multi_select.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/separate_panel.h"
#include "ui/widgets/shadow.h"
#include "ui/wrap/fade_wrap.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/table_layout.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/basic_click_handlers.h"
#include "ui/empty_userpic.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/round_rect.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_amount_field.h"
#include "wallet/wallet_amount_painter.h"
#include "wallet/wallet_card_angle.h"
#include "wallet/wallet_card_gradient.h"
#include "wallet/wallet_chat_show.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_collectibles.h"
#include "wallet/wallet_comment.h"
#include "wallet/wallet_custody.h"
#include "wallet/wallet_diamond_flight.h"
#include "wallet/wallet_fiat.h"
#include "wallet/wallet_key_protection.h"
#include "wallet/wallet_onramp.h"
#include "wallet/wallet_palette.h"
#include "wallet/wallet_rates.h"
#include "wallet/wallet_sending_effects.h"
#include "wallet/wallet_session.h"
#include "wallet/wallet_ton_connect.h"
#include "wallet/wallet_ton_connect_box.h"
#include "wallet/wallet_unlock.h"
#include "wallet/wallet_user_addresses.h"
#include "window/themes/window_theme.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"

#include <QtCore/QLocale>
#include <QtCore/QUrl>
#include <QtCore/QtMath>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QPainterPath>
#include <QtSvg/QSvgRenderer>
#include <QtWidgets/QTextEdit>

#include <array>

#include "styles/style_boxes.h"
#include "styles/style_chat.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_wallet.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace Wallet {

namespace ContentDetails {

constexpr auto kAddressLength = 48;

constexpr auto kAddressGroup = 4;

constexpr auto kAddressGroupsPerLine = 6;

constexpr auto kReceiveGroupsPerLine = 4;

constexpr auto kReceiveLines = kAddressLength
	/ kAddressGroup
	/ kReceiveGroupsPerLine;

constexpr auto kSendUserCardGroupsPerLine = 6;

constexpr auto kQrQuietZoneModules = 4;

constexpr auto kShortAddressChars = 4;

constexpr auto kGaslessDailyTransfersDefault = 5;

constexpr auto kMinus = QChar(0x2212);

constexpr auto kImportWordCountShort = 12;

constexpr auto kImportWordCountLong = 24;

constexpr auto kImportSuggestionsLimit = 3;

constexpr auto kBackupWriteDownDelay = 30 * crl::time(1000);

constexpr auto kBackupQuizWordCount = 3;

constexpr auto kCoverBodyPart = 0.90;

constexpr auto kCoverTitleScale = 0.05;

constexpr auto kCardFoldMinHeight = 1.;

constexpr auto kIntroTooltipShownPref = "wallet_intro_tooltip_shown"_cs;

constexpr auto kWalletIntroGlares = 2;

constexpr auto kUndatedRowDate = std::numeric_limits<TimeId>::max();

constexpr auto kTransactionLookupInterval = crl::time(1000);

constexpr auto kTransactionLookupAttempts = 10;

constexpr auto kMaxFiatUnits = 999'999'999LL;

constexpr auto kMaxAmountNano = 999'999'999'999'999'999LL;

constexpr auto kSendUserLoadTimeout = 30 * crl::time(1000);

constexpr auto kRecipientSearchLimit = 64;

constexpr auto kNameBusyRetryDelay = crl::time(500);

constexpr auto kSendOwnerLookupDelay = crl::time(500);

constexpr auto kSendRefusalRetries = 3;

constexpr auto kCommentPasswordStateTimeout = 30 * crl::time(1000);

constexpr auto kSigningReadyTimeout = 30 * crl::time(1000);

constexpr auto kHeldSendKeyTimeout = 60 * crl::time(1000);

constexpr auto kCustodyResolveTimeout = 20 * crl::time(1000);

constexpr auto kGramDigits = 9;

constexpr auto kSendingRowGlareDuration = crl::time(1450);

constexpr auto kSendingRowGlarePause = crl::time(1040);

constexpr auto kSendingRowGlareLag = 0.4;

constexpr auto kSendingRowGlareBorder = 0.5;

constexpr auto kSendingRowGlareBackground = 0.12;

constexpr auto kSendingRowEntranceDuration = crl::time(370);

constexpr auto kSendingRowEntrancePeak = crl::time(180);

constexpr auto kSendingRowEntranceDamping = 0.57;

constexpr auto kSendingRowEntranceScale = 0.93;

constexpr auto kSendingRowFlightDuration = crl::time(480);

constexpr auto kSendingRowBumpPress = crl::time(115);

constexpr auto kSendingRowBumpRelease = crl::time(220);

constexpr auto kSendingRowBumpRebound = 0.35;

constexpr auto kSendingRowBumpTaper = crl::time(460);

constexpr auto kSendingRowBumpDuration = crl::time(560);

constexpr auto kSendingRowBumpDrop = 0.04;

constexpr auto kSendingRowBumpShrink = 0.012;

constexpr auto kSendingRowBumpSwell = 0.15;

constexpr auto kSendingRowSettlePill = crl::time(110);

constexpr auto kSendingRowSettleBadgeFrom = crl::time(65);

constexpr auto kSendingRowSettleBadgeTill = crl::time(230);

constexpr auto kSendingRowSettleLabelFrom = crl::time(35);

constexpr auto kSendingRowSettleLabelTill = crl::time(230);

constexpr auto kSendingRowSettleAmount = crl::time(170);

constexpr auto kSendingRowSettleAmountFadeFrom = crl::time(110);

constexpr auto kSendingRowSettleDiamondMove = crl::time(130);

constexpr auto kSendingRowSettleDiamondSwellTill = crl::time(100);

constexpr auto kSendingRowSettleDiamondSwell = 1.1;

constexpr auto kSendingRowSettleDiamond = crl::time(370);

constexpr auto kSendingRowSettleDiamondFadeFrom = crl::time(230);

constexpr auto kSendingRowSettlePaintWait = crl::time(1000);

constexpr auto kSendingRowBurstDelay = crl::time(50);

constexpr auto kSendingRowBurstSpread = crl::time(160);

constexpr auto kSendingRowBurstLifeMin = crl::time(450);

constexpr auto kSendingRowBurstLifeMax = crl::time(650);

constexpr auto kRowEmojiDiamondLeft = 7. / 72.;

constexpr auto kRowEmojiDiamondTop = 12. / 72.;

constexpr auto kRowEmojiDiamondRight = 65. / 72.;

constexpr auto kRowEmojiDiamondBottom = 62. / 72.;

class BalanceInk;

class Card;

struct CardFold;

class InfoIsland;

class InfoIslandEntry;

class SendingHistoryRow;

struct SendingRow;

class KeyContext final
	: public Main::SessionShow
	, public std::enable_shared_from_this<KeyContext> {
public:
	KeyContext(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CommentScope> scope,
		Fn<bool()> current,
		Fn<void(KeyAuthorization)> done);

	void showOrHideBoxOrLayer(
		std::variant<
			v::null_t,
			object_ptr<Ui::BoxContent>,
			std::unique_ptr<Ui::LayerWidget>> &&layer,
		Ui::LayerOptions options,
		anim::type animated) const override;
	not_null<QWidget*> toastParent() const override;
	bool valid() const override;
	operator bool() const override;
	Main::Session &session() const override;

	[[nodiscard]] std::shared_ptr<CommentScope> scope() const;
	// The show this context wraps, for a box that must outlive a prompt
	// closing under it instead of being read as the end of the attempt.
	[[nodiscard]] std::shared_ptr<Main::SessionShow> plain() const;
	[[nodiscard]] CustodyInstaller installer();
	void acceptClosed();
	void allowPromptRetry(base::weak_qptr<Ui::BoxContent> box);
	void cancelOnClose(
		base::weak_qptr<Ui::BoxContent> box,
		bool allowSuccessor = false);
	void closePrompt(base::weak_qptr<Ui::BoxContent> box);
	void ready(KeyAuthorization auth);
	void cancel();
	[[nodiscard]] rpl::lifetime &lifetime();

private:
	struct Prompt {
		base::weak_qptr<Ui::BoxContent> box;
		bool closing = false;
		bool accepted = false;
		bool cancelOnClose = false;
		bool allowSuccessor = true;
	};

	void promptClosed(const std::shared_ptr<Prompt> &prompt);
	void finish(KeyAuthorization auth);

	const std::shared_ptr<Main::SessionShow> _show;
	const base::weak_ptr<Main::Session> _session;
	const std::shared_ptr<CommentScope> _scope;
	const Fn<bool()> _current;
	Fn<void(KeyAuthorization)> _done;
	mutable std::vector<std::shared_ptr<Prompt>> _prompts;
	bool _finished = false;
	bool _installing = false;
	rpl::lifetime _lifetime;

};

class EncryptedCommentLabel final : public Ui::FlatLabel {
public:
	EncryptedCommentLabel(
		QWidget *parent,
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		TransferItem item,
		Fn<bool()> originCurrent);

	QString accessibilityName() override;

private:
	const TextWithEntities _cover;
	const bool _revealable = false;
	bool _closed = false;
	bool _revealed = false;
	TransferComment _comment;

};

class Content final : public Ui::RpWidget {
public:
	Content(
		not_null<Ui::SeparatePanel*> panel,
		std::shared_ptr<Main::SessionShow> show);
	~Content();

	void flySendDiamond(
		const std::string &operationId,
		not_null<Ui::TonAmountInput*> amount);

protected:
	void focusInEvent(QFocusEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	struct Regions {
		int reserve = 0;
		int scrollTop = 0;
		int scrollHeight = 0;
	};

	void setupContent();
	void setupPinned();
	void setupInfoIsland();
	void setupWaltEntry(not_null<InfoIsland*> island);
	void setupEarningsEntry(not_null<InfoIsland*> island);
	void setupOldWalletEntry(not_null<InfoIsland*> island);
	void setupProtectRow();
	void setupBalance();
	void setupTabs(rpl::producer<bool> collectiblesShown);
	void setupStrip();
	void setupListsLoading();
	void setupCustodyEntry(not_null<InfoIsland*> island);
	void paintTitle(QPainter &p, float64 fold);
	void updateRegions();
	void updatePinned();
	void updateVisibleArea();
	void checkLoadMore();
	bool revealSendingRow();
	[[nodiscard]] int pinnedMax() const;
	[[nodiscard]] int pinnedMin() const;
	[[nodiscard]] Regions countRegions(int columnHeight) const;
	[[nodiscard]] QRect cardRest() const;
	[[nodiscard]] float64 foldProgress() const;
	[[nodiscard]] const CardFold &cardFold() const;
	[[nodiscard]] QRegion cardOutline() const;

	const std::shared_ptr<Main::SessionShow> _show;
	Ui::SeparatePanel *_panel = nullptr;
	object_ptr<Ui::ScrollArea> _scroll;
	SingleQueuedInvokation _loadMoreCheck;
	std::unique_ptr<BalanceInk> _ink;
	Ui::Text::String _title;
	base::unique_qptr<Ui::RpWidget> _titleBalance;
	Ui::RpWidget *_container = nullptr;
	Ui::PaddingWrap<Ui::VerticalLayout> *_column = nullptr;
	Ui::RpWidget *_pinnedBackground = nullptr;
	Ui::RpWidget *_pinned = nullptr;
	Ui::VerticalLayout *_pinnedInner = nullptr;
	Ui::RpWidget *_pinnedBalance = nullptr;
	Ui::PlainShadow *_headerShadow = nullptr;
	Ui::SlideWrap<Ui::SettingsSlider> *_tabsWrap = nullptr;
	Ui::PlainShadow *_tabsShadow = nullptr;
	Ui::PlainShadow *_stripShadow = nullptr;
	Ui::RpWidget *_strip = nullptr;
	Ui::RpWidget *_listsLoading = nullptr;
	Ui::FlatLabel *_custodyBarLabel = nullptr;
	Ui::FixedHeightWidget *_cardPlaceholder = nullptr;
	Card *_card = nullptr;
	Ui::AbstractButton *_cardButton = nullptr;
	std::unique_ptr<SendingRow> _sendingRow;
	std::unique_ptr<DiamondFlight> _diamondFlight;
	QRect _paintedInk;
	int _reserve = 0;
	int _paintedHeight = -1;
	int _paintedMin = -1;
	bool _tabsShown = false;
	bool _stripShown = false;

};

struct CardFold {
	QRect rest;
	QPolygonF quad;
	QTransform transform;
	float64 topY = 0.;
	float64 bottomY = 0.;
	float64 opacity = 1.;
	float64 fold = 0.;
	bool valid = false;
};

// CardFold is expressed entirely in Content coordinates: `rest` is the
// card's rest rect there, `quad` the folded quadrilateral it is painted
// as, and `transform` maps the first onto the second. A Card paints with
// a widget-local painter, so it reconciles with p.translate(-x(), -y())
// before applying `transform` and draws into `rest`; paintedQuad() and
// paintedOutline() return Content coordinates for the same reason.
[[nodiscard]] CardFold ComputeCardFold(QRect cardRest, float64 fold);

class Card final : public Ui::RpWidget {
public:
	Card(
		QWidget *parent,
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<TextWithEntities> name);

	void setFold(const CardFold &fold);
	[[nodiscard]] const CardFold &fold() const;
	[[nodiscard]] QPolygonF paintedQuad() const;
	[[nodiscard]] QPolygonF paintedOutline() const;
	void invalidateCache();
	void followCursor();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] QRect restRect() const;
	[[nodiscard]] QRectF paintedRect() const;
	[[nodiscard]] float64 paintAngle();
	void paintContent(Painter &p, float64 angle);
	void validateCache(float64 angle);
	void refreshAddress();

	const std::shared_ptr<Main::SessionShow> _show;
	style::TextStyle _nameStyle;
	Ui::Text::String _name;
	QString _addressLine1;
	QString _addressLine2;
	CardFold _fold;
	CardBackground _background;
	QImage _cache;
	float64 _cacheAngle = 0.;
	std::unique_ptr<CardAngle> _angle;

};

struct BalancePalette {
	QColor mark;
	QColor amount;
	QColor secondary;
};

enum class BalanceStyle : uchar {
	Balance,
	Minus,
	Plus,
	Exact,
};

class BalanceInk final {
public:
	BalanceInk();

	void setContent(
		CreditsAmount amount,
		const QString &fiat,
		BalanceStyle style = BalanceStyle::Balance);
	void setOuterWidth(int outerWidth);
	void playMark(Fn<void()> repaint);
	void refresh();

	void paint(
		QPainter &p,
		const CardFold &fold,
		const QRegion &cardOutline,
		QRect clip) const;

	[[nodiscard]] QRect boundingRect(const CardFold &fold) const;
	[[nodiscard]] QRect markRect(QRect cardRest) const;
	[[nodiscard]] QRect markPaintRect(const CardFold &fold) const;

private:
	[[nodiscard]] QRectF amountRect(const CardFold &fold) const;
	[[nodiscard]] QRectF fiatRect(const CardFold &fold) const;
	[[nodiscard]] QRectF markVisible(float64 fold) const;
	[[nodiscard]] QRectF markDrawRect(
		float64 fold,
		const QRectF &box,
		const QRectF &visible) const;
	void paintMark(
		QPainter &p,
		float64 fold,
		const QImage &mono,
		bool card) const;
	void paintPass(
		QPainter &p,
		const CardFold &fold,
		const BalancePalette &palette,
		const QImage &mark,
		bool card,
		float64 secondaryOpacity) const;
	[[nodiscard]] QTransform groupTransform(const CardFold &fold) const;

	std::unique_ptr<Lottie::Icon> _markLottie;
	Fn<void()> _markRepaint;
	Wallet::AmountPainter _painter;
	QPainterPath _fiat;
	QImage _markCard;
	QImage _markSettled;
	QRectF _markFrame;
	QRectF _markLottieVisible;
	QRectF _markMonoVisible;
	CreditsAmount _balance;
	QString _fiatText;
	float64 _markTop = 0.;
	float64 _fiatWidth = 0.;
	int _outerWidth = 0;
	BalanceStyle _style = BalanceStyle::Balance;

};

class InfoIslandEntry final : public Ui::SettingsButton {
public:
	InfoIslandEntry(
		QWidget *parent,
		rpl::producer<QString> text,
		const style::SettingsButton &st);
	InfoIslandEntry(
		QWidget *parent,
		std::nullptr_t,
		const style::SettingsButton &st);

	void setRounding(RectParts corners, int radius);
	void setMinimalHeight(int height);

protected:
	int resizeGetHeight(int newWidth) override;
	QImage prepareRippleMask() const override;

private:
	RectParts _corners;
	int _radius = 0;
	int _minimalHeight = 0;

};

class InfoIsland final : public Ui::VerticalLayout {
public:
	explicit InfoIsland(QWidget *parent);

	not_null<Ui::SlideWrap<InfoIslandEntry>*> add(
		object_ptr<InfoIslandEntry> entry);
	[[nodiscard]] rpl::producer<bool> anyShownValue() const;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	void refreshRounding();
	void paintPill(QPainter &p);
	[[nodiscard]] QRect pillRect() const;

	std::vector<Ui::SlideWrap<InfoIslandEntry>*> _entries;
	Ui::MultiSlideTracker _tracker;
	Ui::BoxShadow _shadow;
	QMargins _extend;

};

[[nodiscard]] int PillRadius(QRect pill);

[[nodiscard]] int PanelCardWidth();

[[nodiscard]] QRect CardQrRect(int cardWidth);

[[nodiscard]] QRect TransferCardInfoRect(int cardWidth);

[[nodiscard]] QColor CardQrIconFg();

[[nodiscard]] QString GroupedAddressLine(
		const QString &address,
		int offset);

[[nodiscard]] QStringList TransferCardLines(
		const QString &destination,
		int recipients);

[[nodiscard]] std::optional<QString> DetailsFriendlyAddress(
		const TransferItem &item);

[[nodiscard]] TextWithEntities DetailsAddressValue(
		const QString &address);

[[nodiscard]] Fn<void()> CopyAddressCallback(
		std::shared_ptr<Ui::Show> show,
		const QString &address);

} // namespace ContentDetails

Fn<void()> CopyTextCallback(
		std::shared_ptr<Ui::Show> show,
		QString text,
		QString toast);

object_ptr<Ui::FlatLabel> AddressValueLabel(
		not_null<QWidget*> parent,
		std::shared_ptr<Ui::Show> show,
		const QString &address);

namespace ContentDetails {

[[nodiscard]] object_ptr<Ui::FlatLabel> NameValueLabel(
		not_null<Ui::RpWidget*> parent,
		std::shared_ptr<Ui::Show> show,
		const QString &name,
		const QString &address);

[[nodiscard]] QString OnrampProvider(const TransferItem &item);

enum class RowAvatar {
	Peer,
	In,
	Out,
	KeyChange,
	Gear,
};

struct HistoryRowContent {
	QString title;
	QString subtitle;
	QString date;
	int64 amountNano = 0;
	bool incoming = false;
	bool pending = false;
	bool failed = false;
	RowAvatar avatar = RowAvatar::Out;
	PeerData *peer = nullptr;
	bool itemAmount = false;
	QString collectible;

	friend bool operator==(
		const HistoryRowContent &,
		const HistoryRowContent &) = default;
};

struct SendingRow {
	std::string operationId;
	Ui::VerticalLayout *slot = nullptr;
	TransferItem item;
	HistoryRowContent content;
	SendingHistoryRow *look = nullptr;
	// Owed by the hand-over or the creation's postponed call, whichever runs first
	bool revealPending = false;
};

[[nodiscard]] QString ShortAddressForm(
		const QString &full,
		int chars = kShortAddressChars);

[[nodiscard]] QString ShortAddress(const QString &address);

[[nodiscard]] QString CounterpartyAddress(const TransferItem &item);

void SetAmountColor(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		const style::color &color);

[[nodiscard]] QString GramMajorPart(int64 amountNano);

[[nodiscard]] QString GramMinorPart(int64 amountNano);

struct RowAmountText {
	QString major;
	TextWithEntities minor;
	Ui::Text::MarkedContext context;
};

[[nodiscard]] QString RowAmountSign(bool incoming);

[[nodiscard]] QString RowAmountWhole(int64 amountNano, const QString &sign);

[[nodiscard]] RowAmountText PrepareRowAmountText(
		int64 amountNano,
		const QString &sign);

[[nodiscard]] RowAmountText PrepareRowItemAmountText(bool incoming);

[[nodiscard]] const style::color &RowAmountColor(
		bool incoming,
		bool pending,
		bool failed);

void SetRowAmountText(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		int64 amountNano,
		const QString &sign);

void SetRowAmount(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		int64 amountNano,
		bool incoming,
		bool pending,
		bool failed);

void SetRowItemAmount(
		not_null<Ui::FlatLabel*> major,
		not_null<Ui::FlatLabel*> minor,
		bool incoming,
		bool failed);

void PaintRowAvatar(Painter &p, QRect rect, RowAvatar avatar);

[[nodiscard]] int LabelLineHeight(const style::FlatLabel &st);

struct HistoryRowHeights {
	int title = 0;
	std::optional<int> subtitle;
	int date = 0;
	int amount = 0;
};

struct HistoryRowLine {
	int top = 0;
	int lines = 0;
};

struct HistoryRowLayout {
	HistoryRowLine title;
	HistoryRowLine subtitle;
	HistoryRowLine date;
	int height = 0;
	int avatarCenter = 0;
	int amountTop = 0;
	int titleWidth = 0;
	int textWidth = 0;
	int chipTop = 0;
};

[[nodiscard]] int HistoryRowTitleSkip(int amountWidth);

[[nodiscard]] HistoryRowLayout ComputeHistoryRowLayout(
		const HistoryRowHeights &heights);

struct HistoryRowText {
	Ui::Text::String title;
	Ui::Text::String subtitle;
	Ui::Text::String date;
	Ui::Text::String major;
	Ui::Text::String minor;
	bool subtitleShown = false;
	bool chip = false;
};

[[nodiscard]] Ui::Text::String HistoryRowLabelText(
		const style::FlatLabel &st,
		const QString &text);

[[nodiscard]] int HistoryRowLabelHeight(
		const style::FlatLabel &st,
		const Ui::Text::String &text,
		int width,
		bool breakEverywhere);

[[nodiscard]] HistoryRowText PrepareHistoryRowText(
		const HistoryRowContent &content,
		Fn<void()> repaint = nullptr);

[[nodiscard]] HistoryRowLayout MeasureHistoryRow(
		const HistoryRowText &text,
		int width);

struct RowAmountPlacement {
	QPoint major;
	QPoint minor;
};

[[nodiscard]] RowAmountPlacement PlaceRowAmount(
		const HistoryRowText &text,
		int amountTop,
		int width);

struct HistoryRowChipState {
	Gram::NftKind kind = Gram::NftKind::Generic;
	Ui::Text::String title;
	Ui::Text::String subtitle;
	int natural = 0;
};

class HistoryRowButton final : public Ui::SettingsButton {
public:
	using Ui::SettingsButton::SettingsButton;

	void setPaintUnderRipple(Fn<void(Painter&)> paint);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	Fn<void(Painter&)> _paintUnderRipple;

};

void PaintHistoryRowChipPlate(
		Painter &p,
		int outerWidth,
		const HistoryRowChipState &state);

void PaintHistoryRowChipArtwork(
		Painter &p,
		int outerWidth,
		const HistoryRowChipState &state,
		const std::shared_ptr<CollectibleMedia> &media,
		const QString &address);

void PaintHistoryRowChipText(
		Painter &p,
		int outerWidth,
		const HistoryRowChipState &state);

void TrackHistoryRowChip(
		not_null<HistoryRowChipState*> state,
		std::shared_ptr<CollectibleMedia> media,
		QString address,
		Fn<void()> repaint,
		rpl::lifetime &lifetime);

void AddHistoryRowChip(
		not_null<Ui::VerticalLayout*> inner,
		not_null<HistoryRowButton*> button,
		std::shared_ptr<CollectibleMedia> media,
		QString address);

not_null<Ui::RpWidget*> AddHistoryRow(
		not_null<Ui::VerticalLayout*> list,
		const HistoryRowContent &content,
		Fn<void()> clicked,
		std::shared_ptr<CollectibleMedia> media = nullptr);

struct SendingRowLayout {
	QRect pill;
	int radius = 0;
	QRect avatar;
	QPointF badge;
	QPoint diamond;
	QPointF amount;
	int titleWidth = 0;
	int textWidth = 0;
};

struct SendingRowSettleTarget {
	QPoint major;
	QPoint minor;
	int boundary = 0;
	QPointF anchor;
	QRectF diamond;
	int titleWidth = 0;
	int textWidth = 0;
};

struct SendingRowSettleProgress {
	crl::time elapsed = 0;
	float64 pill = 0.;
	float64 badge = 1.;
	float64 label = 0.;
	float64 amount = 0.;
	float64 amountFade = 0.;
	float64 emoji = 0.;
};

struct SendingRowSettleRequest {
	HistoryRowContent content;
	Fn<void()> done;
	bool burst = false;
};

class SendingHistoryRow final : public Ui::AbstractButton {
public:
	SendingHistoryRow(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> layer,
		not_null<Ui::RpWidget*> bounds,
		HistoryRowContent content,
		std::shared_ptr<CollectibleMedia> media);

	void setContent(HistoryRowContent content);
	void awaitDiamond();
	void landDiamond(std::unique_ptr<Lottie::Icon> icon, crl::time loopStarted);
	void cancelDiamondAwait();
	void scheduleBump(crl::time at);
	void settle(HistoryRowContent content, bool burst, Fn<void()> done);

	[[nodiscard]] bool surfaceShown() const;
	[[nodiscard]] bool inView() const;
	[[nodiscard]] bool settling() const;
	[[nodiscard]] bool settled() const;
	[[nodiscard]] QRectF diamondTarget(crl::time now) const;

	QString accessibilityName() override;

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(int visibleTop, int visibleBottom) override;

private:
	struct Settle;

	[[nodiscard]] const HistoryRowLayout &rowLayout() const;
	void updateRowLayout();
	[[nodiscard]] SendingRowLayout layout() const;
	[[nodiscard]] SendingRowSettleTarget settleTarget() const;
	[[nodiscard]] crl::time settleDuration() const;
	[[nodiscard]] crl::time settleElapsed(crl::time now) const;
	[[nodiscard]] SendingRowSettleProgress settleProgress(
		crl::time now) const;
	[[nodiscard]] QRectF diamondCanvas(
		const SendingRowLayout &layout,
		const SendingRowSettleTarget &target,
		crl::time elapsed) const;
	[[nodiscard]] QTransform surfaceTransform(crl::time now) const;
	[[nodiscard]] int surfaceSkip() const;
	void updateSurfaceGeometry();
	void paintSurface();
	void paintAvatar(
		Painter &p,
		const SendingRowLayout &layout,
		float64 cutout,
		float64 swap);
	void paintTexts(
		Painter &p,
		const SendingRowLayout &layout,
		const SendingRowSettleTarget &target,
		const SendingRowSettleProgress &progress);
	void paintSettleAmount(
		Painter &p,
		const SendingRowLayout &layout,
		const SendingRowSettleTarget &target,
		const SendingRowSettleProgress &progress,
		crl::time now);
	void paintItemAmount(
		Painter &p,
		const SendingRowSettleProgress &progress,
		crl::time now);
	void paintChip(Painter &p);
	void paintDiamond(QPainter &p, QRectF canvas, crl::time now);
	void startAnimation();
	void startSettle(HistoryRowContent content, bool burst, Fn<void()> done);
	[[nodiscard]] bool glareOwed(crl::time now) const;
	void tickGlare(crl::time now);
	void releaseSettleWait();

	const not_null<Ui::RpWidget*> _layer;
	const not_null<Ui::RpWidget*> _bounds;
	const std::shared_ptr<CollectibleMedia> _media;
	HistoryRowContent _content;
	HistoryRowText _text;
	HistoryRowLayout _layout;
	HistoryRowChipState _chip;
	AmountPainter _amount;
	std::unique_ptr<Lottie::Icon> _diamond;
	std::unique_ptr<Ui::PeerUserpicView> _userpic;
	rpl::lifetime _userpicLifetime;
	Ui::BoxShadow _shadow;
	GlareCycle _glare;
	Ui::Animations::Basic _animation;
	base::unique_qptr<Ui::RpWidget> _surface;
	rpl::lifetime _chipLifetime;
	std::unique_ptr<Settle> _settle;
	std::optional<SendingRowSettleRequest> _waiting;
	crl::time _started = 0;
	crl::time _diamondStarted = 0;
	crl::time _bumpAt = 0;
	int _digitsHeight = 0;
	bool _inView = true;
	bool _diamondAway = false;
	bool _glareSeen = false;

};

[[nodiscard]] int DigitsHeight(const style::font &font);

[[nodiscard]] float64 SettleRamp(crl::time t, crl::time from, crl::time till);

[[nodiscard]] int SendingRowDiamondCanvas();

[[nodiscard]] int SendingRowDiamondLeft(int width);

[[nodiscard]] float64 SendingRowAmountRight(int width);

[[nodiscard]] float64 SendingRowEntrance(crl::time elapsed);

[[nodiscard]] float64 SendingRowBump(crl::time elapsed);

[[nodiscard]] ClockStyle SendingRowClockStyle();

[[nodiscard]] Ui::StarBurstDescriptor SendingRowBurstDescriptor(
		QColor color);

struct SendingHistoryRow::Settle {
	HistoryRowContent content;
	HistoryRowText text;
	HistoryRowLayout layout;
	std::unique_ptr<Ui::StarBurst> burst;
	std::unique_ptr<Ui::PeerUserpicView> userpic;
	rpl::lifetime userpicLifetime;
	crl::time started = 0;
	base::Timer finish;
	Fn<void()> done;
	float64 scale = 1.;
	int fraction = 0;
	bool finished = false;
};



not_null<SendingHistoryRow*> AddSendingHistoryRow(
		not_null<Ui::VerticalLayout*> slot,
		not_null<Ui::RpWidget*> layer,
		not_null<Ui::RpWidget*> bounds,
		const HistoryRowContent &content,
		std::shared_ptr<CollectibleMedia> media,
		Fn<void()> clicked);

[[nodiscard]] bool ShowsCollectible(const TransferItem &item);

[[nodiscard]] QString RowStatusSubtitle(TransferItem::Status status);

[[nodiscard]] HistoryRowContent RowContentFromItem(
		const TransferItem &item,
		not_null<Main::Session*> session);

void AddWalletLottie(
		not_null<Ui::GenericBox*> box,
		const style::margins &margin = style::margins(),
		Ui::VerticalLayout *container = nullptr);

void AddDetailsAmountHeader(
		not_null<Ui::VerticalLayout*> layout,
		const TransferItem &item,
		int topSkip,
		int bottomSkip,
		rpl::producer<FiatRate> rate = nullptr);

void AddDetailsCollectibleHeader(
		not_null<Ui::VerticalLayout*> layout,
		not_null<Main::Session*> session,
		std::shared_ptr<CollectibleMedia> media,
		const TransferItem &item,
		int bottomSkip,
		bool withCollection = true);

class ActionRow final : public Ui::RpWidget {
public:
	ActionRow(QWidget *parent, ActionRowArgs &&args);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	const RowAvatar _avatar = RowAvatar::Gear;
	const not_null<Ui::FlatLabel*> _title;
	Ui::FlatLabel *_subtitle = nullptr;
	Ui::FlatLabel *_major = nullptr;
	Ui::FlatLabel *_minor = nullptr;
	int _circleTop = 0;

};

[[nodiscard]] RowAvatar ActionRowAvatar(ActionRowIcon icon);

} // namespace ContentDetails

object_ptr<Ui::RpWidget> MakeActionRow(
		not_null<QWidget*> parent,
		ActionRowArgs args);

object_ptr<Ui::RpWidget> MakeCommentBubble(
		not_null<QWidget*> parent,
		object_ptr<Ui::RpWidget> content,
		const style::color &bg);

namespace ContentDetails {

[[nodiscard]] bool HasDetailsComment(const TransferItem &item);

[[nodiscard]] bool SameDetailsHeader(
		const TransferItem &a,
		const TransferItem &b);

void AddDetailsComment(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::VerticalLayout*> layout,
		std::shared_ptr<Main::SessionShow> show,
		const TransferItem &item,
		Fn<bool()> originCurrent);

[[nodiscard]] int GaslessDailyTransfers(not_null<Main::Session*> session);

[[nodiscard]] rpl::producer<int> GaslessDailyTransfersValue(
		not_null<Main::Session*> session);

void ShowNetworkFeesAbout(
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session);

[[nodiscard]] TextWithEntities GramMark(
		Ui::Text::CustomEmojiHelper &helper,
		const style::font &font);

// A transaction a message named is served after the box is already open, so
// the fee row exists from the first frame and says what it is waiting for.
enum class DetailsFee {
	Known,
	Loading,
	Failed,
};

void AddPendingFeeTableRow(
		not_null<Ui::TableLayout*> table,
		DetailsFee state);

void AddFeeTableRow(
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		const TransferItem &item);

[[nodiscard]] object_ptr<Ui::RpWidget> PeerCounterpartyValue(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Main::SessionShow> show,
		not_null<PeerData*> peer);

void AddPeerCounterpartyRows(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::TableLayout*> table,
		std::shared_ptr<Main::SessionShow> show,
		not_null<PeerData*> peer,
		const TransferItem &item);

} // namespace ContentDetails

not_null<Ui::TableLayout*> AddDetailsTableFrame(
		not_null<Ui::VerticalLayout*> container);

namespace ContentDetails {

void AddDetailsTable(
		not_null<Ui::GenericBox*> box,
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		const TransferItem &item,
		DetailsFee fee);

void AddBoxCloseButton(
		not_null<Ui::GenericBox*> box,
		Fn<void()> close = nullptr);

[[nodiscard]] rpl::producer<QString> BusyFooterLabel(
		rpl::producer<QString> text,
		rpl::producer<bool> busy);

void AddBusyFooterSpinner(
		not_null<Ui::RoundButton*> button,
		rpl::producer<bool> shown);

void AddRowSpinner(
		not_null<Ui::SettingsButton*> button,
		rpl::producer<bool> shown);

void WalletBusyBox(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> text);

[[nodiscard]] QImage ReceiveQrCenter(int side, int markSide);

[[nodiscard]] QImage ReceiveQrImage(
		const QString &address,
		int size,
		int ratio,
		int quietZoneModules = 0);

struct WalletBoxTitleBar {
	not_null<Ui::FlatLabel*> title;
	not_null<Ui::IconButton*> close;
};

[[nodiscard]] WalletBoxTitleBar AddWalletBoxTitleBar(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title);

struct OldWalletAppLink {
	QString appname;
	QString startapp;
	std::optional<QString> startattach;
	bool compact = false;
	bool fullscreen = false;
};

[[nodiscard]] bool IsValidOnrampUrl(const QString &url);

[[nodiscard]] std::optional<OldWalletAppLink> ParseOldWalletAppLink(
		not_null<Main::Session*> session,
		const QString &url);

void OpenOldWalletApp(
		not_null<UserData*> bot,
		std::shared_ptr<Ui::Show> show,
		const OldWalletAppLink &link);

void OpenOldWalletAppLink(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		const OldWalletAppLink &link,
		const QString &url);

void OpenWalletUrl(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show,
		const QString &url);

not_null<Ui::IconButton*> AddRowChevron(not_null<Ui::RpWidget*> button);

void WalletReceiveBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		const QString &address);

void ShowWalletReceiveBox(
		not_null<Main::Session*> session,
		std::shared_ptr<Ui::Show> show);

void AddWalletFeaturesBody(
		not_null<Ui::GenericBox*> box,
		rpl::producer<QString> title,
		rpl::producer<QString> subtitle,
		const std::vector<Ui::FeatureListEntry> &features,
		rpl::producer<QString> button);

void WalletHowItWorksBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session);

[[nodiscard]] Ui::LayerStackWidget *BoxLayerStack(
		not_null<Ui::GenericBox*> box);

void CloseFirstGramsByOutsideClick(not_null<Ui::GenericBox*> box);

void WalletFirstGramsBox(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session);

void WalletCloudPasswordCreateBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show);

void WalletCloudPasswordIntroBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show);

void SetupIntroTooltip(
		not_null<Ui::RpWidget*> parent,
		not_null<Ui::RpWidget*> card,
		Fn<QRect()> markRect,
		rpl::producer<> moves);

[[nodiscard]] QString ExplorerTransactionUrl(
		not_null<Main::Session*> session,
		const QByteArray &traceId);

void WalletTransactionBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		TransferItem item,
		bool partial,
		std::shared_ptr<CollectibleMedia> media,
		Fn<bool()> originCurrent,
		rpl::producer<> originInvalidated,
		Fn<void()> openWallet,
		rpl::producer<TransferItem> updates);

void ShowWalletTransactionBox(
		std::shared_ptr<Main::SessionShow> show,
		const TransferItem &item,
		std::shared_ptr<CollectibleMedia> media = nullptr,
		rpl::producer<TransferItem> updates = nullptr);

[[nodiscard]] int CommentBytes(const QString &text);

[[nodiscard]] bool CommentFits(const QString &text);

void ApplyCommentLimit(
		not_null<Ui::InputField*> field,
		int rightSkip = 0);

struct SendQuoteDependencies {
	std::optional<TransferWalletIdentity> senderIdentity;
	GaslessTerms gaslessTerms;
	QString destination;
	SendComment comment;
	DeviceCustodyState custody;
	QByteArray recipientPublicKey;
	UserId userId;
	int64 amountNano = 0;
	int64 balanceNano = 0;
	int64 minTransferNano = 0;
	bool bounce = false;
	bool ready = false;
	bool valid = false;

	friend bool operator==(
		const SendQuoteDependencies &,
		const SendQuoteDependencies &) = default;
};

struct SendQuote {
	SendArgs args;
	SendQuoteDependencies dependencies;
	std::shared_ptr<const PreparedSend> prepared;
	int64 feeNano = 0;
	uint64 revision = 0;

	friend bool operator==(const SendQuote &, const SendQuote &) = default;
};

struct SendDraft {
	rpl::variable<SendComment> comment;
	// False once the recipient is known to take plain comments only. It lives
	// here because the comment editor outlives the box that opened it.
	rpl::variable<bool> encryptable = true;
	rpl::variable<std::optional<SendQuote>> quote;
	rpl::variable<bool> preparing = false;
	KeyAuthorization authorization;
	std::optional<quint32> privateEpoch;
};

struct SendFlow {
	QString destination;
	bool bounce = true;
	QString displayForm;
	int64 amountNano = 0;
	std::optional<uint64> expiresAt;
	std::shared_ptr<SendDraft> draft;
	QString tonName;
};

class SendCommentBubble final : public Ui::RpWidget {
public:
	SendCommentBubble(
		QWidget *parent,
		rpl::producer<SendComment> comment,
		rpl::producer<bool> clickable,
		Fn<void()> clicked);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	void setText(const QString &text);

	const not_null<Ui::AbstractButton*> _button;
	Ui::Text::String _text = { 1 };
	Ui::UniqueGiftMessageBubble::Layout _layout;
	QPainterPath _path;
	bool _clickable = false;

};

[[nodiscard]] int AddressGroupsWidth(
	const style::font &font,
	const QString &address,
	int groupsPerLine);

void PaintAddressGroups(
	QPainter &p,
	const style::font &font,
	const QString &address,
	QPoint origin,
	int groupsPerLine);

class SendRecipientCard final : public Ui::RpWidget {
public:
	SendRecipientCard(
		QWidget *parent,
		std::shared_ptr<Ui::Show> show,
		UserData *user,
		rpl::producer<QString> address,
		Fn<void()> about);

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;

private:
	[[nodiscard]] int nameHeight() const;
	[[nodiscard]] QRect addressRect(int outerWidth) const;
	void setAddress(const QString &address);

	UserData * const _user = nullptr;
	Ui::IconButton * const _about = nullptr;
	const not_null<Ui::AbstractButton*> _copy;
	style::TextStyle _nameStyle;
	style::TextStyle _usernameStyle;
	Ui::PeerUserpicView _userpic;
	Ui::Text::String _name;
	Ui::Text::String _username;
	QString _address;

};

enum class KeyActionKind {
	Plain,
	Reveal,
	ResumeAfterRestore,
};

// The one ladder every key-requiring wallet action climbs: the action runs
// at once when this device holds the key, after the backup restore and its
// protection chooser when the key is only in the cloud, after the phrase
// import when there is no backup, and after the conflict is resolved when a
// different wallet is parked here. A context turns the ladder's prompts into
// one attempt that ends when any of them closes without a key.
void RunKeyRequiringAction(
	std::shared_ptr<Main::SessionShow> show,
	Fn<void()> action,
	KeyActionKind kind = KeyActionKind::Plain,
	std::shared_ptr<KeyContext> context = nullptr,
	rpl::producer<QString> importAbout = nullptr);

void WalletConflictBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	Fn<void()> switched);

[[nodiscard]] std::optional<SendFlow> ParseRecipientFlow(
		const QString &text);

[[nodiscard]] QStringList SplitPhraseWords(const QString &text);

enum class RecipientInputKind : uchar {
	Empty,
	Address,
	Name,
	Invalid,
	Search,
};

struct RecipientInput {
	RecipientInputKind kind = RecipientInputKind::Empty;
	std::optional<SendFlow> flow;
};

enum class RecipientError : uchar {
	Invalid,
	NameNotFound,
	NameFailed,
	LookupFailed,
	OwnWallet,
};

[[nodiscard]] rpl::producer<QString> RecipientErrorText(
		RecipientError error);

[[nodiscard]] bool IsTonDnsName(const QString &text);

[[nodiscard]] RecipientInput ClassifyRecipientInput(const QString &text);

[[nodiscard]] not_null<Ui::InputField*> AddCommentField(
		not_null<Ui::GenericBox*> box,
		const QString &comment);

[[nodiscard]] not_null<Ui::InputField*> AddSendField(
		not_null<Ui::VerticalLayout*> container,
		const style::InputField &st,
		rpl::producer<QString> placeholder,
		const QString &value);

void BindCommentField(
		not_null<Ui::InputField*> field,
		const std::shared_ptr<SendDraft> &draft);

void AddCommentPrivacy(
		not_null<Ui::VerticalLayout*> container,
		const std::shared_ptr<SendDraft> &draft,
		const style::margins &margin,
		rpl::producer<bool> encryptable);

void WalletSendCommentBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<SendDraft> draft,
		Fn<bool()> originValid,
		rpl::producer<bool> encryptable);

void ShowKeyChangedBox(
		std::shared_ptr<Main::SessionShow> show,
		v::text::data text);

} // namespace ContentDetails

QString SendErrorText(SendError error, int64 minTransferNano);

void ShowWalletKeyChanged(std::shared_ptr<Main::SessionShow> show);

QString ErrorWithType(const QString &message, const QString &error);

namespace ContentDetails {

[[nodiscard]] QString SendUserLoadErrorText(const QString &error);

void ApplyButtonDisabledLook(not_null<Ui::RoundButton*> button);

void SetButtonDisabledLook(
		not_null<Ui::RoundButton*> button,
		bool disabled);

[[nodiscard]] bool CanSendToUser(
		not_null<Main::Session*> session,
		UserId id);

[[nodiscard]] UserData *SendableUser(
		not_null<Main::Session*> session,
		UserId id);

[[nodiscard]] bool SendsToOwnWallet(
		not_null<Main::Session*> session,
		const QString &destination);

void ChooseMoneyRecipient(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		not_null<UserData*> user);

class RecentMoneyRecipientsController final
	: public PeerListController
	, public base::has_weak_ptr {
public:
	RecentMoneyRecipientsController(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		Fn<void(not_null<UserData*>)> choose);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	void setContent(not_null<PeerListContent*> content);
	void clear();
	[[nodiscard]] rpl::producer<bool> shownValue() const;

private:
	[[nodiscard]] bool active() const;
	[[nodiscard]] bool canOffer(not_null<UserData*> user) const;
	void fillIfEmpty();
	void refresh();
	void scheduleRefresh();
	void watchUsers();

	const base::weak_qptr<Ui::GenericBox> _box;
	const std::shared_ptr<Main::SessionShow> _show;
	const base::weak_ptr<Main::Session> _session;
	const Fn<void(not_null<UserData*>)> _choose;
	PeerListContentDelegateSimple _delegate;
	std::vector<not_null<UserData*>> _users;
	rpl::lifetime _userLifetime;
	rpl::variable<bool> _shown = false;
	bool _closed = false;
	bool _refreshQueued = false;

};



[[nodiscard]] object_ptr<Ui::RpWidget> MakeRecentMoneyRecipientsList(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<bool> hidden,
		Fn<void(not_null<UserData*>)> choose);

class MoneyRecipientSearchController final
	: public ChatsListBoxController {
public:
	MoneyRecipientSearchController(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		Fn<void(not_null<UserData*>)> choose);

	Main::Session &session() const override;
	void rowClicked(not_null<PeerListRow*> row) override;
	void setContent(not_null<PeerListContent*> content);

protected:
	std::unique_ptr<Row> createRow(not_null<History*> history) override;
	void prepareViewHook() override;

private:
	[[nodiscard]] UserData *offered(not_null<PeerData*> peer) const;

	const base::weak_qptr<Ui::GenericBox> _box;
	const std::shared_ptr<Main::SessionShow> _show;
	const not_null<Main::Session*> _session;
	const Fn<void(not_null<UserData*>)> _choose;
	PeerListContentDelegateShow _delegate;
	bool _closed = false;

};



[[nodiscard]] object_ptr<Ui::RpWidget> MakeMoneyRecipientSearchList(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<QString> query,
		Fn<void(not_null<UserData*>)> choose);

enum class TonNameStatus : uchar {
	None,
	Pending,
	Resolved,
	NotFound,
	Failed,
};

struct TonNameState {
	QString name;
	QString address;
	QString displayForm;
	TonNameStatus status = TonNameStatus::None;

	friend bool operator==(
		const TonNameState &,
		const TonNameState &) = default;
};

[[nodiscard]] bool TonNameRowShown(const TonNameState &state);

class TonNameResultRow final : public PeerListRow {
public:
	TonNameResultRow(const QString &name, const QString &address);

	QString generateName() override;
	QString generateShortName() override;
	PaintRoundImageCallback generatePaintUserpicCallback(
		bool forceRound) override;
	void paintStatusText(
		Painter &p,
		const style::PeerListItem &st,
		int x,
		int y,
		int availableWidth,
		int outerWidth,
		bool selected) override;

private:
	const QString _name;
	const QString _address;

};

class TonNameResultController final : public PeerListController {
public:
	TonNameResultController(
		not_null<Main::Session*> session,
		Fn<void()> chosen);

	void prepare() override;
	void rowClicked(not_null<PeerListRow*> row) override;
	Main::Session &session() const override;

	void setContent(not_null<PeerListContent*> content);
	void showState(const TonNameState &state);

private:
	const not_null<Main::Session*> _session;
	const Fn<void()> _chosen;
	PeerListContentDelegateSimple _delegate;

};

// WHY: engine jobs run one at a time and never coalesce, so one lookup
// stays out and the latest name is asked as soon as it returns.
class TonNameLookup final : public base::has_weak_ptr {
public:
	explicit TonNameLookup(not_null<Main::Session*> session);

	void setName(const QString &name);
	void request();
	void close();

	[[nodiscard]] const TonNameState &current() const;
	[[nodiscard]] rpl::producer<TonNameState> value() const;

private:
	void start();
	void issue();
	[[nodiscard]] bool settle(uint64 revision, bool busy);
	void resolved(uint64 revision, std::optional<QString> address);
	void failed(uint64 revision, DnsLookupError error);
	void finish(TonNameStatus status);

	const base::weak_ptr<Main::Session> _session;
	rpl::variable<TonNameState> _state;
	base::Timer _timer;
	base::Timer _deadline;
	uint64 _revision = 0;
	bool _waiting = false;
	bool _inFlight = false;
	bool _closed = false;

};





[[nodiscard]] object_ptr<Ui::RpWidget> MakeTonNameResultList(
		not_null<Ui::GenericBox*> box,
		not_null<Main::Session*> session,
		rpl::producer<TonNameState> state,
		Fn<void()> chosen);

[[nodiscard]] rpl::producer<TextWithEntities> SendRecipientTitle(
		not_null<Ui::GenericBox*> box,
		const QString &recipient,
		Qt::TextElideMode mode);

struct SendConfirmFee {
	std::optional<int64> feeNano;
	bool gasless = false;
	bool pending = false;

	friend bool operator==(
		const SendConfirmFee &,
		const SendConfirmFee &) = default;
};

struct SendConfirmArgs {
	std::shared_ptr<SendDraft> draft;
	QString address;
	int64 amountNano = 0;
	rpl::producer<SendConfirmFee> fee;
	rpl::producer<QString> refusal;
	rpl::producer<bool> busy;
	rpl::producer<bool> canSend;
	Fn<void(SendConfirmFee shown)> send;
};

[[nodiscard]] SendConfirmFee QuoteConfirmFee(const SendQuote &quote);

void FillSendConfirmTable(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		const QString &address,
		const SendConfirmFee &fee,
		TimeId date);

void AddSendCommentLock(
		not_null<Ui::InputField*> field,
		const style::InputField &st,
		std::shared_ptr<SendDraft> draft);

struct SendConfirmNotes {
	not_null<Ui::RpWidget*> caption;
	not_null<Ui::RpWidget*> refusal;
};

SendConfirmNotes AddSendConfirmNotes(
		not_null<Ui::GenericBox*> box,
		rpl::producer<bool> captionShown,
		rpl::producer<QString> refusal);

void WalletSendConfirmBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		SendConfirmArgs args);

struct CollectibleTransfer {
	QString address;
	std::shared_ptr<CollectibleMedia> media;
};

struct CollectibleRecipient {
	QString destination;
	QString tonName;
	UserId userId;
	bool bounce = true;
};

[[nodiscard]] rpl::producer<QString> CollectibleNameValue(
		std::shared_ptr<CollectibleMedia> media,
		QString address);

void CollectibleTransferBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<const CollectibleTransfer> collectible,
		CollectibleRecipient recipient);

void ShowSendRecipientWallet(
	std::shared_ptr<Ui::Show> show,
	not_null<UserData*> user,
	const QString &address,
	Fn<void()> profile);

void WalletSendBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::optional<SendFlow> initial,
		UserData *user,
		Fn<void()> sent,
		Fn<void()> notReady,
		int64 amountNano,
		base::weak_qptr<Ui::BoxContent> origin);

// WHY: an entry point decides about the recipient only from the served
// wallet state, and only the call that ends the wait acts, so an answer
// landing after a timeout or a dismissed busy box changes nothing.
struct SendEntryWait {
	std::shared_ptr<Main::SessionShow> show;
	rpl::variable<QString> text;
	QString timeoutError;
	Fn<void()> closeBusy;
	rpl::lifetime lifetime;
	bool ended = false;
};

bool EndSendEntryWait(const std::shared_ptr<SendEntryWait> &wait);

void FailSendEntryWait(
		const std::shared_ptr<SendEntryWait> &wait,
		const QString &error);

[[nodiscard]] std::shared_ptr<SendEntryWait> StartSendEntryWait(
		std::shared_ptr<Main::SessionShow> show);

void AwaitWalletReady(
		const std::shared_ptr<SendEntryWait> &wait,
		Fn<void()> ready,
		Fn<void()> notReady = nullptr);

void WhenWalletReady(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> ready,
		Fn<void()> notReady = nullptr);

void OpenSendFlow(
		std::shared_ptr<Main::SessionShow> show,
		SendFlow flow,
		AddressOwner owner,
		base::weak_qptr<Ui::BoxContent> origin = nullptr);

void ResolveOwnerAndOpenSendFlow(
		std::shared_ptr<Main::SessionShow> show,
		SendFlow flow);

[[nodiscard]] CollectibleRecipient CollectibleRecipientFrom(
		not_null<Main::Session*> session,
		const SendFlow &flow,
		const AddressOwner &owner);

void WalletSendRecipientBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		QString text,
		std::shared_ptr<const CollectibleTransfer> collectible);

[[nodiscard]] QString PhraseBoxLottie(int wordsCount);

void AddPhraseBoxHeader(
		not_null<Ui::GenericBox*> box,
		const QString &lottieName,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> text,
		const style::FlatLabel &textLabel,
		int lottieSize,
		const style::margins &lottieMargin,
		const style::margins &textMargin);

void AddPhraseGrid(
		not_null<Ui::GenericBox*> box,
		const std::vector<QString> &words);

void WalletPhraseBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::vector<QString> words);

[[nodiscard]] TextWithEntities EnforcementCheckAbout(const QString &error);

enum class PhraseOperation {
	Reveal,
	Restore,
	DropParked,
};

void ShowPhraseError(
		std::shared_ptr<Main::SessionShow> show,
		PhraseOperation operation,
		const QString &error);

[[nodiscard]] bool RestoreIsFirstKeyUse(not_null<Main::Session*> session);

[[nodiscard]] PasscodeBox::CloudFields RestorePasswordFields(
		const Core::CloudPasswordState &state,
		bool firstKeyUse,
		const QString &description);

void RequestPhraseReveal(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> warning,
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> unblock,
		std::optional<QByteArray> parkedKey = std::nullopt,
		Fn<void(std::vector<QString>)> onWords = nullptr,
		Fn<void()> onAuthorized = nullptr,
		Fn<void(std::vector<QString>, CustodyOutcome)> onPrepared = nullptr,
		Fn<void()> onPromptError = nullptr,
		Fn<void()> onPasswordMissing = nullptr);

void StartPhraseReveal(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> warning,
		KeyAuthorization auth,
		Fn<void()> unblock,
		std::optional<QByteArray> parkedKey = std::nullopt,
		Fn<void(std::vector<QString>)> onWords = nullptr,
		Fn<void()> onAuthorized = nullptr,
		Fn<void(std::vector<QString>, CustodyOutcome)> onPrepared = nullptr,
		Fn<void()> onPromptError = nullptr,
		Fn<void()> onPromptClosed = nullptr,
		Fn<bool()> onPromptSubmit = nullptr);

void WalletPhraseWarningBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::optional<QByteArray> parkedKey);

void WalletRevealFlow(
		std::shared_ptr<Main::SessionShow> show,
		std::optional<QByteArray> parkedKey = std::nullopt);

enum class WalletImportMode {
	Replace,
	Restore,
};

// The about text replaces the mode's default cover line, so an import that
// serves a specific action can say what the phrase is needed for.
void WalletImportBox(
	not_null<Ui::GenericBox*> box,
	std::shared_ptr<Main::SessionShow> show,
	WalletImportMode mode,
	Fn<void()> restored,
	std::shared_ptr<KeyContext> context,
	rpl::producer<QString> about);

void ShowInvalidSecretWords(
		std::shared_ptr<Main::SessionShow> show,
		bool foreign,
		std::shared_ptr<KeyContext> context = nullptr);

int AddressGroupsWidth(
		const style::font &font,
		const QString &address,
		int groupsPerLine);

void PaintAddressGroups(
		QPainter &p,
		const style::font &font,
		const QString &address,
		QPoint origin,
		int groupsPerLine);

void AddAddressPlate(
		not_null<Ui::VerticalLayout*> container,
		const QString &address,
		const style::margins &margin,
		std::shared_ptr<Ui::Show> show = nullptr);

void ShowWrongSecretWords(
		std::shared_ptr<Main::SessionShow> show,
		bool outdated,
		std::shared_ptr<KeyContext> context);

void ShowSendRecipientWallet(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> user,
		const QString &address,
		Fn<void()> profile);

void RunWhenSigningReady(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> action);

void RequestCustodyRestore(
		std::shared_ptr<Main::SessionShow> show,
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> action,
		Fn<void()> unblock,
		std::shared_ptr<KeyContext> context = nullptr,
		Fn<void()> onPasswordMissing = nullptr);

void StartCustodyRestore(
		std::shared_ptr<Main::SessionShow> show,
		KeyAuthorization auth,
		Fn<void()> action,
		Fn<void()> unblock = nullptr,
		std::shared_ptr<KeyContext> context = nullptr);

void ResolveDeviceCustody(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> resolved,
		std::shared_ptr<KeyContext> context);

void RunKeyRequiringAction(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> action,
		KeyActionKind kind,
		std::shared_ptr<KeyContext> context,
		rpl::producer<QString> importAbout);

void RequestWalletReplace(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		std::optional<std::vector<QString>> words,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> unblock,
		Fn<void(const QString &text)> showError);

void StartWalletReplace(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		std::optional<std::vector<QString>> words,
		Fn<void()> unblock,
		Fn<void(const QString &text)> showError = nullptr);

struct BackupDisableProof {
	KeyAuthorization auth;
	BackupDisableApproval approved;
};

void ShowBackupChangeError(
		std::shared_ptr<Main::SessionShow> show,
		const QString &error);

void RequestBackupChange(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> unblock,
		Fn<void()> done,
		std::optional<BackupDisableProof> proof = std::nullopt);

void StartBackupRequest(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		Fn<void()> unblock,
		Fn<void()> done);

void RequestBackupDisable(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		BackupDisableProof proof,
		Fn<void()> unblock,
		Fn<void()> done);

void ShowBackupEnabledToast(std::shared_ptr<Main::SessionShow> show);

void StartBackupEnable(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy);

void GuardBackupDisableDismiss(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		Fn<bool()> abandons);

void WalletBackupPhraseBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::vector<QString> words,
		Fn<void(std::vector<QString>)> next,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> text);

[[nodiscard]] std::vector<int> BackupQuizIndices(int count);

[[nodiscard]] rpl::producer<TextWithEntities> BackupQuizText(
		const std::vector<int> &indices);

[[nodiscard]] not_null<Ui::InputField*> AddBackupQuizField(
		not_null<Ui::VerticalLayout*> container,
		int index);

[[nodiscard]] bool BackupQuizAnswerMatches(
		not_null<Ui::InputField*> field,
		const QString &word);

void WalletBackupQuizBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::vector<QString> words,
		Fn<void()> passed,
		Fn<void(Fn<void()> lock)> publishLock,
		Fn<bool()> abandons);

void ShowBackupDisabledToast(std::shared_ptr<Main::SessionShow> show);

void CollectBackupPhrase(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		KeyAuthorization auth);

[[nodiscard]] QString RotationFeeText(
		tr::phrase<lngtag_amount, lngtag_fiat> phrase,
		int64 feeNano,
		const FiatRate &rate);

[[nodiscard]] QString RotationFailureReason(const QString &error);

void ShowRotationFailedToast(
		std::shared_ptr<Main::SessionShow> show,
		const QString &error);

void SetBoxBusy(not_null<Ui::GenericBox*> box);

struct RotationState {
	bool submitted = false;
	base::weak_qptr<Ui::GenericBox> quiz;
	Fn<void()> lockQuiz;
	KeyAuthorization auth;
};

void SubmitRotation(
		std::shared_ptr<Main::SessionShow> show,
		base::weak_qptr<Ui::GenericBox> origin,
		not_null<bool*> busy,
		std::shared_ptr<RotationState> state);

void ShowRotationPhrase(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		std::vector<QString> words,
		KeyAuthorization auth);

void StartRotation(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		int64 feeNano,
		KeyAuthorization auth);

void ShowBackupTopUpAlert(
		std::shared_ptr<Main::SessionShow> show,
		int64 feeNano,
		Fn<void()> topUp);

struct RotationQuote {
	int64 feeNano = 0;
	SendError error = SendError::None;

	friend inline bool operator==(
		const RotationQuote &,
		const RotationQuote &) = default;
};

struct BackupDisableState {
	KeyAuthorization auth;
	rpl::variable<std::optional<RotationQuote>> quote;
	rpl::variable<bool> loading = false;
	Fn<void()> onQuoted;
	bool rotate = false;
	bool started = false;
};

[[nodiscard]] bool RotationQuoteUsable(
		const std::optional<RotationQuote> &quote);

[[nodiscard]] rpl::producer<TextWithEntities> BackupUpdateNoteText(
		not_null<Main::Session*> session,
		rpl::producer<std::optional<RotationQuote>> quote);

void AddBackupUpdateNote(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<TextWithEntities> text,
		rpl::producer<bool> shown);

void WalletBackupDisableBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		base::weak_qptr<Ui::GenericBox> origin,
		not_null<bool*> busy,
		std::shared_ptr<BackupDisableState> state,
		bool updateOffered);

void ShowBackupDisableBox(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		KeyAuthorization auth);

void StartBackupDisable(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		not_null<rpl::variable<bool>*> restoring);

struct ImportCover {
	not_null<Ui::RpWidget*> widget;
	Fn<int()> height;
	Fn<void()> updateScroll;
};

[[nodiscard]] ImportCover SetupImportCover(
		not_null<Ui::GenericBox*> box,
		WalletImportMode mode,
		rpl::producer<QString> about);

void WalletImportBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		WalletImportMode mode,
		Fn<void()> restored,
		std::shared_ptr<KeyContext> context,
		rpl::producer<QString> about);

void WalletReplaceBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show);

void WalletConflictBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> switched);

void AddBackupSection(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> box);

void WalletKeysBackupBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show);

[[nodiscard]] BalancePalette CardBalancePalette();

[[nodiscard]] BalancePalette SettledBalancePalette();

[[nodiscard]] Wallet::AmountStyle MoneyAmountStyle();

[[nodiscard]] float64 BalanceAmountScale(float64 progress);

[[nodiscard]] float64 BalanceFiatScale(float64 progress);

[[nodiscard]] float64 BalanceSettledTop();

[[nodiscard]] QPointF BalanceRowPosition(
		const CardFold &fold,
		int restTop,
		float64 landedTop);

[[nodiscard]] QRectF MarkInkBounds(const QImage &image);

[[nodiscard]] rpl::producer<TextWithEntities> CardNameValue(
		not_null<Main::Session*> session);

void SetupCardBalance(
		not_null<BalanceInk*> ink,
		not_null<Main::Session*> session,
		Fn<void()> repaint,
		not_null<Ui::RpWidget*> owner);

void SetupCardMark(
		not_null<BalanceInk*> ink,
		not_null<Ui::RpWidget*> owner,
		std::shared_ptr<bool> played,
		Fn<void()> repaint);



CardFold ComputeCardFold(QRect cardRest, float64 fold);

[[nodiscard]] bool HistoryShown(not_null<Main::Session*> session);

[[nodiscard]] rpl::producer<bool> HistoryShownValue(
		not_null<Main::Session*> session);

[[nodiscard]] rpl::producer<bool> CollectiblesShownValue(
		not_null<Main::Session*> session);

void PaintBottomRoundedPlate(
		QPainter &p,
		QRect rect,
		const style::color &bg);

// The three faces the area under the card can show once the lists are known
// to be empty. They are derived from one producer and are mutually exclusive
// by construction, so no two of the wraps below can ever be open at once.
enum class EmptyFace {
	None,
	About,
	Unavailable,
	Unreachable,
};

struct ListRowKey {
	std::string operationId;
	QString id;

	friend bool operator==(
		const ListRowKey &,
		const ListRowKey &) = default;
};

struct ListedRow {
	ListRowKey key;
	not_null<Ui::RpWidget*> widget;
};

struct ListAnchorRow {
	ListRowKey key;
	int top = 0;
};

[[nodiscard]] std::vector<ListAnchorRow> CountListAnchor(
		const std::vector<ListedRow> &rows,
		not_null<QWidget*> column,
		int visibleTop);

[[nodiscard]] int CountKeptScrollTop(
		const std::vector<ListAnchorRow> &anchor,
		const std::vector<ListedRow> &rows,
		not_null<QWidget*> column,
		int scrollTop,
		int reserve,
		int maxTop);

[[nodiscard]] TextWithEntities IslandAmount(
		const TextWithEntities &mark,
		CreditsAmount amount);

void AddIslandRowLabel(
		not_null<InfoIslandEntry*> button,
		rpl::producer<TextWithEntities> text,
		Ui::Text::MarkedContext context);



class CurrencyListWidget final : public Ui::RpWidget {
public:
	CurrencyListWidget(
		not_null<QWidget*> parent,
		std::shared_ptr<Main::SessionShow> show,
		Fn<void(QString)> chosen);

	void updateFilter(const QString &query);
	void selectSkip(int direction);
	void selectSkipPage(int height, int direction);
	void chooseSelected();
	void scrollToCurrent();

	[[nodiscard]] rpl::producer<Ui::ScrollToRequest> mustScrollTo() const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	struct Row {
		QString code;
		QString name;
		QStringList words;
	};

	[[nodiscard]] const std::vector<Row> &current() const;
	[[nodiscard]] bool rowMatches(const Row &row) const;
	void refreshRows();
	void refreshFiltered();
	void refreshHeight();
	void updateSelected(QPoint localPos);
	void setSelected(int index);
	void setPressed(int pressed);
	void updateRow(int index);

	const std::shared_ptr<Main::SessionShow> _show;
	const Fn<void(QString)> _chosen;
	QString _activeCode;
	QStringList _filter;
	std::vector<Row> _rows;
	std::vector<Row> _filtered;
	std::vector<std::unique_ptr<Ui::RippleAnimation>> _ripples;
	int _selected = -1;
	int _pressed = -1;
	bool _mouseSelection = false;

	rpl::event_stream<Ui::ScrollToRequest> _mustScrollTo;

};

[[nodiscard]] bool ForwardCurrencyNavigation(
		not_null<QKeyEvent*> e,
		not_null<CurrencyListWidget*> list,
		int pageHeight);

void WalletChooseCurrencyBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show);

void AcquireKeyThroughLadder(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CommentScope> scope,
		Fn<bool()> current,
		rpl::lifetime &lifetime,
		Fn<void(KeyAuthorization)> done,
		rpl::producer<QString> importAbout);

} // namespace ContentDetails

void AcquireTransferCommentKey(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CommentScope> scope,
		Fn<bool()> current,
		rpl::lifetime &lifetime,
		Fn<void(KeyAuthorization)> done);

void AcquireWalletKey(
		std::shared_ptr<Main::SessionShow> show,
		Fn<bool()> current,
		rpl::lifetime &lifetime,
		Fn<void(KeyAuthorization)> done,
		rpl::producer<QString> importAbout);

void ShowTransactionDetails(
		std::shared_ptr<Main::SessionShow> show,
		TransferItem item,
		bool partial,
		std::shared_ptr<CollectibleMedia> media,
		Fn<bool()> originCurrent,
		rpl::producer<> originInvalidated,
		Fn<void()> openWallet,
		Ui::LayerOptions options);

void ShowSubmittedTransfer(
		std::shared_ptr<Main::SessionShow> show,
		const std::string &operationId);

bool ShowFirstGramsIfPending(std::shared_ptr<Main::SessionShow> show);

rpl::producer<bool> TransactionsShownValue(
		not_null<Main::Session*> session);

base::unique_qptr<Ui::RpWidget> CreateContent(
		not_null<Ui::SeparatePanel*> panel,
		std::shared_ptr<Main::SessionShow> show);

object_ptr<Ui::RpWidget> MakeWalletCard(
		QWidget *parent,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<bool> markPlayed);

object_ptr<Ui::RpWidget> MakeTransferCard(
		QWidget *parent,
		not_null<Main::Session*> session,
		TransferCardArgs args);

void FillMenu(
		std::shared_ptr<Main::SessionShow> show,
		const Ui::Menu::MenuCallback &addAction);

bool TransferLinkValid(const QString &url);

void ShowTransferLink(
		std::shared_ptr<Main::SessionShow> show,
		const QString &url);

void ShowWalletConflict(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> switched);

Fn<void()> ShowWalletBusyBox(
		std::shared_ptr<Main::SessionShow> show,
		rpl::producer<QString> text,
		Fn<void()> dismissed);

void ShowSendToUser(
		std::shared_ptr<Main::SessionShow> show,
		not_null<UserData*> user,
		Fn<void()> sent,
		int64 amountNano,
		Fn<void()> notReady,
		base::weak_qptr<Ui::BoxContent> origin);

void ShowCollectibleTransfer(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media,
		const QString &collectible);

void ShowSendToLinkRecipient(
		std::shared_ptr<Main::SessionShow> show,
		const QString &recipient,
		int64 amountNano);

} // namespace Wallet
