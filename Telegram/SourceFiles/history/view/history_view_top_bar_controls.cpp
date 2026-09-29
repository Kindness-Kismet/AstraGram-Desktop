#include "history/view/history_view_top_bar_widget.h"

#include "ayu/ayu_settings.h"
#include "lang/lang_keys.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/tooltip.h"
#include "ui/ui_utility.h"
#include "window/window_session_controller.h"
#include "styles/style_chat.h"
#include "styles/style_dialogs.h"
#include "styles/style_info.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

namespace HistoryView {
namespace {

// 提示文字在显示时读取，切换语言后自动更新。
template <typename Phrase>
void installTooltip(not_null<Ui::RpWidget*> widget, Phrase phrase) {
	Ui::InstallTooltip(widget, [=] { return phrase(tr::now); });
}

} // namespace

void TopBarWidget::setupTooltips() {
	installTooltip(_back.data(), tr::lng_go_back);
	installTooltip(_cancelChoose.data(), tr::lng_cancel);
	installTooltip(_call.data(), tr::lng_profile_action_short_call);
	installTooltip(_videoCall.data(), tr::lng_call_start_video);
	installTooltip(_groupCall.data(), tr::lng_group_call_title);
	installTooltip(_search.data(), tr::lng_shortcuts_search);
	installTooltip(_infoToggle.data(), tr::lng_settings_section_info);
	installTooltip(_menuToggle.data(), tr::lng_chat_menu);
	installTooltip(_recentActions.data(), tr::lng_manage_peer_recent_actions);
	installTooltip(_admins.data(), tr::lng_channel_admins);
}

void TopBarWidget::setupSelection() {
	const auto setup = [&](
			not_null<Ui::IconButton*> button,
			const QString &name,
			tr::phrase<> phrase,
			rpl::event_stream<> &requests) {
		button->setClickedCallback([&requests] { requests.fire({}); });
		button->setAccessibleName(phrase(tr::now));
		button->setObjectName(name);
		installTooltip(button, phrase);
	};
	setup(_clear.data(), u"selection.clear"_q, tr::lng_selected_clear, _clearSelection);
	setup(_forward.data(), u"selection.forward"_q, tr::lng_selected_forward, _forwardSelection);
	setup(_noQuote.data(), u"selection.noQuote"_q, tr::ayu_SelectedForwardNoQuote, _noQuoteSelection);
	setup(_sendNow.data(), u"selection.sendNow"_q, tr::lng_selected_send_now, _sendNowSelection);
	setup(_delete.data(), u"selection.delete"_q, tr::lng_selected_delete, _deleteSelection);
	setup(_messageShot.data(), u"selection.messageShot"_q, tr::ayu_SelectionMessageShot, _messageShotSelection);
	setup(_selectBetween.data(), u"selection.between"_q, tr::ayu_SelectBetweenText, _selectBetweenSelection);

	_selectionCount->setAttribute(Qt::WA_TransparentForMouseEvents);
	_selectionCount->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(_selectionCount.data());
		const auto &font = st::semiboldFont;
		const auto text = tr::lng_media_selected_message(
			tr::now,
			lt_count,
			_selectionCountShown);
		p.setFont(font);
		p.setPen(st::dialogsNameFg);
		p.drawText(
			_selectionCount->rect(),
			font->elided(text, _selectionCount->width()),
			style::al_left);
	}, _selectionCount->lifetime());
}

// 取消按钮和数量靠左，操作图标从右向左排列。
void TopBarWidget::updateSelectionGeometry(int selectedButtonsTop) {
	const auto top = selectedButtonsTop + (height() - _clear->height()) / 2;
	_clear->moveToLeft(0, top);
	const auto buttons = std::array{
		_forward.data(),
		_noQuote.data(),
		_sendNow.data(),
		_delete.data(),
		_messageShot.data(),
		_selectBetween.data(),
	};
	auto right = width() - st::topBarActionSkip;
	for (const auto button : buttons | ranges::views::reverse) {
		if (button->isHidden()) {
			continue;
		}
		right -= button->width();
		button->moveToLeft(right, top);
	}
	const auto left = _clear->width();
	_selectionCount->setGeometry(
		left,
		top,
		std::max(right - left, 0),
		_clear->height());
}

int TopBarWidget::countSelectedButtonsTop(float64 selectedShown) {
	return (1. - selectedShown) * (-st::topBarHeight);
}

bool TopBarWidget::showSelectedState() const {
	const auto &settings = AyuSettings::getInstance();

	return (_selectedCount > 0)
		&& (_canDelete || _canForward || _canSendNow
			|| _canSelectBetween || settings.showMessageShot());
}

void TopBarWidget::showSelected(SelectedState state) {
	const auto &settings = AyuSettings::getInstance();

	auto canDelete = (state.count > 0 && state.count == state.canDeleteCount);
	auto canForward = (state.count > 0 && state.count == state.canForwardCount);
	auto canSendNow = (state.count > 0 && state.count == state.canSendNowCount);
	const auto hideNoQuote = state.hideNoQuote;
	auto canSelectBetween = state.canSelectBetween;
	auto count = (!canDelete && !canForward && !canSendNow
		&& !canSelectBetween && !settings.showMessageShot()) ? 0 : state.count;
	if (_selectedCount == count
		&& _canDelete == canDelete
		&& _canForward == canForward
		&& _canSendNow == canSendNow
		&& _hideNoQuote == hideNoQuote
		&& _canSelectBetween == canSelectBetween) {
		return;
	}
	if (count == 0) {
		// Don't change the visible buttons if the selection is cancelled.
		canDelete = _canDelete;
		canForward = _canForward;
		canSendNow = _canSendNow;
		canSelectBetween = _canSelectBetween;
	}

	const auto wasSelectedState = showSelectedState();
	const auto visibilityChanged = (_canDelete != canDelete)
		|| (_canForward != canForward)
		|| (_canSendNow != canSendNow)
		|| (_hideNoQuote != hideNoQuote)
		|| (_canSelectBetween != canSelectBetween);
	_selectedCount = count;
	_canDelete = canDelete;
	_canForward = canForward;
	_canSendNow = canSendNow;
	_hideNoQuote = hideNoQuote;
	_canSelectBetween = canSelectBetween;
	const auto nowSelectedState = showSelectedState();
	if (nowSelectedState) {
		// 取消选择时保留原数量，直到选择栏滑出。
		_selectionCountShown = _selectedCount;
		_selectionCount->update();
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
	_selectionCount->setVisible(visible);
	_delete->setVisible(_canDelete && visible);
	_messageShot->setVisible(settings.showMessageShot() && visible);
	_forward->setVisible(_canForward && visible);
	_noQuote->setVisible(_canForward && !_canSendNow && !_hideNoQuote && visible);
	_sendNow->setVisible(_canSendNow && visible);
	_selectBetween->setVisible(_canSelectBetween && visible);
}

void TopBarWidget::refreshLang() {
	InvokeQueued(this, [this] {
		updateControlsGeometry();
		_selectionCount->update();
	});
}

void TopBarWidget::resizeEvent(QResizeEvent *e) {
	updateSearchVisibility();
	updateControlsGeometry();
}

} // namespace HistoryView
