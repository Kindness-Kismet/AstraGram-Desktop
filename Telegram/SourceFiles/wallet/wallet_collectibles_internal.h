#pragma once

/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "wallet/wallet_collectibles.h"

#include "apiwrap.h"
#include "base/qt/qt_common_adapters.h"
#include "base/call_delayed.h"
#include "base/event_filter.h"
#include "base/timer.h"
#include "base/weak_qptr.h"
#include "boxes/star_gift_box.h"
#include "boxes/star_gift_cover_box.h"
#include "data/data_session.h"
#include "data/data_star_gift.h"
#include "data/data_user_wallet_card.h"
#include "gram/api/gram_api_nft.h"
#include "info/peer_gifts/info_peer_gifts_common.h"
#include "lang/lang_hardcoded.h"
#include "lang/lang_keys.h"
#include "main/session/session_show.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "mtproto/mtproto_response.h"
#include "settings/settings_credits_graphics.h"
#include "ui/effects/ripple_animation.h"
#include "ui/effects/round_area_with_shadow.h"
#include "ui/layers/generic_box.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/basic_click_handlers.h"
#include "ui/painter.h"
#include "wallet/wallet_address.h"
#include "wallet/wallet_card_colors.h"
#include "wallet/wallet_card_drag.h"
#include "wallet/wallet_card_flight.h"
#include "wallet/wallet_card_look.h"
#include "wallet/wallet_card_stickers.h"
#include "wallet/wallet_chat_show.h"
#include "wallet/wallet_collectible_media.h"
#include "wallet/wallet_collectibles_hint.h"
#include "wallet/wallet_content.h"
#include "wallet/wallet_own_card.h"
#include "wallet/wallet_palette.h"
#include "wallet/wallet_session.h"

#include "styles/style_credits.h"
#include "styles/style_giveaway.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_wallet.h"

#include <QtGui/QContextMenuEvent>
#include <QtGui/QWindow>
#include <QtWidgets/QApplication>

namespace Wallet::CollectiblesDetails {

constexpr auto kPreloadScreens = 3;
constexpr auto kPreloadRows = 2;

#if QT_VERSION < QT_VERSION_CHECK(6, 0, 0)
using TouchScreenKey = const QTouchDevice*;
using TouchScreenDevice = TouchScreenKey;

[[nodiscard]] inline TouchScreenKey DeviceOf(not_null<QTouchEvent*> e) {
	return e->device();
}

[[nodiscard]] inline QTouchEvent TouchCancelFrom(TouchScreenDevice device) {
	return QTouchEvent(
		QEvent::TouchCancel,
		const_cast<QTouchDevice*>(device));
}
#else // Qt < 6.0.0
using TouchScreenKey = const QPointingDevice*;
using TouchScreenDevice = QPointer<const QPointingDevice>;

[[nodiscard]] inline TouchScreenKey DeviceOf(not_null<QTouchEvent*> e) {
	return e->pointingDevice();
}

[[nodiscard]] inline QTouchEvent TouchCancelFrom(const TouchScreenDevice &device) {
	return QTouchEvent(QEvent::TouchCancel, device.data());
}
#endif // Qt >= 6.0.0

struct CoverCardState {
	bool onCard = false;
	bool source = false;
	bool allowed = false;
	bool listed = false;

	friend inline bool operator==(
		const CoverCardState &,
		const CoverCardState &) = default;
};

class CollectiblesGrid final : public Ui::RpWidget {
public:
	CollectiblesGrid(
		QWidget *parent,
		not_null<Ui::ScrollArea*> scroll,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CollectibleMedia> media,
		CardHost host);
	~CollectiblesGrid();

	void rebuild();
	[[nodiscard]] CollectiblesSquares squares();

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) override;
	bool eventHook(QEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;

private:
	struct Square {
		CollectibleRecord record;
		bool onCard = false;
		bool silhouette = false;
		std::unique_ptr<Info::PeerGifts::GiftButton> gift;
		std::unique_ptr<Ui::AbstractButton> thumb;
	};
	struct Flight {
		std::unique_ptr<CardFlight> flight;
		uint64 id = 0;
	};
	struct Press {
		QString address;
		QPoint origin;
		QPointF grab;
		bool draggable = false;
		bool grabbed = false;
	};
	struct Drag {
		QString address;
		DocumentData *document = nullptr;
		QImage firstFrame;
		uint64 generation = 0;
		std::unique_ptr<DraggedGift> gift;
	};
	enum class TouchPhase {
		Waiting,
		Held,
		Ignored,
	};
	struct Touch {
		QString address;
		QPoint origin;
		TouchScreenDevice device = {};
		int id = 0;
		TouchPhase phase = TouchPhase::Waiting;
		bool moved = false;
	};

	[[nodiscard]] int rowsCount() const;
	[[nodiscard]] std::pair<int, int> rowsRange(
		int top,
		int bottom,
		int preload) const;
	[[nodiscard]] QRect squareRect(int index) const;
	[[nodiscard]] int squareAt(QPoint point) const;
	[[nodiscard]] int indexOf(const QString &address) const;
	[[nodiscard]] std::optional<QRect> hintTarget() const;

	void invalidateWindows();
	void refreshWindows();
	void refreshSquares();
	void refreshMediaWindow();
	void applyOnCard(const std::vector<Data::WalletCardSticker> &stickers);
	void applySquare(Square &square, const CollectibleRecord &record);
	void activate(const QString &address);
	[[nodiscard]] std::optional<CoverCardState> coverState(
		const QString &address) const;
	[[nodiscard]] std::vector<Ui::UniqueGiftCoverAction> coverActions(
		not_null<Ui::GenericBox*> box,
		const QString &address);
	void coverColorPressed(
		not_null<Ui::GenericBox*> box,
		const QString &address);
	void coverCardPressed(
		not_null<Ui::GenericBox*> box,
		const QString &address);
	void closeCoverThen(not_null<Ui::GenericBox*> box, Fn<void()> action);
	void showCardMenu(const QString &address, QPoint point);
	void prepareColor(int index);
	void cardAction(const QString &address, bool add);
	void colorAction(const QString &address, bool apply);
	void addToCard(const CollectibleRecord &record, bool color = false);
	void removeFromCard(const QString &address);
	void applyFailure(const OwnCardFailure &failure);
	void showRefusal(OwnCardChange change);
	void flyToCard(const QString &address, QImage frame);
	void flyToSquare(
		const QString &address,
		Data::StoryArea area,
		QImage frame);
	void startFlight(const QString &address, CardFlightArgs &&args);
	void cancelFlight(const QString &address);
	void dropFlight(const QString &address);
	void holdSticker(const QString &address);
	void releaseSticker(const QString &address);
	void landSticker(
		const QString &address,
		float64 fromScale = 1.,
		QImage frame = QImage(),
		TravelArtwork artwork = TravelArtwork());
	void setReturning(const QString &address, bool returning);
	void setLifted(const QString &address);
	void setGripped(const QString &address);
	void setLeaving(const QString &address, bool leaving);
	void refreshSquare(const QString &address);
	[[nodiscard]] std::optional<FlightPose> dragSource(
		const QString &address) const;
	[[nodiscard]] bool dragFollowing() const;
	void pressAt(const QString &address, QPoint global);
	void pointerMoved(QPoint global);
	void startDrag(QPoint global);
	void endGesture(std::optional<QPoint> release);
	void clearPress();
	[[nodiscard]] bool touchOwned() const;
	[[nodiscard]] int touchContacts() const;
	[[nodiscard]] bool takeTouch(not_null<QTouchEvent*> e);
	void handleTouch(not_null<QTouchEvent*> e);
	void holdTouch();
	void moveHeldTouch(QPoint global);
	void finishHeldTouch(QPoint global);
	void finishWaitingTouch(not_null<QTouchEvent*> e);
	void interruptTouch();
	void watchTouchWindow();
	void observeTouchFrame(not_null<QEvent*> e);
	void dropDrag(const FlightPose &released);
	[[nodiscard]] bool addDragged(
		std::shared_ptr<const Data::StarGift> gift,
		QPointF local);
	void landDrag(uint64 generation, float64 releasedSide);
	void returnDrag();
	void restoreSquare(uint64 generation);
	void vanishDrag();
	void dragFinished(uint64 generation);
	void finishTravel();
	[[nodiscard]] OwnCard &ownCard() const;
	[[nodiscard]] OwnCardState stackedCard() const;
	[[nodiscard]] bool colorSource(const QString &address) const;
	[[nodiscard]] QImage squareFrame(const QString &address) const;
	[[nodiscard]] std::optional<Data::StoryArea> shownArea(
		const QString &address) const;
	[[nodiscard]] std::optional<FlightPose> shownPose(
		const QString &address) const;
	[[nodiscard]] std::optional<FlightPose> squarePose(
		const QString &address) const;
	[[nodiscard]] std::optional<FlightPose> cardPose(
		const Data::StoryArea &area) const;
	[[nodiscard]] auto createGiftButton(const QString &address)
	-> std::unique_ptr<Info::PeerGifts::GiftButton>;
	[[nodiscard]] std::unique_ptr<Ui::AbstractButton> createThumbButton(
		const QString &address);
	void paintThumb(not_null<QWidget*> thumb, const QString &address);

	const not_null<Ui::ScrollArea*> _scroll;
	const std::shared_ptr<Main::SessionShow> _show;
	const std::shared_ptr<CollectibleMedia> _media;
	const CardHost _host;
	Info::PeerGifts::Delegate _delegate;
	const QSize _singleMin;
	CardColorToast _colorToast;
	std::vector<CollectibleRecord> _records;
	base::flat_set<QString> _seen;
	base::flat_map<QString, Square> _squares;
	base::flat_set<QString> _onCard;
	base::unique_qptr<Ui::PopupMenu> _menu;
	base::flat_map<QString, Flight> _flights;
	base::flat_set<QString> _held;
	base::flat_set<QString> _returning;
	Drag _drag;
	QString _lifted;
	QString _gripped;
	QString _leaving;
	uint64 _lastFlightId = 0;
	uint64 _lastDragGeneration = 0;
	Press _press;
	QSize _single;
	int _perRow = 0;
	int _left = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;
	int _squaresFrom = -1;
	int _squaresTill = -1;
	int _mediaFrom = -1;
	int _mediaTill = -1;
	bool _over = false;
	bool _scrollPressing = false;
	std::vector<std::pair<TouchScreenDevice, int>> _touchContacts;
	std::optional<Touch> _touch;
	base::Timer _touchTimer;
	base::unique_qptr<QObject> _touchFilter;
	std::unique_ptr<CollectiblesHint> _hint;

};

class CollectibleActionButton final : public Ui::RippleButton {
public:
	CollectibleActionButton(
		QWidget *parent,
		rpl::producer<QString> text,
		const style::icon &icon);

protected:
	void paintEvent(QPaintEvent *e) override;
	QImage prepareRippleMask() const override;

private:
	const style::icon &_icon;
	QString _text;

};

[[nodiscard]] QTouchEvent *TouchScreenEvent(not_null<QEvent*> e);
void SendTouchCancel(not_null<Ui::ScrollArea*> scroll, const TouchScreenDevice &device);

} // namespace Wallet::CollectiblesDetails
