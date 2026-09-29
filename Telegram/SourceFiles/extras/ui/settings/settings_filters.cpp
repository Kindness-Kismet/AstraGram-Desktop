#include "extras/ui/settings/settings_filters.h"

#include "lang_auto.h"
#include "extras/extras_settings.h"
#include "extras/data/extras_database.h"
#include "extras/features/filters/filters_cache_controller.h"
#include "extras/ui/boxes/import_filters_box.h"
#include "extras/ui/settings/extras_builder.h"
#include "extras/ui/settings/settings_main.h"
#include "extras/utils/telegram_helpers.h"
#include "boxes/abstract_box.h"
#include "boxes/peer_list_box.h"
#include "core/application.h"
#include "filters/per_dialog_filter.h"
#include "filters/settings_filters_list.h"
#include "inline_bots/bot_attach_web_view.h"
#include "settings/settings_builder.h"
#include "settings/settings_common.h"
#include "styles/style_extras_icons.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/vertical_list.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_controller.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"

namespace Settings {

using namespace Builder;
using namespace ExtrasBuilder;

namespace {

void BuildFiltersSettings(SectionBuilder &builder) {
	auto *settings = &ExtrasSettings::getInstance();

	builder.addSkip();
	builder.addSubsectionTitle(tr::extras_RegexFilters());

	const auto enabledButton = builder.addButton({
		.id = u"extras/filtersEnabled"_q,
		.title = tr::extras_RegexFiltersEnable(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filtersEnabled()),
	});
	if (enabledButton) {
		enabledButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->filtersEnabled());
		}) | on_next([=](bool enabled) {
			ExtrasSettings::getInstance().setFiltersEnabled(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, enabledButton->lifetime());
	}

	const auto sharedButton = builder.addButton({
		.id = u"extras/filtersEnabledInChats"_q,
		.altIds = { u"extras/filtersInChats"_q },
		.title = tr::extras_RegexFiltersEnableSharedInChats(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->filtersEnabledInChats()),
	});
	if (sharedButton) {
		sharedButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->filtersEnabledInChats());
		}) | on_next([=](bool enabled) {
			ExtrasSettings::getInstance().setFiltersEnabledInChats(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, sharedButton->lifetime());
	}

	const auto blockedButton = builder.addButton({
		.id = u"extras/hideFromBlocked"_q,
		.title = tr::extras_FiltersHideFromBlocked(),
		.st = &st::settingsButtonNoIcon,
		.toggled = rpl::single(settings->hideFromBlocked()),
	});
	if (blockedButton) {
		blockedButton->toggledValue(
		) | rpl::filter([=](bool enabled) {
			return (enabled != settings->hideFromBlocked());
		}) | on_next([=](bool enabled) {
			ExtrasSettings::getInstance().setHideFromBlocked(enabled);
			FiltersCacheController::rebuildCache();
			FiltersCacheController::fireUpdate();
		}, blockedButton->lifetime());
	}

	builder.addSkip();
}

void BuildShared(SectionBuilder &builder) {
	builder.addDivider();
	builder.addSkip();

	const auto controller = builder.controller();
	builder.addButton({
		.id = u"extras/sharedFilters"_q,
		.title = tr::extras_RegexFiltersShared(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->dialogId = std::nullopt;
			controller->showExclude = false;
			controller->showSettings(ExtrasFiltersList::Id());
		},
	});
}

void BuildShadowBan(SectionBuilder &builder) {
	const auto controller = builder.controller();

	builder.addButton({
		.id = u"extras/shadowBanIds"_q,
		.altIds = { u"extras/shadowBanList"_q },
		.title = tr::extras_FiltersShadowBan(),
		.st = &st::settingsButtonNoIcon,
		.onClick = [=] {
			controller->dialogId = std::nullopt;
			controller->showExclude = false;
			controller->shadowBan = true;
			controller->showSettings(ExtrasFiltersList::Id());
		},
	});
}

void BuildPerDialog(SectionBuilder &builder) {
	builder.add([](const BuildContext &ctx) {
		v::match(ctx, [&](const WidgetContext &wctx) {
			if (!Database::hasPerDialogFilters()) {
				return;
			}

			const auto container = wctx.container;
			const auto controller = wctx.controller;

			AddSkip(container);
			AddDivider(container);

			auto ctrl = container->lifetime().make_state<PerDialogFiltersListController>(
				&controller->session(),
				controller);

			auto list = object_ptr<Ui::PaddingWrap<PeerListContent>>(
				container,
				object_ptr<PeerListContent>(
					container,
					ctrl),
				QMargins(0, -st::peerListBox.padding.top(), 0, -st::peerListBox.padding.bottom()));
			AddSkip(container);
			const auto content = container->add(std::move(list));
			AddSkip(container);
			auto delegate = container->lifetime().make_state<PeerListContentDelegateSimple>();
			delegate->setContent(content->entity());
			ctrl->setDelegate(delegate);
		}, [&](const SearchContext &) {
		});
	});
}

const auto kMeta = BuildHelper({
	.id = ExtrasFilters::Id(),
	.parentId = ExtrasMain::Id(),
	.title = &tr::extras_CategoryFilters,
	.icon = &st::menuIconTagFilter,
}, [](SectionBuilder &builder) {
	if (!ExtrasSettings::getInstance().devFeaturesEnabled()) {
		return;
	}
	BuildFiltersSettings(builder);
	BuildShared(builder);
	BuildShadowBan(builder);
	BuildPerDialog(builder);
});

} // namespace

rpl::producer<QString> ExtrasFilters::title() {
	return tr::extras_CategoryFilters();
}

void ExtrasFilters::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	addAction(
		tr::extras_FiltersMenuSelectChat(tr::now),
		[=] {
			const auto window = Core::App().activeWindow();
			const auto controller = window
				? window->sessionController()
				: nullptr;
			if (!controller) {
				return;
			}
			auto types = InlineBots::PeerTypes();
			types |= InlineBots::PeerType::Bot;
			types |= InlineBots::PeerType::Group;
			types |= InlineBots::PeerType::Broadcast;

			Window::ShowChooseRecipientBox(
				controller,
				[=](not_null<Data::Thread*> thread) {
					const auto peer = thread->peer();
					controller->dialogId = getDialogIdFromPeer(peer);
					controller->showExclude = true;
					controller->showSettings(ExtrasFiltersList::Id());
					return true;
				},
				tr::extras_FiltersMenuSelectChat(),
				nullptr,
				types);
		},
		&st::menuIconSearch);
	addAction({ .isSeparator = true });
	addAction(
		tr::extras_FiltersMenuImport(tr::now),
		[=] {
			auto box = Box(Ui::FillImportFiltersBox, true);
			Ui::show(std::move(box));
		},
		&st::menuIconArchive);
	if (Database::hasFilters()) {
		addAction(
			tr::extras_FiltersMenuExport(tr::now),
			[=] {
				auto box = Box(Ui::FillImportFiltersBox, false);
				Ui::show(std::move(box));
			},
			&st::menuIconUnarchive);
	}
	addAction({ .isSeparator = true });
	addAction({
		.text = tr::extras_FiltersMenuClear(tr::now),
		.handler = [=] {
			auto callback = [=](Fn<void()> &&close) {
				Database::deleteAllFilters();
				Database::deleteAllExclusions();
				FiltersCacheController::rebuildCache();
				FiltersCacheController::fireUpdate();
				close();
			};
			auto box = Ui::MakeConfirmBox({
				.text = tr::extras_FiltersClearPopupText(),
				.confirmed = callback,
				.confirmText = tr::extras_FiltersClearPopupActionText(),
				.confirmStyle = &st::attentionBoxButton,
			});
			Ui::show(std::move(box));
		},
		.icon = &st::menuIconClearAttention,
		.isAttention = true,
	});
}

ExtrasFilters::ExtrasFilters(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

void ExtrasFilters::setupContent() {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

Type ExtrasFiltersId() {
	return ExtrasFilters::Id();
}

} // namespace Settings
