/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/cloud_password/settings_cloud_password_manage.h"
#include "settings/settings_card_layout.h"

#include "api/api_cloud_password.h"
#include "core/application.h"
#include "core/core_cloud_password.h"
#include "lang/lang_keys.h"
#include "settings/cloud_password/settings_cloud_password_common.h"
#include "settings/settings_common.h"
#include "settings/cloud_password/settings_cloud_password_email_confirm.h"
#include "settings/cloud_password/settings_cloud_password_email.h"
#include "settings/cloud_password/settings_cloud_password_hint.h"
#include "settings/cloud_password/settings_cloud_password_input.h"
#include "settings/cloud_password/settings_cloud_password_start.h"
#include "settings/cloud_password/settings_cloud_password_step.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

/*
Available actions for follow states.

From CreateEmail
From CreateEmailConfirm
From ChangeEmail
From ChangeEmailConfirm
From CheckPassword
From RecreateResetHint:
– Continue to ChangePassword.
– Continue to ChangeEmail.
– DisablePassword and Back to Settings.
– Back to Settings.
*/

namespace Settings {
namespace CloudPassword {

class Manage : public TypedAbstractStep<Manage> {
public:
	using TypedAbstractStep::TypedAbstractStep;

	[[nodiscard]] rpl::producer<QString> title() override;
	void setupContent();

	[[nodiscard]] base::weak_qptr<Ui::RpWidget> createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) override;

protected:
	[[nodiscard]] rpl::producer<std::vector<Type>> removeTypes() override;

private:

	QString _currentPassword;

	rpl::lifetime _requestLifetime;

	QPointer<Ui::RpWidget> _changePasswordButton;
	QPointer<Ui::RpWidget> _changeEmailButton;
	QPointer<Ui::RpWidget> _disableButton;

};

rpl::producer<QString> Manage::title() {
	return tr::lng_settings_cloud_password_start_title();
}

rpl::producer<std::vector<Type>> Manage::removeTypes() {
	return rpl::single(std::vector<Type>{
		CloudPasswordStartId(),
		CloudPasswordInputId(),
		CloudPasswordHintId(),
		CloudPasswordEmailId(),
		CloudPasswordEmailConfirmId(),
		CloudPasswordManageId(),
	});
}

void Manage::setupContent() {
	setFocusPolicy(Qt::StrongFocus);
	setFocus();

	const auto page = Ui::CreateChild<CardPage>(this);
	const auto content = page->content();
	auto currentStepData = stepData();
	_currentPassword = base::take(currentStepData.currentPassword);
	// If we go back from Password Manage to Privacy Settings
	// we should forget the current password.
	setStepData(std::move(currentStepData));

	const auto quit = [=] {
		setStepData(StepData());
		showBack();
	};

	SetupAutoCloseTimer(
		content->lifetime(),
		quit,
		[] { return Core::App().lastNonIdleTime(); });

	const auto state = cloudPassword().stateCurrent();
	if (!state) {
		quit();
		return;
	}
	cloudPassword().state(
	) | rpl::on_next([=](const Core::CloudPasswordState &state) {
		if (!_requestLifetime && !state.hasPassword) {
			quit();
		}
	}, lifetime());

	const auto showOtherAndRememberPassword = [=](Type type) {
		// Remember the current password to have ability
		// to return from Change Password to Password Manage.
		auto data = stepData();
		data.currentPassword = _currentPassword;
		setStepData(std::move(data));

		showOther(type);
	};

	AddDividerTextWithLottie(content, {
		.lottie = u"cloud_password/intro"_q,
		.showFinished = showFinishes(),
		.about = tr::lng_settings_cloud_password_manage_about1(
			TextWithEntities::Simple),
		.showDivider = false,
	});

	const auto actions = AddCardGroup(content);
	const auto changePasswordButton = AddButtonWithIcon(
		actions,
		tr::lng_settings_cloud_password_manage_password_change(),
		st::settingsButton,
		{ &st::menuIconPermissions });
	_changePasswordButton = changePasswordButton;
	changePasswordButton->setClickedCallback([=] {
		showOtherAndRememberPassword(CloudPasswordInputId());
	});
	const auto changeEmailButton = AddButtonWithIcon(
		actions,
		state->hasRecovery
			? tr::lng_settings_cloud_password_manage_email_change()
			: tr::lng_settings_cloud_password_manage_email_new(),
		st::settingsButton,
		{ &st::menuIconRecoveryEmail });
	_changeEmailButton = changeEmailButton;
	changeEmailButton->setClickedCallback([=] {
		auto data = stepData();
		data.setOnlyRecoveryEmail = true;
		setStepData(std::move(data));

		showOtherAndRememberPassword(CloudPasswordEmailId());
	});

	showFinishes() | rpl::take(1) | rpl::on_next([=] {
		controller()->checkHighlightControl(
			u"2sv/change"_q,
			_changePasswordButton);
		controller()->checkHighlightControl(
			u"2sv/change-email"_q,
			_changeEmailButton);
		controller()->checkHighlightControl("2sv/disable"_q, _disableButton);
	}, lifetime());

	AddCardDescription(content, tr::lng_settings_cloud_password_manage_about2());

	Ui::ResizeFitChild(this, page);
}

base::weak_qptr<Ui::RpWidget> Manage::createPinnedToBottom(
		not_null<Ui::RpWidget*> parent) {

	const auto disable = [=](Fn<void()> close) {
		if (_requestLifetime) {
			return;
		}
		_requestLifetime = cloudPassword().set(
			_currentPassword,
			QString(),
			QString(),
			false,
			QString()
		) | rpl::on_error_done([=](const QString &type) {
			AbstractStep::isPasswordInvalidError(type);
		}, [=] {
			setStepData(StepData());
			close();
			showBack();
		});
	};

	auto callback = [=] {
		controller()->show(
			Ui::MakeConfirmBox({
				.text = tr::lng_settings_cloud_password_manage_disable_sure(),
				.confirmed = disable,
				.confirmText = tr::lng_settings_auto_night_disable(),
				.confirmStyle = &st::attentionBoxButton,
			}));
	};
	auto bottomButton = CloudPassword::CreateBottomDisableButton(
		parent,
		geometryValue(),
		tr::lng_settings_password_disable(),
		std::move(callback));

	_disableButton = bottomButton.button.get();

	return bottomButton.content;
}

} // namespace CloudPassword

Type CloudPasswordManageId() {
	return CloudPassword::Manage::Id();
}

} // namespace Settings
