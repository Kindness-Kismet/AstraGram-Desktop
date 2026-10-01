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

// 折叠时箭头朝右，展开后朝下。
class RecordGroupAction final : public Ui::Menu::Action {
public:
	using Action::Action;

	void setExpanded(bool expanded) {
		_expanded = expanded;
		update();
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		Action::paintEvent(e);
		auto p = QPainter(this);
		const auto &arrow = st().arrow;
		p.translate(
			width() - st().itemRightSkip - arrow.width() / 2.,
			height() / 2.);
		if (_expanded) {
			p.rotate(90.);
		}
		arrow.paintInCenter(p, QRect(
			-arrow.width() / 2,
			-arrow.height() / 2,
			arrow.width(),
			arrow.height()));
	}

private:
	bool _expanded = false;

};

} // namespace

RecordMenuOptions recordMenuOptions(
		not_null<PeerData*> peer,
		Webrtc::RecordAvailability availability) {
	using Availability = Webrtc::RecordAvailability;
	if (!ExtrasSettings::getInstance().showMicrophoneButtonInMessageField()
		|| availability == Availability::None
		|| Media::Capture::instance()->started()
		|| Core::App().calls().currentCall()
		|| Core::App().calls().currentGroupCall()) {
		return {};
	}
	const auto permissions = readRecordPermissions();
	if (!permissions.microphone) {
		return {};
	}
	return {
		.voice = Data::CanSend(
			peer,
			ChatRestriction::SendVoiceMessages,
			false),
		.round = permissions.camera
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

void addRecordMenu(
		not_null<Ui::DropdownMenu*> menu,
		RecordMenuOptions options,
		Fn<void(bool round)> record) {
	if (!options.voice && !options.round) {
		return;
	}
	const auto inner = menu->menu();
	if (!inner->empty()) {
		menu->addSeparator();
	}
	struct State {
		RecordGroupAction *group = nullptr;
		int shown = 0;
	};
	const auto state = inner->lifetime().make_state<State>();
	// 展开项位于菜单末尾，删除时不影响其他项的序号。
	const auto collapse = [=] {
		const auto position = state->group->index() + 1;
		while (state->shown > 0) {
			inner->removeAction(position + --state->shown);
		}
		state->group->setExpanded(false);
	};
	const auto expand = [=] {
		auto position = state->group->index() + 1;
		const auto add = [&](
				const QString &text,
				const style::icon *icon,
				bool round) {
			inner->insertAction(
				position++,
				base::make_unique_q<Ui::Menu::Action>(
					inner,
					inner->st(),
					Ui::Menu::CreateAction(inner, text, [=] {
						record(round);
					}),
					icon,
					icon));
			++state->shown;
		};
		if (options.voice) {
			add(
				tr::extras_RecordVoiceMessage(tr::now),
				&st::messageFieldVoiceIcon,
				false);
		}
		if (options.round) {
			add(
				tr::extras_RecordVideoMessage(tr::now),
				&st::extrasRecordRoundIcon,
				true);
		}
		state->group->setExpanded(true);
	};
	auto group = base::make_unique_q<RecordGroupAction>(
		inner,
		inner->st(),
		Ui::Menu::CreateAction(inner, tr::extras_RecordMessage(tr::now), [=] {
			if (state->shown) {
				collapse();
			} else {
				expand();
			}
		}),
		&st::extrasRecordMessageIcon,
		&st::extrasRecordMessageIcon);
	group->setPreventClose(true);
	group->setMinWidth(group->minWidth()
		+ inner->st().itemRightSkip
		+ inner->st().arrow.width());
	state->group = group.get();
	menu->addAction(std::move(group));
	// 每次打开都从折叠状态开始，避免误触录制。
	menu->setShowStartCallback([=] {
		if (state->shown) {
			collapse();
		}
	});
}

}
