#include "extras/ui/controls/attach_menu.h"

#include "extras/extras_settings.h"
#include "base/event_filter.h"
#include "calls/calls_instance.h"
#include "core/application.h"
#include "data/data_chat_participant_status.h"
#include "lang/lang_keys.h"
#include "media/audio/media_audio_capture.h"
#include "platform/platform_specific.h"
#include "ui/widgets/dropdown_menu.h"
#include "ui/widgets/menu/menu_action.h"
#include "ui/widgets/menu/menu_common.h"
#include "webrtc/webrtc_device_common.h"
#include "styles/style_extras_icons.h"

#ifdef Q_OS_WIN
#include "extras/utils/windows_utils.h"
#endif

namespace ExtrasUi {
namespace {

struct RecordPermissions {
	bool microphone = false;
	bool camera = false;

	friend inline bool operator==(
		const RecordPermissions &,
		const RecordPermissions &) = default;
};

// 未询问过的状态视为可用，录制时由系统弹出授权。
[[nodiscard]] RecordPermissions readRecordPermissions() {
#ifdef Q_OS_WIN
	return {
		.microphone = !isCapabilityDenied(u"microphone"_q),
		.camera = !isCapabilityDenied(u"webcam"_q),
	};
#else
	using Platform::PermissionStatus;
	using Platform::PermissionType;
	const auto allowed = [](PermissionType type) {
		return Platform::GetPermissionStatus(type)
			!= PermissionStatus::Denied;
	};
	return {
		.microphone = allowed(PermissionType::Microphone),
		.camera = allowed(PermissionType::Camera),
	};
#endif
}

struct GroupItem {
	QString text;
	const style::icon *icon = nullptr;
	Fn<void()> callback;
	bool enabled = true;
};

[[nodiscard]] not_null<QAction*> createAction(
		not_null<Ui::Menu::Menu*> menu,
		const QString &text,
		Fn<void()> callback,
		bool enabled) {
	const auto result = Ui::Menu::CreateAction(
		menu,
		text,
		std::move(callback));
	result->setEnabled(enabled);
	return result;
}

// 禁用时图标随文字置灰；分组标题右侧的箭头折叠时朝右，展开后朝下。
class MenuItem final : public Ui::Menu::Action {
public:
	MenuItem(
		not_null<Ui::Menu::Menu*> menu,
		const QString &text,
		Fn<void()> callback,
		const style::icon *icon,
		bool enabled,
		bool group)
	: Action(
		menu,
		menu->st(),
		createAction(menu, text, std::move(callback), enabled),
		enabled ? icon : nullptr,
		enabled ? icon : nullptr)
	, _disabledIcon(enabled ? nullptr : icon)
	, _group(group) {
		if (!_group) {
			return;
		}
		setPreventClose(true);
		setMinWidth(minWidth() + st().itemRightSkip + st().arrow.width());
	}

	void setExpanded(bool expanded) {
		_expanded = expanded;
		update();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		Action::paintEvent(e);
		auto p = QPainter(this);
		const auto disabled = st().itemFgDisabled->c;
		if (_disabledIcon) {
			_disabledIcon->paint(p, st().itemIconPosition, width(), disabled);
		}
		if (!_group) {
			return;
		}
		const auto &arrow = st().arrow;
		p.translate(
			width() - st().itemRightSkip - arrow.width() / 2.,
			height() / 2.);
		if (_expanded) {
			p.rotate(90.);
		}
		const auto rect = QRect(
			-arrow.width() / 2,
			-arrow.height() / 2,
			arrow.width(),
			arrow.height());
		if (isEnabled()) {
			arrow.paintInCenter(p, rect);
		} else {
			arrow.paintInCenter(p, rect, disabled);
		}
	}

private:
	const style::icon *_disabledIcon = nullptr;
	bool _group = false;
	bool _expanded = false;

};

// 在菜单末尾追加可展开的分组，展开项紧跟分组标题；全部子项禁用时标题置灰。
void addGroup(
		not_null<Ui::DropdownMenu*> menu,
		const QString &title,
		const style::icon *icon,
		std::vector<GroupItem> items) {
	Expects(!items.empty());

	const auto inner = menu->menu();
	const auto enabled = ranges::any_of(items, &GroupItem::enabled);
	struct State {
		std::vector<GroupItem> items;
		MenuItem *group = nullptr;
		int shown = 0;
	};
	const auto state = inner->lifetime().make_state<State>(State{
		.items = std::move(items),
	});
	const auto collapse = [=] {
		const auto position = state->group->index() + 1;
		while (state->shown > 0) {
			inner->removeAction(position + --state->shown);
		}
		state->group->setExpanded(false);
	};
	const auto expand = [=] {
		auto position = state->group->index() + 1;
		for (const auto &item : state->items) {
			inner->insertAction(position++, base::make_unique_q<MenuItem>(
				inner,
				item.text,
				item.callback,
				item.icon,
				item.enabled,
				false));
			++state->shown;
		}
		state->group->setExpanded(true);
	};
	auto group = base::make_unique_q<MenuItem>(inner, title, [=] {
		if (state->shown) {
			collapse();
		} else {
			expand();
		}
	}, icon, enabled, true);
	state->group = group.get();
	menu->addAction(std::move(group));
	// 菜单隐藏后收起，每次打开都从折叠状态开始，避免误触。
	base::install_event_filter(menu, menu, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Hide && state->shown) {
			collapse();
		}
		return base::EventFilterResult::Continue;
	});
}

} // namespace

RecordMenuOptions recordMenuOptions(
		not_null<PeerData*> peer,
		Webrtc::RecordAvailability availability) {
	using Availability = Webrtc::RecordAvailability;
	if (!ExtrasSettings::getInstance().showRecordMessageInAttachMenu()) {
		return {};
	}
	const auto busy = Media::Capture::instance()->started()
		|| Core::App().calls().currentCall()
		|| Core::App().calls().currentGroupCall();
	const auto permissions = readRecordPermissions();
	const auto audio = !busy
		&& permissions.microphone
		&& (availability != Availability::None);
	return {
		.shown = true,
		.voice = audio
			&& Data::CanSend(peer, ChatRestriction::SendVoiceMessages, false),
		.round = audio
			&& permissions.camera
			&& (availability == Availability::VideoAndAudio)
			&& Data::CanSend(peer, ChatRestriction::SendVideoMessages, false),
	};
}

rpl::producer<> recordMenuChanges() {
	auto permissions = rpl::producer<>([](auto consumer) {
		auto result = rpl::lifetime();
		const auto last = result.make_state<RecordPermissions>(
			readRecordPermissions());
		Core::App().appDeactivatedValue(
		) | rpl::filter(
			!rpl::mappers::_1
		) | rpl::on_next([=] {
			const auto now = readRecordPermissions();
			if (*last == now) {
				return;
			}
			*last = now;
			consumer.put_next({});
		}, result);
		return result;
	});
	return rpl::merge(
		std::move(permissions),
		Media::Capture::instance()->startedChanges() | rpl::to_empty,
		Core::App().calls().currentCallValue() | rpl::to_empty,
		Core::App().calls().currentGroupCallValue() | rpl::to_empty);
}

void setupAttachMenu(
		not_null<QWidget*> button,
		not_null<Ui::DropdownMenu*> menu) {
	menu->setObjectName(u"compose.attachMenu"_q);
	menu->setOrigin(Ui::PanelAnimation::Origin::BottomLeft);
	if (ExtrasSettings::getInstance().showAttachPopup()) {
		base::install_event_filter(menu, button, [=](not_null<QEvent*> event) {
			if (event->type() == QEvent::Enter) {
				menu->otherEnter();
			} else if (event->type() == QEvent::Leave) {
				menu->otherLeave();
			}
			return base::EventFilterResult::Continue;
		});
		return;
	}
	base::install_event_filter(menu, button, [=](not_null<QEvent*> event) {
		if (event->type() != QEvent::MouseButtonRelease) {
			return base::EventFilterResult::Continue;
		}
		const auto mouse = static_cast<QMouseEvent*>(event.get());
		if (mouse->button() != Qt::LeftButton
			|| !button->rect().contains(mouse->pos())) {
			return base::EventFilterResult::Continue;
		}
		if (menu->isHidden()) {
			menu->showAnimated();
		} else {
			menu->hideAnimated();
		}
		return base::EventFilterResult::Continue;
	});
}

rpl::producer<> attachMenuChanges() {
	const auto &settings = ExtrasSettings::getInstance();
	return rpl::merge(
		settings.showPhotoInAttachMenuChanges() | rpl::to_empty,
		settings.showFileInAttachMenuChanges() | rpl::to_empty,
		settings.showPollInAttachMenuChanges() | rpl::to_empty,
		settings.showTodoListInAttachMenuChanges() | rpl::to_empty,
		settings.showArticleInAttachMenuChanges() | rpl::to_empty,
		settings.showLocationInAttachMenuChanges() | rpl::to_empty,
		settings.showMusicInAttachMenuChanges() | rpl::to_empty,
		settings.showRecordMessageInAttachMenuChanges() | rpl::to_empty);
}

void addRecordMenu(
		not_null<Ui::DropdownMenu*> menu,
		RecordMenuOptions options,
		Fn<void(bool round)> record) {
	if (!options.shown) {
		return;
	}
	if (!menu->menu()->empty()) {
		menu->addSeparator();
	}
	addGroup(menu, tr::extras_RecordMessage(tr::now), &st::extrasRecordMessageIcon, {
		{
			.text = tr::extras_RecordVoiceMessage(tr::now),
			.icon = &st::messageFieldVoiceIcon,
			.callback = [=] { record(false); },
			.enabled = options.voice,
		},
		{
			.text = tr::extras_RecordVideoMessage(tr::now),
			.icon = &st::extrasRecordRoundIcon,
			.callback = [=] { record(true); },
			.enabled = options.round,
		},
	});
}

}
