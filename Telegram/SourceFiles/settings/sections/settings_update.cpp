#include "settings/sections/settings_update.h"

#include "settings/settings_common.h"
#include "boxes/about_box.h"
#include "core/application.h"
#include "core/launcher.h"
#include "core/update_channel.h"
#include "core/update_checker.h"
#include "lang/lang_keys.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "storage/localstorage.h"
#include "ui/text/format_values.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_settings.h"

#include <ksandbox.h>

namespace Settings {

using namespace Builder;

void BuildUpdateSection(SectionBuilder &builder) {
	if (!HasUpdate()) {
		return;
	}
	const auto container = builder.container();

	builder.addSkip();

	const auto version = tr::lng_settings_current_version(
		tr::now,
		lt_version,
		currentVersionText());

	const auto texts = container
		? Ui::CreateChild<rpl::event_stream<QString>>(container)
		: nullptr;
	const auto downloading = container
		? Ui::CreateChild<rpl::event_stream<bool>>(container)
		: nullptr;

	const auto toggle = builder.addButton({
		.id = u"main/updates/auto_update"_q,
		.altIds = { u"advanced/auto_update"_q },
		.title = tr::extras_AutoCheckUpdates(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(cAutoUpdate()),
		.keywords = { u"update"_q, u"automatic"_q, u"version"_q },
	});

	if (toggle) {
		builder.add([=](const WidgetContext &ctx) {
			auto label = object_ptr<Ui::FlatLabel>(
				ctx.container, texts->events(), st::settingsCardHint);
			label->setObjectName(u"about-update-status"_q);
			return SectionBuilder::WidgetToAdd{
				.widget = std::move(label),
				.margin = st::settingsCardHintPadding,
			};
		});
	}

	auto optionsShown = rpl::producer<bool>(nullptr);
	if (toggle) {
		Core::UpdateChecker checker;
		optionsShown = downloading->events_starting_with(
			checker.state() == Core::UpdateChecker::State::Download
		) | rpl::map([](bool downloading) {
			return !downloading;
		});
	}
	auto options = (Ui::SlideWrap<Ui::VerticalLayout>*)nullptr;
	auto install = (Ui::SettingsButton*)nullptr;
	auto check = (Ui::SettingsButton*)nullptr;
	builder.scope([&] {
		install = (cAlphaVersion() || Core::BuildIsCanary || KSandbox::isInside())
			? nullptr
			: builder.addButton({
				.id = u"main/updates/install_beta"_q,
				.altIds = { u"advanced/install_beta"_q },
				.title = tr::lng_settings_install_beta(),
				.st = &st::settingsButtonNoIcon,
				.toggled = rpl::single(cInstallBetaVersion()),
				.keywords = { u"beta"_q, u"update"_q, u"version"_q },
			});

		check = builder.addButton({
			.id = u"main/updates/check_update"_q,
			.altIds = { u"advanced/check_update"_q },
			.title = tr::lng_settings_check_now(),
			.st = &st::settingsButtonNoIcon,
			.onClick = [] {
				Core::UpdateChecker().checkNow();
			},
			.keywords = { u"check"_q, u"update"_q, u"version"_q },
		});
	}, std::move(optionsShown), [&](auto wrap) {
		options = wrap;
	});

	if (check && container) {
		const auto update = Ui::CreateChild<Settings::RowButton>(
			check,
			tr::lng_update_telegram(),
			st::settingsUpdate);
		update->hide();
		check->widthValue() | rpl::on_next([=](int width) {
			update->resizeToWidth(width);
			update->moveToLeft(0, 0);
		}, update->lifetime());

		const auto showDownloadProgress = [=](
				int64 ready,
				int64 total,
				bool preferPercent) {
			const auto formatted = [&] {
				if (!preferPercent) {
					return Ui::FormatDownloadText(ready, total);
				}
				const auto percent = (total > 0)
					? std::clamp((ready * 100) / float64(total), 0., 100.)
					: 0.;
				auto result = QString::number(percent, 'f', 2);
				if (result.contains('.')) {
					while (result.endsWith('0')) {
						result.chop(1);
					}
					if (result.endsWith('.')) {
						result.chop(1);
					}
				}
				return result + '%';
			}();
			texts->fire(tr::lng_settings_downloading_update(
				tr::now,
				lt_progress,
				formatted));
			downloading->fire(true);
		};
		const auto setDefaultStatus = [=](
				const Core::UpdateChecker &checker) {
			using State = Core::UpdateChecker::State;
			const auto state = checker.state();
			switch (state) {
			case State::Download:
				showDownloadProgress(
					checker.already(),
					checker.size(),
					checker.percent());
				break;
			case State::Ready:
				texts->fire(tr::lng_settings_update_ready(tr::now));
				update->show();
				break;
			default:
				texts->fire_copy(version);
				break;
			}
		};

		toggle->toggledValue(
		) | rpl::filter([](bool toggled) {
			return (toggled != cAutoUpdate());
		}) | rpl::on_next([=](bool toggled) {
			cSetAutoUpdate(toggled);
			Local::writeSettings();
			Core::UpdateChecker checker;
			if (cAutoUpdate()) {
				checker.start();
			} else {
				checker.stop();
				options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
				downloading->fire(false);
				setDefaultStatus(checker);
			}
		}, toggle->lifetime());

		if (install) {
			install->toggledValue(
			) | rpl::filter([](bool toggled) {
				return (toggled != cInstallBetaVersion());
			}) | rpl::on_next([=](bool toggled) {
				cSetInstallBetaVersion(toggled);
				Core::Launcher::Instance().writeInstallBetaVersionsSetting();
				Core::UpdateChecker checker;
				checker.stop();
				if (toggled) {
					cSetLastUpdateCheck(0);
				}
				checker.start();
			}, toggle->lifetime());
		}

		Core::UpdateChecker checker;

		checker.checking() | rpl::on_next([=] {
			options->setAttribute(Qt::WA_TransparentForMouseEvents);
			texts->fire(tr::lng_settings_update_checking(tr::now));
			downloading->fire(false);
		}, options->lifetime());
		checker.isLatest() | rpl::on_next([=] {
			options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
			texts->fire(tr::lng_settings_latest_installed(tr::now));
			downloading->fire(false);
		}, options->lifetime());
		checker.progress(
		) | rpl::on_next([=](Core::UpdateChecker::Progress progress) {
			showDownloadProgress(
				progress.already,
				progress.size,
				progress.percent);
		}, options->lifetime());
		checker.failed() | rpl::on_next([=] {
			options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
			texts->fire(tr::lng_settings_update_fail(tr::now));
			downloading->fire(false);
		}, options->lifetime());
		checker.ready() | rpl::on_next([=] {
			options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
			texts->fire(tr::lng_settings_update_ready(tr::now));
			update->show();
			downloading->fire(false);
		}, options->lifetime());

		setDefaultStatus(checker);

		update->setClickedCallback([] {
			if (!Core::UpdaterDisabled()) {
				Core::checkReadyUpdate();
			}
			Core::Restart();
		});
	}

	builder.addSkip();
}

bool HasUpdate() {
	return !Core::UpdaterDisabled();
}

void SetupUpdate(not_null<Ui::VerticalLayout*> container) {
	if (!HasUpdate()) {
		return;
	}

	const auto texts = Ui::CreateChild<rpl::event_stream<QString>>(
		container.get());
	const auto downloading = Ui::CreateChild<rpl::event_stream<bool>>(
		container.get());
	const auto version = tr::lng_settings_current_version(
		tr::now,
		lt_version,
		currentVersionText());
	const auto toggle = container->add(object_ptr<Settings::RowButton>(
		container,
		tr::extras_AutoCheckUpdates(),
		st::settingsUpdateToggle));
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		toggle,
		texts->events(),
		st::settingsUpdateState);

	const auto options = container->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			container,
			object_ptr<Ui::VerticalLayout>(container)));
	const auto inner = options->entity();
	const auto install = (cAlphaVersion() || Core::BuildIsCanary || KSandbox::isInside())
		? nullptr
		: inner->add(object_ptr<Settings::RowButton>(
			inner,
			tr::lng_settings_install_beta(),
			st::settingsButtonNoIcon));

	const auto check = inner->add(object_ptr<Settings::RowButton>(
		inner,
		tr::lng_settings_check_now(),
		st::settingsButtonNoIcon));
	const auto update = Ui::CreateChild<Settings::RowButton>(
		check,
		tr::lng_update_telegram(),
		st::settingsUpdate);
	update->hide();
	check->widthValue() | rpl::on_next([=](int width) {
		update->resizeToWidth(width);
		update->moveToLeft(0, 0);
	}, update->lifetime());

	rpl::combine(
		toggle->widthValue(),
		label->widthValue()
	) | rpl::on_next([=] {
		label->moveToLeft(
			st::settingsUpdateStatePosition.x(),
			st::settingsUpdateStatePosition.y());
	}, label->lifetime());
	label->setAttribute(Qt::WA_TransparentForMouseEvents);

	const auto showDownloadProgress = [=](
			int64 ready,
			int64 total,
			bool preferPercent) {
		const auto formatted = [&] {
			if (!preferPercent) {
				return Ui::FormatDownloadText(ready, total);
			}
			const auto percent = (total > 0)
				? std::clamp((ready * 100) / float64(total), 0., 100.)
				: 0.;
			auto result = QString::number(percent, 'f', 2);
			if (result.contains('.')) {
				while (result.endsWith('0')) {
					result.chop(1);
				}
				if (result.endsWith('.')) {
					result.chop(1);
				}
			}
			return result + '%';
		}();
		texts->fire(tr::lng_settings_downloading_update(
			tr::now,
			lt_progress,
			formatted));
		downloading->fire(true);
	};
	const auto setDefaultStatus = [=](const Core::UpdateChecker &checker) {
		using State = Core::UpdateChecker::State;
		const auto state = checker.state();
		switch (state) {
		case State::Download:
			showDownloadProgress(
				checker.already(),
				checker.size(),
				checker.percent());
			break;
		case State::Ready:
			texts->fire(tr::lng_settings_update_ready(tr::now));
			update->show();
			break;
		default:
			texts->fire_copy(version);
			break;
		}
	};

	toggle->toggleOn(rpl::single(cAutoUpdate()));
	toggle->toggledValue(
	) | rpl::filter([](bool toggled) {
		return (toggled != cAutoUpdate());
	}) | rpl::on_next([=](bool toggled) {
		cSetAutoUpdate(toggled);

		Local::writeSettings();
		Core::UpdateChecker checker;
		if (cAutoUpdate()) {
			checker.start();
		} else {
			checker.stop();
			options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
			downloading->fire(false);
			setDefaultStatus(checker);
		}
	}, toggle->lifetime());

	if (install) {
		install->toggleOn(rpl::single(cInstallBetaVersion()));
		install->toggledValue(
		) | rpl::filter([](bool toggled) {
			return (toggled != cInstallBetaVersion());
		}) | rpl::on_next([=](bool toggled) {
			cSetInstallBetaVersion(toggled);
			Core::Launcher::Instance().writeInstallBetaVersionsSetting();

			Core::UpdateChecker checker;
			checker.stop();
			if (toggled) {
				cSetLastUpdateCheck(0);
			}
			checker.start();
		}, toggle->lifetime());
	}

	Core::UpdateChecker checker;
	options->toggleOn(downloading->events_starting_with(
		checker.state() == Core::UpdateChecker::State::Download
	) | rpl::map([](bool downloading) {
		return !downloading;
	}));

	checker.checking() | rpl::on_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents);
		texts->fire(tr::lng_settings_update_checking(tr::now));
		downloading->fire(false);
	}, options->lifetime());
	checker.isLatest() | rpl::on_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(tr::lng_settings_latest_installed(tr::now));
		downloading->fire(false);
	}, options->lifetime());
	checker.progress(
	) | rpl::on_next([=](Core::UpdateChecker::Progress progress) {
		showDownloadProgress(
			progress.already,
			progress.size,
			progress.percent);
	}, options->lifetime());
	checker.failed() | rpl::on_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(tr::lng_settings_update_fail(tr::now));
		downloading->fire(false);
	}, options->lifetime());
	checker.ready() | rpl::on_next([=] {
		options->setAttribute(Qt::WA_TransparentForMouseEvents, false);
		texts->fire(tr::lng_settings_update_ready(tr::now));
		update->show();
		downloading->fire(false);
	}, options->lifetime());

	setDefaultStatus(checker);

	check->addClickHandler([] {
		Core::UpdateChecker().checkNow();
	});
	update->addClickHandler([] {
		if (!Core::UpdaterDisabled()) {
			Core::checkReadyUpdate();
		}
		Core::Restart();
	});
}

} // namespace Settings
