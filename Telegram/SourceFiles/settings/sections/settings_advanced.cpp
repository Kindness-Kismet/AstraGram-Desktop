/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_common.h"
#include "settings/sections/settings_advanced.h"

#include "settings/settings_common_session.h"

#include "api/api_global_privacy.h"
#include "apiwrap.h"
#include "base/call_delayed.h"
#include "base/platform/base_platform_custom_app_icon.h"
#include "base/platform/base_platform_info.h"
#include "base/screen_reader_state.h"
#include "settings/sections/settings_auto_download.h"
#include "boxes/connection_box.h"
#include "boxes/download_path_box.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/file_utilities.h"
#include "data/data_auto_download.h"
#include "data/data_session.h"
#include "export/export_manager.h"
#include "info/downloads/info_downloads_widget.h"
#include "info/info_memento.h"
#include "lang/lang_keys.h"
#include "main/main_account.h"
#include "main/main_domain.h"
#include "main/main_session.h"
#include "main/main_session_settings.h"
#include "mtproto/facade.h"
#include "mtproto/mtp_instance.h"
#include "platform/platform_specific.h"
#include "settings/settings_builder.h"
#include "settings/sections/settings_local_storage.h"
#include "settings/sections/settings_main.h"
#include "settings/sections/settings_chat.h"
#include "settings/settings_experimental.h"
#include "settings/settings_power_saving.h"
#include "settings/sections/settings_privacy_security.h"
#include "storage/localstorage.h"
#include "storage/storage_domain.h"
#include "tray.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/gl/gl_detection.h"
#include "ui/layers/generic_box.h"
#include "ui/painter.h"
#include "ui/power_saving.h"
#include "ui/rp_widget.h"
#include "ui/screen_reader_mode.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/settings_toggle.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_saved_windows.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#ifdef Q_OS_MAC
#include "base/platform/mac/base_confirm_quit.h"
#endif // Q_OS_MAC

#ifndef TDESKTOP_DISABLE_SPELLCHECK
#include "boxes/dictionaries_manager.h"
#include "chat_helpers/spellchecker_common.h"
#include "spellcheck/platform/platform_spellcheck.h"
#endif // !TDESKTOP_DISABLE_SPELLCHECK

namespace Settings {
namespace {

using namespace Builder;

#if defined Q_OS_MAC && !defined OS_MAC_STORE
[[nodiscard]] const QImage &IconMacRound() {
	static const auto result = QImage(u":/gui/art/icon_round512@2x.png"_q);
	return result;
}
#endif // Q_OS_MAC && !OS_MAC_STORE

void BuildInterfaceScaleSection(SectionBuilder &builder) {
	if (!HasInterfaceScale()) {
		return;
	}
	builder.addSubsectionTitle({
		.id = u"advanced/interface-scale"_q,
		.title = tr::lng_settings_scale(),
	});
	builder.add([](const WidgetContext &ctx) {
		auto wrap = object_ptr<Ui::VerticalLayout>(ctx.container);
		SetupInterfaceScale(&ctx.controller->window(), wrap.data());
		return SectionBuilder::WidgetToAdd{ .widget = std::move(wrap) };
	}, [] {
		return SearchEntry{
			.id = u"main/scale"_q,
			.title = tr::lng_settings_scale(tr::now),
			.keywords = tr::extras_InterfaceScaleSearchKeywords(tr::now).split(' '),
		};
	});
	builder.addSkip();
}

void BuildDataStorageSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto container = builder.container();
	const auto session = builder.session();
	const auto account = &session->account();

	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/data_storage"_q,
		.title = tr::lng_settings_data_storage(),
		.keywords = { u"storage"_q, u"data"_q, u"download"_q, u"connection"_q },
	});

	const auto connectionType = [=] {
		const auto transport = account->mtp().dctransport();
		if (!Core::App().settings().proxy().isEnabled()) {
			return transport.isEmpty()
				? tr::lng_connection_auto_connecting(tr::now)
				: tr::lng_connection_auto(tr::now, lt_transport, transport);
		} else {
			return transport.isEmpty()
				? tr::lng_connection_proxy_connecting(tr::now)
				: tr::lng_connection_proxy(tr::now, lt_transport, transport);
		}
	};

	builder.addButton({
		.id = u"advanced/connection_type"_q,
		.title = tr::lng_settings_connection_type(),
		.icon = { &st::menuIconNetwork },
		.label = rpl::merge(
			Core::App().settings().proxy().connectionTypeChanges(),
			tr::lng_connection_auto_connecting() | rpl::to_empty
		) | rpl::map(connectionType),
		.onClick = [=] {
			controller->window().show(
				ProxiesBoxController::CreateOwningBox(account));
		},
		.keywords = { u"connection"_q, u"proxy"_q, u"network"_q, u"vpn"_q },
	});

	const auto showDownloadPath = container
		? container->lifetime().make_state<rpl::variable<bool>>(
			!Core::App().settings().askDownloadPath())
		: nullptr;

	auto downloadLabel = Core::App().settings().downloadPathValue(
	) | rpl::map([](const QString &text) {
		if (text.isEmpty()) {
			return Core::App().canReadDefaultDownloadPath()
				? tr::lng_download_path_default(tr::now)
				: tr::lng_download_path_temp(tr::now);
		} else if (text == FileDialog::Tmp()) {
			return tr::lng_download_path_temp(tr::now);
		}
		return QDir::toNativeSeparators(text);
	});

	builder.addButton({
		.id = u"advanced/download_path"_q,
		.title = tr::lng_download_path(),
		.icon = { &st::menuIconShowInFolder },
		.label = std::move(downloadLabel),
		.onClick = [=] {
			controller->show(Box<DownloadPathBox>(controller));
		},
		.keywords = { u"download"_q, u"path"_q, u"folder"_q },
		.shown = showDownloadPath
			? showDownloadPath->value()
			: rpl::single(true) | rpl::type_erased,
	});

	builder.addButton({
		.id = u"advanced/storage"_q,
		.title = tr::lng_settings_manage_local_storage(),
		.icon = { &st::menuIconStorage },
		.onClick = [=] {
			controller->showSettings(LocalStorageId());
		},
		.keywords = { u"storage"_q, u"cache"_q, u"local"_q },
	});

	builder.addButton({
		.id = u"advanced/downloads"_q,
		.title = tr::lng_downloads_section(),
		.icon = { &st::menuIconDownload },
		.onClick = [=] {
			if (controller) {
				controller->showSection(
					Info::Downloads::Make(controller->session().user()));
			}
		},
		.keywords = { u"downloads"_q, u"files"_q },
	});

	const auto askDownloadPath = builder.addButton({
		.id = u"advanced/ask_download"_q,
		.title = tr::lng_download_path_ask(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(Core::App().settings().askDownloadPath()),
		.keywords = { u"download"_q, u"path"_q, u"ask"_q },
	});

	if (askDownloadPath) {
		askDownloadPath->toggledValue(
		) | rpl::filter([](bool checked) {
			return (checked != Core::App().settings().askDownloadPath());
		}) | rpl::on_next([=](bool checked) {
			Core::App().settings().setAskDownloadPath(checked);
			Core::App().saveSettingsDelayed();
			if (showDownloadPath) {
				*showDownloadPath = !checked;
			}
		}, askDownloadPath->lifetime());
	}

	builder.addSkip(st::settingsCheckboxesSkip);
}

void BuildAutoDownloadSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto container = builder.container();
	const auto session = builder.session();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/auto_download"_q,
		.title = tr::lng_media_auto_settings(),
		.keywords = { u"auto"_q, u"download"_q, u"media"_q },
	});

	using Source = Data::AutoDownload::Source;

	struct State {
		rpl::event_stream<> changes;
	};
	const auto state = container
		? container->lifetime().make_state<State>()
		: nullptr;
	const auto shouldBeChecked = [=](Source source) {
		return HasEnabledTypes(session->settings().autoDownload(), source);
	};
	const auto add = [&](
			QString id,
			rpl::producer<QString> title,
			const style::icon *icon,
			Source source,
			QStringList keywords) {
		const auto row = builder.addButton({
			.id = std::move(id),
			.title = std::move(title),
			.icon = { icon },
			.onClick = [=] {
				ShowAutoDownload(controller, source, crl::guard(container, [=] {
					state->changes.fire({});
				}));
			},
			.keywords = std::move(keywords),
		});
		if (!row) {
			return;
		}
		const auto [toggle, checkView] = AddSeparatedToggle(
			row,
			st::settingsButton,
			shouldBeChecked(source));
		state->changes.events() | rpl::on_next([=] {
			checkView->setChecked(shouldBeChecked(source), anim::type::normal);
		}, row->lifetime());
		toggle->clicks() | rpl::on_next([=] {
			auto &data = session->settings().autoDownload();
			const auto enable = !checkView->checked();
			if (enable) {
				SetEnabledForSource(data, source);
				session->data().photoLoadSettingsChanged();
				session->data().documentLoadSettingsChanged();
			} else {
				SetDisabledForSource(data, source);
				session->data().checkPlayingAnimations();
			}
			session->saveSettingsDelayed();
			state->changes.fire({});
		}, toggle->lifetime());
	};

	add(
		u"advanced/auto_download_private"_q,
		tr::lng_media_auto_in_private(),
		&st::menuIconProfile,
		Source::User,
		{ u"auto"_q, u"download"_q, u"private"_q, u"media"_q });
	add(
		u"advanced/auto_download_groups"_q,
		tr::lng_media_auto_in_groups(),
		&st::menuIconGroups,
		Source::Group,
		{ u"auto"_q, u"download"_q, u"groups"_q, u"media"_q });
	add(
		u"advanced/auto_download_channels"_q,
		tr::lng_media_auto_in_channels(),
		&st::menuIconChannel,
		Source::Channel,
		{ u"auto"_q, u"download"_q, u"channels"_q, u"media"_q });

	builder.addSkip(st::settingsCheckboxesSkip);
}

#if !defined Q_OS_WIN && !defined Q_OS_MAC
void BuildWindowCloseBehaviorSection(SectionBuilder &builder) {
	using Behavior = Core::Settings::CloseBehavior;

	const auto settings = &Core::App().settings();
	auto shown = Platform::TrayIconSupported()
		? (Core::App().settings().workModeValue(
			) | rpl::map([](Core::Settings::WorkMode mode) {
				return (mode == Core::Settings::WorkMode::WindowOnly);
			}) | rpl::distinct_until_changed() | rpl::type_erased)
		: rpl::producer<bool>(nullptr);

	builder.scope([&] {
		builder.addDivider();
		builder.addSkip();
		builder.addSubsectionTitle({
			.id = u"advanced/window_close"_q,
			.title = tr::lng_settings_window_close(),
			.keywords = { u"close"_q, u"window"_q, u"background"_q, u"quit"_q, u"taskbar"_q, u"minimize"_q },
		});

		builder.add([settings](const WidgetContext &ctx) {
			const auto container = ctx.container.get();
			auto wrap = object_ptr<Ui::VerticalLayout>(container);
			const auto inner = wrap.data();

			const auto group = std::make_shared<Ui::RadioenumGroup<Behavior>>(
				settings->closeBehavior());
			const auto addRadio = [&](Behavior value, const QString &label) {
				inner->add(
					object_ptr<Ui::Radioenum<Behavior>>(
						inner,
						group,
						value,
						label,
						st::settingsSendType),
					st::settingsSendTypePadding);
			};

			addRadio(
				Behavior::RunInBackground,
				tr::lng_settings_run_in_background(tr::now));
			addRadio(
				Behavior::CloseToTaskbar,
				tr::lng_settings_close_to_taskbar(tr::now));
			addRadio(
				Behavior::Quit,
				tr::lng_settings_quit_on_close(tr::now));

			group->value() | rpl::filter([=](Behavior value) {
				return (value != settings->closeBehavior());
			}) | rpl::on_next([=](Behavior value) {
				settings->setCloseBehavior(value);
				Local::writeSettings();
			}, inner->lifetime());

			return SectionBuilder::WidgetToAdd{ .widget = std::move(wrap) };
		});

		builder.addSkip();
	}, std::move(shown));
}
#endif // !Q_OS_WIN && !Q_OS_MAC

void BuildSystemIntegrationSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto settings = &Core::App().settings();

	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/system_integration"_q,
		.title = tr::lng_settings_system_integration(),
		.keywords = { u"system"_q, u"tray"_q, u"startup"_q, u"autostart"_q },
	});

	using WorkMode = Core::Settings::WorkMode;

	if (Platform::TrayIconSupported()) {
		const auto trayEnabled = [=] {
			const auto workMode = settings->workMode();
			return (workMode == WorkMode::TrayOnly)
				|| (workMode == WorkMode::WindowAndTray);
		};
		const auto tray = builder.addToggle({
			.id = u"advanced/tray"_q,
			.title = tr::lng_settings_workmode_tray(),
			.checked = trayEnabled(),
			.keywords = { u"tray"_q, u"icon"_q, u"system"_q },
		});

		const auto taskbarEnabled = [=] {
			const auto workMode = settings->workMode();
			return (workMode == WorkMode::WindowOnly)
				|| (workMode == WorkMode::WindowAndTray);
		};
		const auto taskbar = Platform::SkipTaskbarSupported()
			? builder.addToggle({
				.id = u"advanced/taskbar"_q,
				.title = tr::lng_settings_workmode_window(),
				.checked = taskbarEnabled(),
				.keywords = { u"taskbar"_q, u"window"_q },
			})
			: nullptr;

		const auto monochrome = Platform::HasMonochromeSetting()
			? builder.addToggle({
				.id = u"advanced/monochrome_icon"_q,
				.title = tr::lng_settings_monochrome_icon(),
				.checked = settings->trayIconMonochrome(),
				.keywords = { u"monochrome"_q, u"icon"_q, u"tray"_q },
				.shown = tray
					? tray->checkedValue()
					: rpl::single(trayEnabled()),
			})
			: nullptr;

		if (monochrome) {
			monochrome->checkedChanges(
			) | rpl::filter([=](bool value) {
				return (value != settings->trayIconMonochrome());
			}) | rpl::on_next([=](bool value) {
				settings->setTrayIconMonochrome(value);
				Core::App().saveSettingsDelayed();
			}, monochrome->lifetime());
		}

		const auto updateWorkmode = [=] {
			const auto newMode = (tray && tray->checked())
				? ((!taskbar || taskbar->checked())
					? WorkMode::WindowAndTray
					: WorkMode::TrayOnly)
				: WorkMode::WindowOnly;
			if ((newMode == WorkMode::WindowAndTray
				|| newMode == WorkMode::TrayOnly)
				&& settings->workMode() != newMode) {
				cSetSeenTrayTooltip(false);
			}
			settings->setWorkMode(newMode);
			Core::App().saveSettingsDelayed();
		};

		if (tray) {
			tray->checkedChanges(
			) | rpl::filter([=](bool checked) {
				return (checked != trayEnabled());
			}) | rpl::on_next([=](bool checked) {
				if (!checked && taskbar && !taskbar->checked()) {
					taskbar->setChecked(true);
				} else {
					updateWorkmode();
				}
			}, tray->lifetime());
		}

		if (taskbar) {
			taskbar->checkedChanges(
			) | rpl::filter([=](bool checked) {
				return (checked != taskbarEnabled());
			}) | rpl::on_next([=](bool checked) {
				if (!checked && tray && !tray->checked()) {
					tray->setChecked(true);
				} else {
					updateWorkmode();
				}
			}, taskbar->lifetime());
		}
	}

#ifdef Q_OS_MAC
	const auto warnBeforeQuit = builder.addToggle({
		.id = u"advanced/warn_before_quit"_q,
		.title = tr::lng_settings_mac_warn_before_quit(
			lt_text,
			rpl::single(Platform::ConfirmQuit::QuitKeysString())),
		.checked = settings->macWarnBeforeQuit(),
		.keywords = { u"quit"_q, u"warn"_q, u"close"_q },
	});
	if (warnBeforeQuit) {
		warnBeforeQuit->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != settings->macWarnBeforeQuit());
		}) | rpl::on_next([=](bool checked) {
			settings->setMacWarnBeforeQuit(checked);
			Core::App().saveSettingsDelayed();
		}, warnBeforeQuit->lifetime());
	}

	const auto systemReplace = builder.addToggle({
		.id = u"advanced/system_text_replace"_q,
		.title = tr::lng_settings_system_text_replace(),
		.checked = settings->systemTextReplace(),
		.keywords = { u"text"_q, u"replace"_q, u"system"_q },
	});
	if (systemReplace) {
		systemReplace->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != settings->systemTextReplace());
		}) | rpl::on_next([=](bool checked) {
			settings->setSystemTextReplace(checked);
			Core::App().saveSettingsDelayed();
		}, systemReplace->lifetime());
	}

#ifndef OS_MAC_STORE
	const auto roundIconEnabled = [=] {
		const auto digest = base::Platform::CurrentCustomAppIconDigest();
		return digest && (settings->macRoundIconDigest() == digest);
	};
	const auto roundIcon = builder.addToggle({
		.id = u"advanced/round_icon"_q,
		.title = tr::lng_settings_mac_round_icon(),
		.checked = roundIconEnabled(),
		.keywords = { u"icon"_q, u"round"_q, u"dock"_q },
	});
	if (roundIcon) {
		roundIcon->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != roundIconEnabled());
		}) | rpl::on_next([=](bool checked) {
			const auto digest = checked
				? base::Platform::SetCustomAppIcon(IconMacRound())
				: std::optional<uint64>();
			if (!checked) {
				base::Platform::ClearCustomAppIcon();
			}
			Window::OverrideApplicationIcon(checked ? IconMacRound() : QImage());
			Core::App().refreshApplicationIcon();
			settings->setMacRoundIconDigest(digest);
			Core::App().saveSettings();
		}, roundIcon->lifetime());
	}
#endif // OS_MAC_STORE
#elif defined Q_OS_WIN // Q_OS_MAC
	using Behavior = Core::Settings::CloseBehavior;

	const auto container = builder.container();
	const auto closeToTaskbarShown = container
		? container->lifetime().make_state<rpl::variable<bool>>(
			!Core::App().tray().has())
		: nullptr;

	if (closeToTaskbarShown) {
		settings->workModeValue(
		) | rpl::on_next([=](WorkMode) {
			*closeToTaskbarShown = !Core::App().tray().has();
		}, container->lifetime());
	}

	const auto closeToTaskbar = builder.addToggle({
		.id = u"advanced/close_to_taskbar"_q,
		.title = tr::lng_settings_close_to_taskbar(),
		.checked = settings->closeBehavior() == Behavior::CloseToTaskbar,
		.keywords = { u"close"_q, u"taskbar"_q, u"minimize"_q },
		.shown = closeToTaskbarShown
			? closeToTaskbarShown->value()
			: rpl::single(false),
	});
	if (closeToTaskbar) {
		closeToTaskbar->checkedChanges(
		) | rpl::map([=](bool checked) {
			return checked ? Behavior::CloseToTaskbar : Behavior::Quit;
		}) | rpl::filter([=](Behavior value) {
			return (settings->closeBehavior() != value);
		}) | rpl::on_next([=](Behavior value) {
			settings->setCloseBehavior(value);
			Local::writeSettings();
		}, closeToTaskbar->lifetime());
	}
#endif // Q_OS_MAC || Q_OS_WIN

	if (Platform::AutostartSupported()) {
		const auto minimizedToggled = [=] {
			return cStartMinimized()
				&& controller
				&& !controller->session().domain().local().hasLocalPasscode();
		};

		const auto autostart = builder.addToggle({
			.id = u"advanced/autostart"_q,
			.title = tr::lng_settings_auto_start(),
			.checked = cAutoStart(),
			.keywords = { u"autostart"_q, u"startup"_q, u"boot"_q },
		});

		const auto minimized = builder.addToggle({
			.id = u"advanced/start_minimized"_q,
			.title = tr::lng_settings_start_min(),
			.checked = minimizedToggled(),
			.keywords = { u"minimized"_q, u"startup"_q, u"hidden"_q },
			.shown = autostart
				? autostart->checkedValue()
				: rpl::single(cAutoStart()),
		});

		if (autostart) {
			autostart->checkedChanges(
			) | rpl::filter([](bool checked) {
				return (checked != cAutoStart());
			}) | rpl::on_next([=](bool checked) {
				const auto weak = base::make_weak(controller);
				cSetAutoStart(checked);
				Platform::AutostartToggle(checked, crl::guard(autostart, [=](
						bool enabled) {
					if (checked && !enabled && weak) {
						weak->window().showToast(
							Lang::Hard::AutostartEnableError());
					}
					Ui::PostponeCall(autostart, [=] {
						autostart->setChecked(enabled);
					});
					if (enabled || !minimized || !minimized->checked()) {
						Local::writeSettings();
					} else if (minimized) {
						minimized->setChecked(false);
					}
				}));
			}, autostart->lifetime());

			if (controller) {
				Platform::AutostartRequestStateFromSystem(crl::guard(
					controller,
					[=](bool enabled) { autostart->setChecked(enabled); }));
			}
		}

		if (minimized && controller) {
			minimized->checkedChanges(
			) | rpl::filter([=](bool checked) {
				return (checked != minimizedToggled());
			}) | rpl::on_next([=](bool checked) {
				if (controller->session().domain().local().hasLocalPasscode()) {
					minimized->setChecked(false);
					controller->show(Ui::MakeInformBox(
						tr::lng_error_start_minimized_passcoded()));
				} else {
					cSetStartMinimized(checked);
					Local::writeSettings();
				}
			}, minimized->lifetime());

			controller->session().domain().local().localPasscodeChanged(
			) | rpl::on_next([=] {
				minimized->setChecked(minimizedToggled());
			}, minimized->lifetime());
		}
	}

	const auto restoreWindows = builder.addToggle({
		.id = u"advanced/restore_windows"_q,
		.title = tr::lng_settings_restore_windows(),
		.checked = Core::App().savedWindows()->restoreOnLaunch(),
		.keywords = {
			u"restore"_q,
			u"windows"_q,
			u"launch"_q,
			u"startup"_q,
			u"reopen"_q,
			u"session"_q,
		},
	});
	if (restoreWindows) {
		restoreWindows->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != Core::App().savedWindows()->restoreOnLaunch());
		}) | rpl::on_next([=](bool checked) {
			Core::App().savedWindows()->setRestoreOnLaunch(checked);
		}, restoreWindows->lifetime());
	}

	if (Platform::IsWindows() && !Platform::IsWindowsStoreBuild()) {
		const auto sendto = builder.addToggle({
			.id = u"advanced/sendto"_q,
			.title = tr::lng_settings_add_sendto(),
			.checked = cSendToMenu(),
			.keywords = { u"sendto"_q, u"send"_q, u"menu"_q, u"context"_q },
		});
		if (sendto) {
			sendto->checkedChanges(
			) | rpl::filter([](bool checked) {
				return (checked != cSendToMenu());
			}) | rpl::on_next([](bool checked) {
				cSetSendToMenu(checked);
				psSendToMenu(checked);
				Local::writeSettings();
			}, sendto->lifetime());
		}
	}

	builder.addSkip();
}

#ifdef DESKTOP_APP_USE_ANGLE
void BuildANGLEOption(SectionBuilder &builder) {
	const auto controller = builder.controller();
	using ANGLE = Ui::GL::ANGLE;

	const auto options = std::vector{
		tr::lng_settings_angle_backend_auto(tr::now),
		tr::lng_settings_angle_backend_d3d11(tr::now),
		tr::lng_settings_angle_backend_d3d9(tr::now),
		tr::lng_settings_angle_backend_d3d11on12(tr::now),
		tr::lng_settings_angle_backend_disabled(tr::now),
	};
	const auto disabled = int(options.size()) - 1;
	const auto backendIndex = [=] {
		if (Core::App().settings().disableOpenGL()) {
			return disabled;
		} else switch (Ui::GL::CurrentANGLE()) {
		case ANGLE::Auto: return 0;
		case ANGLE::D3D11: return 1;
		case ANGLE::D3D9: return 2;
		case ANGLE::D3D11on12: return 3;
		}
		Unexpected("Ui::GL::CurrentANGLE value in BuildANGLEOption.");
	}();

	builder.addButton({
		.id = u"advanced/angle_backend"_q,
		.title = tr::lng_settings_angle_backend(),
		.st = &st::settingsButtonNoIcon,
		.label = rpl::single(options[backendIndex]),
		.onClick = [=] {
			controller->show(Box([=](not_null<Ui::GenericBox*> box) {
				const auto save = [=](int index) {
					if (index == backendIndex) {
						return;
					}
					const auto confirmed = [=] {
						const auto nowDisabled = (index == disabled);
						if (!nowDisabled) {
							Ui::GL::ChangeANGLE([&] {
								switch (index) {
								case 0: return ANGLE::Auto;
								case 1: return ANGLE::D3D11;
								case 2: return ANGLE::D3D9;
								case 3: return ANGLE::D3D11on12;
								}
								Unexpected("Index in BuildANGLEOption.");
							}());
						}
						const auto wasDisabled = (backendIndex == disabled);
						if (nowDisabled != wasDisabled) {
							Core::App().settings().setDisableOpenGL(nowDisabled);
							Local::writeSettings();
						}
						Core::Restart();
					};
					controller->show(Ui::MakeConfirmBox({
						.text = tr::lng_settings_need_restart(),
						.confirmed = confirmed,
						.confirmText = tr::lng_settings_restart_now(),
					}));
				};
				SingleChoiceBox(box, {
					.title = tr::lng_settings_angle_backend(),
					.options = options,
					.initialSelection = backendIndex,
					.callback = save,
				});
			}));
		},
		.keywords = { u"angle"_q, u"opengl"_q, u"d3d"_q, u"graphics"_q },
	});
}
#else
void BuildOpenGLOption(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto opengl = builder.addButton({
		.id = u"advanced/opengl"_q,
		.title = tr::lng_settings_enable_opengl(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(!Core::App().settings().disableOpenGL()),
		.keywords = { u"opengl"_q, u"graphics"_q, u"gpu"_q },
	});

	if (opengl) {
		opengl->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled == Core::App().settings().disableOpenGL());
		}) | rpl::on_next([=](bool enabled) {
			const auto confirmed = crl::guard(opengl, [=] {
				Core::App().settings().setDisableOpenGL(!enabled);
				Local::writeSettings();
				Core::Restart();
			});
			controller->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = confirmed,
				.confirmText = tr::lng_settings_restart_now(),
			}));
		}, opengl->lifetime());
	}
}
#endif

void BuildPerformanceSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/performance"_q,
		.title = tr::lng_settings_performance(),
		.keywords = { u"performance"_q, u"power"_q, u"graphics"_q, u"hardware"_q },
	});

	builder.addButton({
		.id = u"advanced/power_saving"_q,
		.title = tr::lng_settings_power_menu(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			ShowPowerSaving(controller);
		},
		.keywords = { u"power"_q, u"saving"_q, u"battery"_q, u"animation"_q },
	});

	const auto hwAccel = builder.addButton({
		.id = u"advanced/hw_accel"_q,
		.title = tr::lng_settings_enable_hwaccel(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(
			Core::App().settings().hardwareAcceleratedVideo()),
		.keywords = { u"hardware"_q, u"acceleration"_q, u"video"_q },
	});

	if (hwAccel) {
		hwAccel->toggledValue(
		) | rpl::filter([](bool enabled) {
			return (enabled !=
				Core::App().settings().hardwareAcceleratedVideo());
		}) | rpl::on_next([=](bool enabled) {
			Core::App().settings().setHardwareAcceleratedVideo(enabled);
			Core::App().saveSettingsDelayed();
		}, hwAccel->lifetime());
	}

#ifdef DESKTOP_APP_USE_ANGLE
	BuildANGLEOption(builder);
#else
	if constexpr (!Platform::IsMac()) {
		BuildOpenGLOption(builder);
	}
#endif

	builder.addSkip();
}

void BuildSpellcheckerSection(SectionBuilder &builder) {
#ifndef TDESKTOP_DISABLE_SPELLCHECK
	const auto controller = builder.controller();
	const auto session = builder.session();
	const auto settings = &Core::App().settings();
	const auto isSystem = Platform::Spellchecker::IsSystemSpellchecker();

	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/spellchecker"_q,
		.title = tr::lng_settings_spellchecker(),
		.keywords = { u"spellcheck"_q, u"spelling"_q, u"dictionary"_q },
	});

	const auto spellchecker = builder.addButton({
		.id = u"advanced/spellchecker_toggle"_q,
		.title = isSystem
			? tr::lng_settings_system_spellchecker()
			: tr::lng_settings_custom_spellchecker(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->spellcheckerEnabled()),
		.keywords = { u"spellcheck"_q, u"spelling"_q, u"dictionary"_q },
	});

	if (spellchecker) {
		spellchecker->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->spellcheckerEnabled());
		}) | rpl::on_next([=](bool enabled) {
			settings->setSpellcheckerEnabled(enabled);
			Core::App().saveSettingsDelayed();
		}, spellchecker->lifetime());
	}

	if (!isSystem) {
		builder.scope([&] {
			const auto autoDownload = builder.addButton({
				.id = u"advanced/auto_download_dictionaries"_q,
				.title = tr::lng_settings_auto_download_dictionaries(),
				.st = &st::settingsButtonNoIcon,
				.toggled = rpl::single(settings->autoDownloadDictionaries()),
				.keywords = { u"dictionary"_q, u"download"_q, u"spellcheck"_q },
			});

			if (autoDownload) {
				autoDownload->toggledValue(
				) | rpl::filter([=](bool enabled) {
					return (enabled != settings->autoDownloadDictionaries());
				}) | rpl::on_next([=](bool enabled) {
					settings->setAutoDownloadDictionaries(enabled);
					Core::App().saveSettingsDelayed();
				}, autoDownload->lifetime());
			}

			builder.addButton({
				.id = u"advanced/manage_dictionaries"_q,
				.title = tr::lng_settings_manage_dictionaries(),
				.st = &st::settingsButtonNoIcon,
				.label = Spellchecker::ButtonManageDictsState(session),
				.onClick = [=] {
					controller->show(Box<Ui::ManageDictionariesBox>(session));
				},
				.keywords = { u"dictionary"_q, u"manage"_q, u"spellcheck"_q },
			});
		}, spellchecker ? spellchecker->toggledValue() : nullptr);
	}

	builder.addSkip();
#endif // !TDESKTOP_DISABLE_SPELLCHECK
}

void BuildExportSection(SectionBuilder &builder) {
	const auto controller = builder.controller();
	const auto session = builder.session();
	const auto showOther = builder.showOther();
	builder.addSkip();
	builder.addDivider();
	builder.addSkip();

	builder.addButton({
		.id = u"advanced/export"_q,
		.title = tr::lng_settings_export_data(),
		.icon = { &st::menuIconExport },
		.onClick = [=] {
			controller->window().hideSettingsAndLayer();
			base::call_delayed(
				st::boxDuration,
				session,
				[=] { Core::App().exportManager().start(session); });
		},
		.keywords = { u"export"_q, u"data"_q, u"backup"_q },
	});

	builder.addButton({
		.id = u"advanced/experimental"_q,
		.title = tr::lng_settings_experimental(),
		.icon = { &st::menuIconExperimental },
		.onClick = [showOther] { showOther(Experimental::Id()); },
		.keywords = { u"experimental"_q, u"beta"_q, u"features"_q },
	});
}

void BuildScreenReaderSection(SectionBuilder &builder) {
	const auto detected = base::ScreenReaderState::Instance()->active();
	const auto disabled = Ui::ScreenReaderModeDisabled();
	if (!detected || !disabled) {
		return;
	}

	builder.addDivider();
	builder.addSkip();
	builder.addSubsectionTitle({
		.id = u"advanced/screen_reader"_q,
		.title = tr::lng_screen_reader_settings_title(),
		.keywords = { u"screen reader"_q, u"accessibility"_q, u"voiceover"_q },
	});

	const auto toggle = builder.addButton({
		.id = u"advanced/screen_reader_disable"_q,
		.title = tr::lng_screen_reader_settings_disable(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(disabled),
		.keywords = { u"screen reader"_q, u"accessibility"_q },
	});

	if (toggle) {
		toggle->toggledValue(
		) | rpl::filter([=](bool value) {
			return (value != Ui::ScreenReaderModeDisabled());
		}) | rpl::on_next([=](bool value) {
			Core::App().settings().writePref<bool>(
				Core::kScreenReaderModeDisabledKey,
				value);
			Core::App().saveSettingsDelayed();
			Ui::SetScreenReaderModeDisabled(value);
		}, toggle->lifetime());
	}

	builder.addSkip();
}

class Advanced : public Section<Advanced> {
public:
	Advanced(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;

private:
	void setupContent();

};

const auto kMeta = BuildHelper({
	.id = Advanced::Id(),
	.parentId = MainId(),
	.title = &tr::lng_settings_advanced,
	.icon = &st::menuIconManage,
}, [](SectionBuilder &builder) {
	BuildInterfaceScaleSection(builder);
	BuildDataStorageSection(builder);
	BuildAutoDownloadSection(builder);
#if !defined Q_OS_WIN && !defined Q_OS_MAC
	BuildWindowCloseBehaviorSection(builder);
#endif
	BuildSystemIntegrationSection(builder);
	BuildPerformanceSection(builder);
	BuildSpellcheckerSection(builder);
	BuildScreenReaderSection(builder);
	BuildExportSection(builder);
});

const SectionBuildMethod kAdvancedSection = kMeta.build;

Advanced::Advanced(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

rpl::producer<QString> Advanced::title() {
	return tr::lng_settings_advanced();
}

void Advanced::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);

	build(content, kAdvancedSection);

	Ui::ResizeFitChild(this, content);
}

} // namespace

void SetupConnectionType(
		not_null<Window::Controller*> controller,
		not_null<::Main::Account*> account,
		not_null<Ui::VerticalLayout*> container) {
	const auto connectionType = [=] {
		const auto transport = account->mtp().dctransport();
		if (!Core::App().settings().proxy().isEnabled()) {
			return transport.isEmpty()
				? tr::lng_connection_auto_connecting(tr::now)
				: tr::lng_connection_auto(tr::now, lt_transport, transport);
		} else {
			return transport.isEmpty()
				? tr::lng_connection_proxy_connecting(tr::now)
				: tr::lng_connection_proxy(tr::now, lt_transport, transport);
		}
	};
	const auto button = AddButtonWithLabel(
		container,
		tr::lng_settings_connection_type(),
		rpl::merge(
			Core::App().settings().proxy().connectionTypeChanges(),
			tr::lng_connection_auto_connecting() | rpl::to_empty
		) | rpl::map([=] { return connectionType(); }),
		st::settingsButton,
		{ &st::menuIconNetwork });
	button->addClickHandler([=] {
		controller->show(ProxiesBoxController::CreateOwningBox(account));
	});
}

void SetupSystemIntegrationContent(
		Window::SessionController *controller,
		not_null<Ui::VerticalLayout*> container) {
	using WorkMode = Core::Settings::WorkMode;

	const auto checkbox = [&](rpl::producer<QString> &&label, bool checked) {
		return object_ptr<Ui::SettingsToggle>(
			container,
			std::move(label),
			checked,
			st::settingsButtonNoIcon);
	};
	const auto addCheckbox = [&](
			rpl::producer<QString> &&label,
			bool checked) {
		return container->add(
			checkbox(std::move(label), checked));
	};
	const auto addSlidingCheckbox = [&](
			rpl::producer<QString> &&label,
			bool checked) {
		return container->add(
			object_ptr<Ui::SlideWrap<Ui::SettingsToggle>>(
				container,
				checkbox(std::move(label), checked)));
	};

	const auto settings = &Core::App().settings();
	if (Platform::TrayIconSupported()) {
		const auto trayEnabled = [=] {
			const auto workMode = settings->workMode();
			return (workMode == WorkMode::TrayOnly)
				|| (workMode == WorkMode::WindowAndTray);
		};
		const auto tray = addCheckbox(
			tr::lng_settings_workmode_tray(),
			trayEnabled());
		const auto monochrome = Platform::HasMonochromeSetting()
			? addSlidingCheckbox(
				tr::lng_settings_monochrome_icon(),
				settings->trayIconMonochrome())
			: nullptr;
		if (monochrome) {
			monochrome->toggle(tray->checked(), anim::type::instant);

			monochrome->entity()->checkedChanges(
			) | rpl::filter([=](bool value) {
				return (value != settings->trayIconMonochrome());
			}) | rpl::on_next([=](bool value) {
				settings->setTrayIconMonochrome(value);
				Core::App().saveSettingsDelayed();
			}, monochrome->lifetime());
		}

		const auto taskbarEnabled = [=] {
			const auto workMode = settings->workMode();
			return (workMode == WorkMode::WindowOnly)
				|| (workMode == WorkMode::WindowAndTray);
		};
		const auto taskbar = Platform::SkipTaskbarSupported()
			? addCheckbox(
				tr::lng_settings_workmode_window(),
				taskbarEnabled())
			: nullptr;

		const auto updateWorkmode = [=] {
			const auto newMode = tray->checked()
				? ((!taskbar || taskbar->checked())
					? WorkMode::WindowAndTray
					: WorkMode::TrayOnly)
				: WorkMode::WindowOnly;
			if ((newMode == WorkMode::WindowAndTray
				|| newMode == WorkMode::TrayOnly)
				&& settings->workMode() != newMode) {
				cSetSeenTrayTooltip(false);
			}
			settings->setWorkMode(newMode);
			Core::App().saveSettingsDelayed();
		};

		tray->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != trayEnabled());
		}) | rpl::on_next([=](bool checked) {
			if (!checked && taskbar && !taskbar->checked()) {
				taskbar->setChecked(true);
			} else {
				updateWorkmode();
			}
			if (monochrome) {
				monochrome->toggle(checked, anim::type::normal);
			}
		}, tray->lifetime());

		if (taskbar) {
			taskbar->checkedChanges(
			) | rpl::filter([=](bool checked) {
				return (checked != taskbarEnabled());
			}) | rpl::on_next([=](bool checked) {
				if (!checked && !tray->checked()) {
					tray->setChecked(true);
				} else {
					updateWorkmode();
				}
			}, taskbar->lifetime());
		}
	}

#ifdef Q_OS_MAC
	const auto warnBeforeQuit = addCheckbox(
		tr::lng_settings_mac_warn_before_quit(
			lt_text,
			rpl::single(Platform::ConfirmQuit::QuitKeysString())),
		settings->macWarnBeforeQuit());
	warnBeforeQuit->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != settings->macWarnBeforeQuit());
	}) | rpl::on_next([=](bool checked) {
		settings->setMacWarnBeforeQuit(checked);
		Core::App().saveSettingsDelayed();
	}, warnBeforeQuit->lifetime());

#ifndef OS_MAC_STORE
	const auto enabled = [=] {
		const auto digest = base::Platform::CurrentCustomAppIconDigest();
		return digest && (settings->macRoundIconDigest() == digest);
	};
	const auto roundIcon = addCheckbox(
		tr::lng_settings_mac_round_icon(),
		enabled());
	roundIcon->checkedChanges(
	) | rpl::filter([=](bool checked) {
		return (checked != enabled());
	}) | rpl::on_next([=](bool checked) {
		const auto digest = checked
			? base::Platform::SetCustomAppIcon(IconMacRound())
			: std::optional<uint64>();
		if (!checked) {
			base::Platform::ClearCustomAppIcon();
		}
		Window::OverrideApplicationIcon(checked ? IconMacRound() : QImage());
		Core::App().refreshApplicationIcon();
		settings->setMacRoundIconDigest(digest);
		Core::App().saveSettings();
	}, roundIcon->lifetime());
#endif // OS_MAC_STORE
#elif defined Q_OS_WIN // Q_OS_MAC
	using Behavior = Core::Settings::CloseBehavior;
	const auto closeToTaskbar = addSlidingCheckbox(
		tr::lng_settings_close_to_taskbar(),
		settings->closeBehavior() == Behavior::CloseToTaskbar);

	const auto closeToTaskbarShown = std::make_shared<
		rpl::variable<bool>
	>(false);
	settings->workModeValue(
	) | rpl::on_next([=](WorkMode workMode) {
		*closeToTaskbarShown = !Core::App().tray().has();
	}, closeToTaskbar->lifetime());

	closeToTaskbar->toggleOn(closeToTaskbarShown->value());
	closeToTaskbar->entity()->checkedChanges(
	) | rpl::map([=](bool checked) {
		return checked ? Behavior::CloseToTaskbar : Behavior::Quit;
	}) | rpl::filter([=](Behavior value) {
		return (settings->closeBehavior() != value);
	}) | rpl::on_next([=](Behavior value) {
		settings->setCloseBehavior(value);
		Local::writeSettings();
	}, closeToTaskbar->lifetime());
#endif // Q_OS_MAC || Q_OS_WIN

	if (Platform::AutostartSupported() && controller) {
		const auto minimizedToggled = [=] {
			return cStartMinimized()
				&& !controller->session().domain().local().hasLocalPasscode();
		};

		const auto autostart = addCheckbox(
			tr::lng_settings_auto_start(),
			cAutoStart());
		const auto minimized = addSlidingCheckbox(
			tr::lng_settings_start_min(),
			minimizedToggled());

		autostart->checkedChanges(
		) | rpl::filter([](bool checked) {
			return (checked != cAutoStart());
		}) | rpl::on_next([=](bool checked) {
			const auto weak = base::make_weak(controller);
			cSetAutoStart(checked);
			Platform::AutostartToggle(checked, crl::guard(autostart, [=](
					bool enabled) {
				if (checked && !enabled && weak) {
					weak->window().showToast(
						Lang::Hard::AutostartEnableError());
				}
				Ui::PostponeCall(autostart, [=] {
					autostart->setChecked(enabled);
				});
				if (enabled || !minimized->entity()->checked()) {
					Local::writeSettings();
				} else {
					minimized->entity()->setChecked(false);
				}
			}));
		}, autostart->lifetime());

		Platform::AutostartRequestStateFromSystem(crl::guard(
			controller,
			[=](bool enabled) { autostart->setChecked(enabled); }));

		minimized->toggleOn(autostart->checkedValue());
		minimized->entity()->checkedChanges(
		) | rpl::filter([=](bool checked) {
			return (checked != minimizedToggled());
		}) | rpl::on_next([=](bool checked) {
			if (controller->session().domain().local().hasLocalPasscode()) {
				minimized->entity()->setChecked(false);
				controller->show(Ui::MakeInformBox(
					tr::lng_error_start_minimized_passcoded()));
			} else {
				cSetStartMinimized(checked);
				Local::writeSettings();
			}
		}, minimized->lifetime());

		controller->session().domain().local().localPasscodeChanged(
		) | rpl::on_next([=] {
			minimized->entity()->setChecked(minimizedToggled());
		}, minimized->lifetime());
	}

	if (Platform::IsWindows() && !Platform::IsWindowsStoreBuild()) {
		const auto sendto = addCheckbox(
			tr::lng_settings_add_sendto(),
			cSendToMenu());

		sendto->checkedChanges(
		) | rpl::filter([](bool checked) {
			return (checked != cSendToMenu());
		}) | rpl::on_next([](bool checked) {
			cSetSendToMenu(checked);
			psSendToMenu(checked);
			Local::writeSettings();
		}, sendto->lifetime());
	}
}

void SetupAnimations(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container) {
	container->add(object_ptr<Settings::RowButton>(
		container,
		tr::lng_settings_power_menu(),
		st::settingsButtonNoIcon
	))->setClickedCallback([=] {
		window->show(Box(PowerSavingBox, PowerSaving::Flags()));
	});
}

Type AdvancedId() {
	return Advanced::Id();
}

} // namespace Settings
