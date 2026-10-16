/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_members.h"

#include <rpl/combine.h>
#include "info/profile/info_profile_widget.h"
#include "info/profile/info_profile_values.h"
#include "info/profile/info_profile_members_controllers.h"
#include "info/members/info_members_widget.h"
#include "info/info_content_widget.h"
#include "info/info_controller.h"
#include "info/info_memento.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/padding_wrap.h"
#include "ui/search_field_controller.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "ui/boxes/confirm_box.h"
#include "boxes/peers/add_participants_box.h"
#include "window/window_session_controller.h"
#include "data/data_channel.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "styles/style_boxes.h"
#include "styles/style_info.h"

namespace Info {
namespace Profile {
namespace {

constexpr auto kEnableSearchMembersAfterCount = 8;

} // namespace

Members::Members(
	QWidget *parent,
	not_null<Controller*> controller,
	bool skipHeader)
: RpWidget(parent)
, _show(controller->uiShow())
, _controller(controller)
, _skipHeader(skipHeader)
, _peer(_controller->key().peer())
, _listController(CreateMembersController(controller, _peer)) {
	_listController->setStoriesShown(true);
	setupHeader();
	setupList();
	setContent(_list.data());
	_listController->setDelegate(static_cast<PeerListDelegate*>(this));

	_controller->searchFieldController()->queryValue(
	) | rpl::on_next([this](QString &&query) {
		peerListScrollToTop();
		content()->searchQueryChanged(std::move(query));
	}, lifetime());
	MembersCountValue(
		_peer
	) | rpl::on_next([this](int count) {
		const auto enabled = (count >= kEnableSearchMembersAfterCount);
		_controller->setSearchEnabledByContent(enabled);
	}, lifetime());
}

int Members::desiredHeight() const {
	auto desired = _header ? _header->height() : 0;
	desired += _list->fullRowsCount() * st::infoMembersList.item.height;
	return std::max(height(), desired);
}

rpl::producer<int> Members::onlineCountValue() const {
	return _listController->onlineCountValue();
}

rpl::producer<int> Members::fullCountValue() const {
	return _listController->fullCountValue();
}

rpl::producer<Ui::ScrollToRequest> Members::scrollToRequests() const {
	return _scrollToRequests.events();
}

void Members::applySearchQuery(const QString &query) {
	peerListScrollToTop();
	content()->searchQueryChanged(query);
}

void Members::setGroupByRole(bool grouped) {
	_listController->setGroupByRole(grouped);
}

rpl::producer<bool> Members::groupByRoleValue() const {
	return _listController->groupByRoleValue();
}

rpl::producer<bool> Members::groupByRoleAvailableValue() const {
	return _listController->groupByRoleAvailableValue();
}

rpl::producer<bool> Members::rowsVisibleValue() const {
	return _rowsVisible.value();
}

std::unique_ptr<MembersState> Members::saveState() {
	auto result = std::make_unique<MembersState>();
	result->list = _listController->saveState();
	//if (_searchShown) {
	//	result->search = _searchField->getLastText();
	//}
	return result;
}

void Members::restoreState(std::unique_ptr<MembersState> state) {
	if (!state) {
		return;
	}
	_listController->restoreState(std::move(state->list));
	//updateSearchEnabledByContent();
	//if (!_controller->searchFieldController()->query().isEmpty()) {
	//	if (!_searchShown) {
	//		toggleSearch(anim::type::instant);
	//	}
	//} else if (_searchShown) {
	//	toggleSearch(anim::type::instant);
	//}
}

void Members::setupHeader() {
	if (_skipHeader
		|| (_controller->section().type() == Section::Type::Members)) {
		return;
	}
	_header = object_ptr<Ui::FixedHeightWidget>(
		this,
		st::infoMembersHeader);
	auto parent = _header.data();

	// 标题只展示人数，进入完整列表由右侧搜索按钮负责。
	_title = setupTitle();
	_addMember = Ui::CreateChild<Ui::IconButton>(
		parent,
		st::infoMembersHeaderAdd);
	_addMember->setObjectName(u"profile.members.add"_q);
	_addMember->setAccessibleName(tr::lng_channel_add_members(tr::now));
	//_searchField = _controller->searchFieldController()->createField(
	//	parent,
	//	st::infoMembersSearchField);
	_search = Ui::CreateChild<Ui::IconButton>(
		parent,
		st::infoMembersSearch);
	_search->setObjectName(u"profile.members.search"_q);
	_search->setAccessibleName(tr::lng_participant_filter(tr::now));
	//_cancelSearch = Ui::CreateChild<Ui::CrossButton>(
	//	parent,
	//	st::infoMembersCancelSearch);

	setupButtons();

	//_controller->wrapValue(
	//) | rpl::on_next([this](Wrap wrap) {
	//	_wrap = wrap;
	//	updateSearchOverrides();
	//}, lifetime());
	widthValue(
	) | rpl::on_next([this](int width) {
		_header->resizeToWidth(width);
	}, _header->lifetime());
}

object_ptr<Ui::FlatLabel> Members::setupTitle() {
	auto visible = _peer->isMegagroup()
		? CanViewParticipantsValue(_peer->asMegagroup())
		: rpl::single(true);
	auto members = rpl::combine(
		tr::lng_profile_participants_section(),
		MembersCountValue(_peer)
	) | rpl::map([](const QString &title, int count) {
		return (count > 0)
			? (title + u" "_q + QChar(0x00B7) + u" "_q
				+ Lang::FormatCountDecimal(count))
			: title;
	});
	auto text = rpl::conditional(
		std::move(visible),
		std::move(members),
		tr::lng_channel_admins());
	auto result = object_ptr<Ui::FlatLabel>(
		_header.data(),
		std::move(text),
		st::infoProfileGroupTitle);
	result->setAttribute(Qt::WA_TransparentForMouseEvents);
	return result;
}

void Members::setupButtons() {
	using namespace rpl::mappers;

	//_searchField->hide();
	//_cancelSearch->setVisible(false);

	auto visible = (_peer->isMegagroup()
		? CanViewParticipantsValue(_peer->asMegagroup())
		: rpl::single(true)) | rpl::start_spawning(lifetime());

	auto addMemberShown = rpl::combine(
		CanAddMemberValue(_peer),
		rpl::duplicate(visible)
	) | rpl::map(_1 && _2) | rpl::start_spawning(lifetime());
	_addMember->showOn(rpl::duplicate(addMemberShown));
	_addMember->addClickHandler([this] { // TODO throttle(ripple duration)
		this->addMember();
	});

	// 搜索按钮是进入完整成员列表的唯一入口，人数少时也保留。
	auto searchShown = rpl::duplicate(
		visible
	) | rpl::distinct_until_changed() | rpl::start_spawning(lifetime());
	_search->showOn(rpl::duplicate(searchShown));
	_search->addClickHandler([this] { // TODO throttle(ripple duration)
		this->showMembersWithSearch(true);
	});
	//_cancelSearch->addClickHandler([this] {
	//	this->cancelSearch();
	//});

	rpl::combine(
		std::move(addMemberShown),
		std::move(searchShown),
		std::move(visible)
	) | rpl::on_next([this] {
		updateHeaderControlsGeometry(width());
	}, lifetime());
}

void Members::setupList() {
	auto topSkip = _header ? _header->height() : 0;
	_listController->setStyleOverrides(&st::infoMembersList);
	_listController->setStoriesShown(true);
	_list = object_ptr<ListWidget>(
		this,
		_listController.get());
	_list->setRowBackgroundRounding(
		st::infoMembersRowMargins,
		st::infoMembersRowRadius);
	_list->scrollToRequests(
	) | rpl::on_next([this](Ui::ScrollToRequest request) {
		auto addmin = (request.ymin < 0 || !_header)
			? 0
			: _header->height();
		auto addmax = (request.ymax < 0 || !_header)
			? 0
			: _header->height();
		_scrollToRequests.fire({
			request.ymin + addmin,
			request.ymax + addmax });
	}, _list->lifetime());
	widthValue(
	) | rpl::on_next([this](int newWidth) {
		_list->resizeToWidth(newWidth);
	}, _list->lifetime());
	_list->heightValue(
	) | rpl::on_next([=](int listHeight) {
		auto newHeight = (listHeight > st::membersMarginBottom)
			? (topSkip
				+ listHeight
				+ st::membersMarginBottom)
			: 0;
		resize(width(), newHeight);
	}, _list->lifetime());
	_list->moveToLeft(0, topSkip);
}

int Members::resizeGetHeight(int newWidth) {
	if (_header) {
		updateHeaderControlsGeometry(newWidth);
	}
	return heightNoMargins();
}

//void Members::updateSearchEnabledByContent() {
//	_controller->setSearchEnabledByContent(
//		peerListFullRowsCount() >= kEnableSearchMembersAfterCount);
//}

void Members::updateHeaderControlsGeometry(int newWidth) {
	const auto height = st::infoMembersHeader;
	auto availableWidth = newWidth - st::infoMembersButtonPosition.x();
	const auto place = [&](not_null<Ui::IconButton*> button) {
		const auto top = (height - button->height()) / 2;
		button->moveToLeft(availableWidth - button->width(), top, newWidth);
		if (!button->isHidden()) {
			availableWidth -= button->width();
		}
	};
	place(_addMember);
	place(_search);

	const auto left = st::infoProfileGroupTitleLeft;
	_title->resizeToWidth(std::max(0, availableWidth - left));
	_title->moveToLeft(left, (height - _title->height()) / 2, newWidth);
}

void Members::addMember() {
	if (const auto chat = _peer->asChat()) {
		AddParticipantsBoxController::Start(_controller, chat);
	} else if (const auto channel = _peer->asChannel()) {
		const auto state = _listController->saveState();
		const auto users = ranges::views::all(
			state->list
		) | ranges::views::transform([](not_null<PeerData*> peer) {
			return peer->asUser();
		}) | ranges::to_vector;
		AddParticipantsBoxController::Start(
			_controller,
			channel,
			{ users.begin(), users.end() });
	}
}

void Members::showMembersWithSearch(bool withSearch) {
	//if (!_searchShown) {
	//	toggleSearch();
	//}
	auto contentMemento = std::make_shared<Info::Members::Memento>(
		_controller);
	contentMemento->setState(saveState());
	contentMemento->setSearchStartsFocused(withSearch);
	auto mementoStack = std::vector<std::shared_ptr<ContentMemento>>();
	mementoStack.push_back(std::move(contentMemento));
	_controller->showSection(
		std::make_shared<Info::Memento>(std::move(mementoStack)));
}

//void Members::toggleSearch(anim::type animated) {
//	_searchShown = !_searchShown;
//	_cancelSearch->toggle(_searchShown, animated);
//	if (animated == anim::type::normal) {
//		_searchShownAnimation.start(
//			[this] { searchAnimationCallback(); },
//			_searchShown ? 0. : 1.,
//			_searchShown ? 1. : 0.,
//			st::slideWrapDuration);
//	} else {
//		_searchShownAnimation.finish();
//		searchAnimationCallback();
//	}
//	_search->setDisabled(_searchShown);
//	if (_searchShown) {
//		_searchField->show();
//		_searchField->setFocus();
//	} else {
//		setFocus();
//	}
//}
//
//void Members::searchAnimationCallback() {
//	if (!_searchShownAnimation.animating()) {
//		_searchField->setVisible(_searchShown);
//		updateSearchOverrides();
//		_search->setPointerCursor(!_searchShown);
//	}
//	updateHeaderControlsGeometry(width());
//}
//
//void Members::updateSearchOverrides() {
//	auto iconOverride = !_searchShown
//		? nullptr
//		: (_wrap == Wrap::Layer)
//		? &st::infoMembersSearchActiveLayer
//		: &st::infoMembersSearchActive;
//	_search->setIconOverride(iconOverride, iconOverride);
//}
//
//void Members::cancelSearch() {
//	if (_searchShown) {
//		if (!_searchField->getLastText().isEmpty()) {
//			_searchField->setText(QString());
//			_searchField->setFocus();
//		} else {
//			toggleSearch();
//		}
//	}
//}

void Members::visibleTopBottomUpdated(
		int visibleTop,
		int visibleBottom) {
	setChildVisibleTopBottom(_list, visibleTop, visibleBottom);
	const auto top = _list->y();
	_rowsVisible = (visibleBottom > top)
		&& (visibleTop < top + _list->height());
}

void Members::peerListSetTitle(rpl::producer<QString> title) {
}

void Members::peerListSetAdditionalTitle(rpl::producer<QString> title) {
}

bool Members::peerListIsRowChecked(not_null<PeerListRow*> row) {
	return false;
}

int Members::peerListSelectedRowsCount() {
	return 0;
}

void Members::peerListScrollToTop() {
	_scrollToRequests.fire({ -1, -1 });
}

void Members::peerListAddSelectedPeerInBunch(not_null<PeerData*> peer) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void Members::peerListAddSelectedRowInBunch(not_null<PeerListRow*> row) {
	Unexpected("Item selection in Info::Profile::Members.");
}

void Members::peerListFinishSelectedRowsBunch() {
}

std::shared_ptr<Main::SessionShow> Members::peerListUiShow() {
	return _show;
}

void Members::peerListSetDescription(
		object_ptr<Ui::FlatLabel> description) {
	description.destroy();
}

} // namespace Profile
} // namespace Info
