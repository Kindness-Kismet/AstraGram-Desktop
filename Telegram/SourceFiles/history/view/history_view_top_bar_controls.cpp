#include "history/view/history_view_top_bar_widget.h"

#include "ayu/ayu_settings.h"
#include "ui/widgets/buttons.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace HistoryView {

void TopBarWidget::setupSelection() {
	_clear->setTextTransform(Ui::RoundButtonTextTransform::ToUpper);
	_forward->setTextTransform(Ui::RoundButtonTextTransform::ToUpper);
	_sendNow->setTextTransform(Ui::RoundButtonTextTransform::ToUpper);
	_delete->setTextTransform(Ui::RoundButtonTextTransform::ToUpper);
	_messageShot->setTextTransform(Ui::RoundButtonTextTransform::ToUpper);

	_forward->setClickedCallback([=] { _forwardSelection.fire({}); });
	_forward->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_noQuote->setClickedCallback([=] { _noQuoteSelection.fire({}); });
	_noQuote->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_sendNow->setClickedCallback([=] { _sendNowSelection.fire({}); });
	_sendNow->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_delete->setClickedCallback([=] { _deleteSelection.fire({}); });
	_delete->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_messageShot->setClickedCallback([=] { _messageShotSelection.fire({}); });
	_messageShot->setWidthChangedCallback([=] { updateControlsGeometry(); });
	_clear->setClickedCallback([=] { _clearSelection.fire({}); });
}

void TopBarWidget::updateSelectionGeometry(int selectedButtonsTop) {
	auto buttonsLeft = st::topBarActionSkip
		+ (_controller->adaptive().isOneColumn() ? 0 : st::lineWidth);
	auto buttonsWidth = (_forward->isHidden() ? 0 : _forward->contentWidth())
		+ (_noQuote->isHidden() ? 0 : _noQuote->contentWidth())
		+ (_sendNow->isHidden() ? 0 : _sendNow->contentWidth())
		+ (_delete->isHidden() ? 0 : _delete->contentWidth())
		+ (_messageShot->isHidden() ? 0 : _messageShot->contentWidth())
		+ _clear->width();
	buttonsWidth += buttonsLeft + st::topBarActionSkip * 4;

	auto widthLeft = std::min(
		width() - buttonsWidth,
		-2 * st::defaultActiveButton.width);
	auto buttonFullWidth = std::min(-(widthLeft / 2), 0);
	_forward->setFullWidth(buttonFullWidth);
	_noQuote->setFullWidth(buttonFullWidth);
	_sendNow->setFullWidth(buttonFullWidth);
	_delete->setFullWidth(buttonFullWidth);
	_messageShot->setFullWidth(buttonFullWidth);

	selectedButtonsTop += (height() - _forward->height()) / 2;

	_forward->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_forward->isHidden()) {
		buttonsLeft += _forward->width() + st::topBarActionSkip;
	}

	_noQuote->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_noQuote->isHidden()) {
		buttonsLeft += _noQuote->width() + st::topBarActionSkip;
	}

	_sendNow->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_sendNow->isHidden()) {
		buttonsLeft += _sendNow->width() + st::topBarActionSkip;
	}

	_delete->moveToLeft(buttonsLeft, selectedButtonsTop);
	if (!_delete->isHidden()) {
		buttonsLeft += _delete->width() + st::topBarActionSkip;
	}

	_messageShot->moveToLeft(buttonsLeft, selectedButtonsTop);
	{
		const auto large = st::topBarActionButtonLargeRadius;
		const auto &buttonSt = st::defaultActiveButton;
		const auto small = buttonSt.radius
			? buttonSt.radius
			: st::buttonRadius;
		const auto buttons = std::array{
			_forward.data(),
			_sendNow.data(),
			_delete.data(),
			_messageShot.data(),
		};
		auto first = (Ui::RoundButton*)(nullptr);
		auto last = (Ui::RoundButton*)(nullptr);
		for (const auto button : buttons) {
			if (!button->isHidden()) {
				if (!first) {
					first = button;
				}
				last = button;
			}
		}
		for (const auto button : buttons) {
			if (button->isHidden()) {
				continue;
			}
			const auto left = (button == first) ? large : small;
			const auto right = (button == last) ? large : small;
			button->setCornerRadii(left, right, left, right);
		}
	}
	_clear->moveToRight(st::topBarActionSkip, selectedButtonsTop);
}

int TopBarWidget::countSelectedButtonsTop(float64 selectedShown) {
	return (1. - selectedShown) * (-st::topBarHeight);
}

bool TopBarWidget::showSelectedState() const {
	const auto &settings = AyuSettings::getInstance();

	return (_selectedCount > 0)
		&& (_canDelete || _canForward || _canSendNow || settings.showMessageShot());
}

void TopBarWidget::showSelected(SelectedState state) {
	const auto &settings = AyuSettings::getInstance();

	auto canDelete = (state.count > 0 && state.count == state.canDeleteCount);
	auto canForward = (state.count > 0 && state.count == state.canForwardCount);
	auto canSendNow = (state.count > 0 && state.count == state.canSendNowCount);
	const auto hideNoQuote = state.hideNoQuote;
	auto count = (!canDelete && !canForward && !canSendNow && !settings.showMessageShot()) ? 0 : state.count;
	if (_selectedCount == count
		&& _canDelete == canDelete
		&& _canForward == canForward
		&& _canSendNow == canSendNow
		&& _hideNoQuote == hideNoQuote) {
		return;
	}
	if (count == 0) {
		// Don't change the visible buttons if the selection is cancelled.
		canDelete = _canDelete;
		canForward = _canForward;
		canSendNow = _canSendNow;
	}

	const auto wasSelectedState = showSelectedState();
	const auto visibilityChanged = (_canDelete != canDelete)
		|| (_canForward != canForward)
		|| (_canSendNow != canSendNow)
		|| (_hideNoQuote != hideNoQuote);
	_selectedCount = count;
	_canDelete = canDelete;
	_canForward = canForward;
	_canSendNow = canSendNow;
	_hideNoQuote = hideNoQuote;
	const auto nowSelectedState = showSelectedState();
	if (nowSelectedState) {
		_forward->setNumbersText(_selectedCount);
		_noQuote->setNumbersText(_selectedCount);
		_sendNow->setNumbersText(_selectedCount);
		_delete->setNumbersText(_selectedCount);
		_messageShot->setNumbersText(_selectedCount);
		if (!wasSelectedState) {
			_forward->finishNumbersAnimation();
			_noQuote->finishNumbersAnimation();
			_sendNow->finishNumbersAnimation();
			_delete->finishNumbersAnimation();
			_messageShot->finishNumbersAnimation();
		}
	}
	if (visibilityChanged
		|| (!wasSelectedState && nowSelectedState)) {
		updateControlsVisibility();
	}
	if (wasSelectedState != nowSelectedState && !_chooseForReportReason) {
		setCursor(nowSelectedState
			? style::cur_default
			: style::cur_pointer);

		updateMembersShowArea();
		toggleSelectedControls(nowSelectedState);
	} else {
		updateControlsGeometry();
	}
}

void TopBarWidget::toggleSelectedControls(bool shown) {
	_selectedShown.start(
		[this] { slideAnimationCallback(); },
		shown ? 0. : 1.,
		shown ? 1. : 0.,
		st::slideWrapDuration,
		anim::easeOutCirc);
}

bool TopBarWidget::showSelectedActions() const {
	return showSelectedState() && !_chooseForReportReason;
}

void TopBarWidget::slideAnimationCallback() {
	if (!_selectedShown.animating() && !_searchShown.animating()) {
		updateControlsVisibility();
	}
	updateControlsGeometry();
	update();
}

void TopBarWidget::finishAnimating() {
	_selectedShown.stop();
	updateControlsVisibility();
	update();
}

void TopBarWidget::setAnimatingMode(bool enabled) {
	if (_animatingMode != enabled) {
		_animatingMode = enabled;
		setAttribute(Qt::WA_OpaquePaintEvent, false);
		finishAnimating();
	} else if (!enabled) {
		finishAnimating();
	}
}

void TopBarWidget::showChooseMessagesForReport(Data::ReportInput input) {
	setChooseForReportReason(input);
}

void TopBarWidget::clearChooseMessagesForReport() {
	setChooseForReportReason(std::nullopt);
}

void TopBarWidget::setChooseForReportReason(
		std::optional<Data::ReportInput> reportInput) {
	if (_chooseForReportReason == reportInput) {
		return;
	}
	const auto wasNoReason = !_chooseForReportReason;
	_chooseForReportReason = reportInput;
	const auto nowNoReason = !_chooseForReportReason;
	updateControlsVisibility();
	updateControlsGeometry();
	update();
	if (wasNoReason != nowNoReason && showSelectedState()) {
		toggleSelectedControls(false);
		finishAnimating();
	}
	setCursor((nowNoReason && !showSelectedState())
		? style::cur_pointer
		: style::cur_default);
}

void TopBarWidget::updateSelectionVisibility() {
	const auto &settings = AyuSettings::getInstance();

	const auto visible = showSelectedState() || _selectedShown.animating();
	_clear->setVisible(visible);
	_delete->setVisible(_canDelete && visible);
	_messageShot->setVisible(settings.showMessageShot() && visible);
	_forward->setVisible(_canForward && visible);
	_noQuote->setVisible(_canForward && !_canSendNow && !_hideNoQuote && visible);
	_sendNow->setVisible(_canSendNow && visible);
}

void TopBarWidget::refreshLang() {
	InvokeQueued(this, [this] { updateControlsGeometry(); });
}

void TopBarWidget::resizeEvent(QResizeEvent *e) {
	updateSearchVisibility();
	updateControlsGeometry();
}

} // namespace HistoryView
