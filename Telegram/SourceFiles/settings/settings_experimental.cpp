/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "settings/settings_experimental.h"
#include "settings/settings_card_layout.h"

#include "settings/settings_common.h"
#include "data/components/passkeys.h"
#include "ui/layers/generic_box.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_entity.h"
#include "ui/toast/toast.h"
#include "ui/widgets/menu/menu_add_action_callback.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/kinetic_scroller.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/vertical_list.h"
#include "ui/gl/gl_detection.h"
#include "ui/chat/chat_style_radius.h"
#include "ui/controls/compose_ai_button_factory.h"
#include "base/options.h"
#include "boxes/moderate_messages_box.h"
#include "core/application.h"
#include "core/launcher.h"
#include "core/sandbox.h"
#include "chat_helpers/tabbed_panel.h"
#include "dialogs/dialogs_entry.h"
#include "dialogs/dialogs_widget.h"
#include "dialogs/ui/dialogs_layout.h"
#include "ffmpeg/ffmpeg_utility.h"
#include "history/history_item_components.h"
#include "history/view/controls/compose_controls_common.h"
#include "history/view/history_view_message.h"
#include "info/profile/info_profile_actions.h"
#include "info/profile/tabs/adapters/info_profile_tab_media.h"
#include "info/profile/tabs/info_profile_tabs_host.h"
#include "lang/lang_keys.h"
#include "mainwindow.h"
#include "mainwidget.h"
#include "media/player/media_player_instance.h"
#include "mtproto/session_private.h"
#include "webview/webview_embed.h"
#include "window/main_window.h"
#include "window/window_filters_favorite.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "window/window_controller.h"
#include "window/notifications_manager.h"
#include "info/info_flexible_scroll.h"
#include "chat_helpers/stickers_list_widget.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_settings.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QJsonDocument>
#include <QtGui/QGuiApplication>

// AyuGram includes
#include "extras/ui/settings/settings_main.h"
#include "settings/settings_builder.h"


namespace Settings {
namespace {

const auto kOptionsClipboardPrefix = u"tdesktop-flags:"_q;

struct DecodeOptionsResult {
	bool ok = false;
	QString json;
};

struct ExperimentalOption {
	const char *id;
	tr::phrase<> title;
	const tr::phrase<> *description = nullptr;
};

struct ResolvedReferrer {
	QString controlId;
	Type section = ExtrasMain::Id();
};

[[nodiscard]] QString EncodeOptionsToText(const QString &json) {
	const auto flags = QByteArray::Base64UrlEncoding
		| QByteArray::OmitTrailingEquals;
	return kOptionsClipboardPrefix
		+ qs(qCompress(json.toLatin1(), 9).toBase64(flags));
}

[[nodiscard]] DecodeOptionsResult DecodeOptionsFromText(const QString &text) {
	auto result = DecodeOptionsResult();
	if (!text.startsWith(kOptionsClipboardPrefix)) {
		return result;
	}
	auto encoded = QStringView(text).mid(
		kOptionsClipboardPrefix.size()).toLatin1();
	const auto compressed = QByteArray::fromBase64Encoding(
		std::move(encoded),
		QByteArray::Base64UrlEncoding
			| QByteArray::AbortOnBase64DecodingErrors);
	if (!compressed || (*compressed).isEmpty()) {
		return result;
	}
	const auto decoded = qUncompress(*compressed);
	if (decoded.isEmpty()) {
		return result;
	}

	auto error = QJsonParseError();
	const auto parsed = QJsonDocument::fromJson(decoded, &error);
	if ((error.error != QJsonParseError::NoError) || !parsed.isObject()) {
		return result;
	}
	result.ok = true;
	result.json = QString::fromUtf8(decoded);
	return result;
}

[[nodiscard]] ResolvedReferrer ResolveReferrer(
		const QString &controlId,
		not_null<Main::Session*> session) {
	const auto &registry = Builder::SearchRegistry::Instance();
	const auto entries = registry.collectAll(session);
	for (const auto &entry : entries) {
		if (!entry.section) {
			continue;
		}
		if (entry.id == controlId) {
			return {
				.controlId = entry.id,
				.section = entry.section,
			};
		}
		if (entry.altIds.contains(controlId)) {
			return {
				.controlId = entry.id,
				.section = entry.section,
			};
		}
	}
	return {
		.controlId = controlId,
	};
}

[[nodiscard]] QString OptionReferrer(const base::options::option<bool> &option) {
	const auto &id = option.id();
	if (id == u"tabbed-panel-show-on-click"_q) {
		return u"extras/showEmojiPopup"_q;
	} else if (id == u"show-peer-id-below-about"_q) {
		return u"extras/showPeerId"_q;
	} else if (id == u"unlimited-recent-stickers"_q) {
		return u"extras/unlimitedRecentStickers"_q;
	} else if (id == u"hide-ai-button"_q) {
		return u"extras/showAiEditorButtonInMessageField"_q;
	} else if (id == u"unlimited-message-width"_q) {
		return u"extras/wideMultiplier"_q;
	}
	return QString();
}

void SetupCopyDeepLink(
		not_null<Window::Controller*> window,
		not_null<Button*> button,
		const QString &id) {
	const auto link = u"tg://settings/experimental/"_q + id;
	const auto menu
		= button->lifetime().make_state<base::unique_qptr<Ui::PopupMenu>>();
	button->events(
	) | rpl::filter([](not_null<QEvent*> e) {
		return e->type() == QEvent::ContextMenu;
	}) | rpl::on_next([=](not_null<QEvent*> e) {
		*menu = base::make_unique_q<Ui::PopupMenu>(
			button,
			st::popupMenuWithIcons);
		(*menu)->addAction(tr::extras_ExperimentalCopyDeepLink(tr::now), [=] {
			TextUtilities::SetClipboardText({ link });
			window->showToast({
				.text = { tr::extras_ExperimentalDeepLinkCopied(tr::now) },
				.iconLottie = u"toast/voip_invite"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		}, &st::menuIconCopy);
		(*menu)->popup(QCursor::pos());
		e->accept();
	}, button->lifetime());
}

[[nodiscard]] not_null<Button*> AddOptionRow(
		not_null<Ui::VerticalLayout*> container,
		const QString &name,
		const QString &description,
		const style::SettingsButton &st) {
	if (description.isEmpty()) {
		return container->add(CreateButtonWithIcon(
			container,
			rpl::single(name),
			st,
			{}), style::al_justify);
	}
	const auto button = CreateButtonWithIcon(
		container,
		rpl::single(QString()),
		st,
		{}).release();
	button->setAccessibleName(name);
	auto titlePadding = st::settingsExperimentalTitlePadding;
	auto aboutPadding = st::settingsExperimentalAboutPadding;
	titlePadding.setLeft(button->st().padding.left());
	aboutPadding.setLeft(button->st().padding.left());
	const auto title = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			name,
			st::settingsExperimentalTitle),
		titlePadding);
	const auto about = container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			description,
			st::settingsExperimentalAbout),
		aboutPadding);
	title->setAttribute(Qt::WA_TransparentForMouseEvents);
	about->setAttribute(Qt::WA_TransparentForMouseEvents);
	rpl::combine(
		container->widthValue(),
		title->heightValue(),
		about->heightValue()
	) | rpl::on_next([=](int width, int titleHeight, int aboutHeight) {
		button->resize(width, titlePadding.top()
			+ titleHeight
			+ titlePadding.bottom()
			+ aboutPadding.top()
			+ aboutHeight
			+ aboutPadding.bottom());
	}, button->lifetime());
	title->topValue(
	) | rpl::on_next([=](int top) {
		button->moveToLeft(0, top - titlePadding.top());
	}, button->lifetime());
	button->show();
	return button;
}

void AddOption(
		not_null<Window::Controller*> window,
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		const ExperimentalOption &entry,
		rpl::producer<> resetClicks,
		rpl::producer<> reloadOptionsRequests,
		Fn<void(const QString&, not_null<QWidget*>)> registerHighlight) {
	auto &option = base::options::lookup<bool>(entry.id);
	const auto name = entry.title(tr::now);
	const auto description = entry.description
		? (*entry.description)(tr::now)
		: QString();

	const auto inner = container->add(
		object_ptr<Ui::VerticalLayout>(container), style::al_justify);

	auto &lifetime = inner->lifetime();
	const auto toggles = lifetime.make_state<rpl::event_stream<bool>>();
	std::move(
		resetClicks
	) | rpl::map_to(
		option.defaultValue()
	) | rpl::start_to_stream(*toggles, lifetime);
	std::move(reloadOptionsRequests) | rpl::on_next([=, &option] {
		toggles->fire_copy(option.value());
	}, lifetime);

	const auto referrer = OptionReferrer(option);
	const auto button = AddOptionRow(
		inner,
		name,
		description,
		(!referrer.isEmpty() || option.relevant())
			? st::settingsButtonNoIcon
			: st::settingsOptionDisabled);
	button->setObjectName(u"experimental/"_q + option.id());
	if (!referrer.isEmpty()) {
		button->addClickHandler([=] {
			const auto resolved = ResolveReferrer(
				referrer,
				&controller->session());
			controller->setHighlightControlId(resolved.controlId);
			controller->showSettings(resolved.section);
			window->activate();
		});
	} else {
		button->toggleOn(toggles->events_starting_with(option.value()));
	}

	if (registerHighlight) {
		registerHighlight(u"experimental/"_q + option.id(), button);
	}

	SetupCopyDeepLink(window, button, option.id());

	const auto restarter = (referrer.isEmpty()
		&& option.relevant()
		&& option.restartRequired())
		? button->lifetime().make_state<base::Timer>()
		: nullptr;
	if (restarter) {
		restarter->setCallback([=] {
			window->show(Ui::MakeConfirmBox({
				.text = tr::lng_settings_need_restart(),
				.confirmed = [] { Core::Restart(); },
				.confirmText = tr::lng_settings_restart_now(),
				.cancelText = tr::lng_settings_restart_later(),
			}));
		});
	}
	if (referrer.isEmpty()) {
		button->toggledChanges(
		) | rpl::on_next([=, &option](bool toggled) {
			if (!option.relevant() && toggled != option.defaultValue()) {
				toggles->fire_copy(option.defaultValue());
				window->showToast(
					tr::lng_settings_experimental_irrelevant(tr::now));
				return;
			}
			option.set(toggled);
			if (restarter) {
				restarter->callOnce(st::settingsButtonNoIcon.toggle.duration);
			}
		}, inner->lifetime());
	}
}

void AddFavoriteLinkButton(
		not_null<Window::Controller*> window,
		not_null<Ui::VerticalLayout*> container,
		Fn<void(const QString&, not_null<QWidget*>)> registerHighlight) {
	const auto option = &base::options::lookup<QString>(
		Window::kOptionFolderFavoriteLink);
	const auto name = tr::extras_ExperimentalFolderFavoriteLink(tr::now);

	const auto inner = container->add(
		object_ptr<Ui::VerticalLayout>(container), style::al_justify);

	auto label = rpl::single(
		rpl::empty
	) | rpl::then(
		option->changes()
	) | rpl::map([option] {
		return option->value();
	});
	const auto button = AddButtonWithLabel(
		inner,
		rpl::single(name),
		std::move(label),
		st::settingsButtonNoIcon);
	AddCardDescription(inner, tr::extras_ExperimentalFolderFavoriteLinkDescription());
	button->setClickedCallback([=] {
		window->show(Box(Window::EditFolderFavoriteLinkBox));
	});

	if (registerHighlight) {
		registerHighlight(u"experimental/"_q + option->id(), button);
	}

	SetupCopyDeepLink(window, button, option->id());
}

struct Category {
	QString title;
	std::vector<ExperimentalOption> options;
};
std::vector<Category> experimentalCategories() {
	return {
		{
			tr::extras_ExperimentalCategoryChats(tr::now),
			{
				{ Dialogs::kOptionForumHideChatsList, tr::extras_ExperimentalForumHideChatsList,
					&tr::extras_ExperimentalForumHideChatsListDescription },
				{ Dialogs::kOptionDialogsUnreadOnTop, tr::extras_ExperimentalDialogsUnreadOnTop,
					&tr::extras_ExperimentalDialogsUnreadOnTopDescription },
				{ Dialogs::Ui::kOptionDialogsMuteIcon, tr::extras_ExperimentalDialogsMuteIcon,
					&tr::extras_ExperimentalDialogsMuteIconDescription },
				{ kOptionUseNewChatView, tr::extras_ExperimentalUseNewChatView,
					&tr::extras_ExperimentalUseNewChatViewDescription },
				{ kOptionAutoScrollInactiveChat, tr::extras_ExperimentalAutoScrollInactiveChat,
					&tr::extras_ExperimentalAutoScrollInactiveChatDescription },
				{ kModerateCommonGroups, tr::extras_ExperimentalModerateCommonGroups },
				{ Info::kClassicProfileScroll, tr::extras_ExperimentalClassicProfileScroll,
					&tr::extras_ExperimentalClassicProfileScrollDescription },
			}
		},
		{
			tr::extras_ExperimentalCategoryMessages(tr::now),
			{
				{ Ui::kOptionUseSmallMsgBubbleRadius, tr::extras_ExperimentalUseSmallMsgBubbleRadius,
					&tr::extras_ExperimentalUseSmallMsgBubbleRadiusDescription },
				{ HistoryView::kOptionUnlimitedMessageWidth, tr::extras_ExperimentalUnlimitedMessageWidth,
					&tr::extras_ExperimentalUnlimitedMessageWidthDescription },
				{ HistoryView::Controls::kOptionMacCmdReplyImmediately, tr::extras_ExperimentalMacCmdReplyImmediately,
					&tr::extras_ExperimentalMacCmdReplyImmediatelyDescription },
				{ Ui::kOptionHideAiButton, tr::extras_ExperimentalHideAiButton,
					&tr::extras_ExperimentalHideAiButtonDescription },
				{ kForceComposeSearchOneColumn, tr::extras_ExperimentalForceComposeSearchOneColumn,
					&tr::extras_ExperimentalForceComposeSearchOneColumnDescription },
			}
		},
		{
			tr::extras_ExperimentalCategoryProfile(tr::now),
			{
				{ Window::kOptionViewProfileInChatsListContextMenu, tr::extras_ExperimentalViewProfileInChatsListContextMenu,
					&tr::extras_ExperimentalViewProfileInChatsListContextMenuDescription },
				{ Info::Profile::kOptionShowPeerIdBelowAbout, tr::extras_ExperimentalShowPeerIdBelowAbout,
					&tr::extras_ExperimentalShowPeerIdBelowAboutDescription },
				{ Info::Profile::kOptionShowChannelJoinedBelowAbout, tr::extras_ExperimentalShowChannelJoinedBelowAbout,
					&tr::extras_ExperimentalShowChannelJoinedBelowAboutDescription },
				{ Info::Profile::kOptionProfileMediaTabs, tr::extras_ExperimentalProfileMediaTabs,
					&tr::extras_ExperimentalProfileMediaTabsDescription },
				{ Info::Profile::kOptionProfileMediaTabsExpanded, tr::extras_ExperimentalProfileMediaTabsExpanded,
					&tr::extras_ExperimentalProfileMediaTabsExpandedDescription },
			}
		},
		{
			tr::extras_ExperimentalCategoryStickersAndEmoji(tr::now),
			{
				{ ChatHelpers::kOptionTabbedPanelShowOnClick, tr::extras_ExperimentalTabbedPanelShowOnClick,
					&tr::extras_ExperimentalTabbedPanelShowOnClickDescription },
				{ ChatHelpers::kOptionUnlimitedRecentStickers, tr::extras_ExperimentalUnlimitedRecentStickers,
					&tr::extras_ExperimentalUnlimitedRecentStickersDescription },
			}
		},
		{
			tr::extras_ExperimentalCategoryMedia(tr::now),
			{
				{ Media::Player::kOptionDisableAutoplayNext, tr::extras_ExperimentalDisableAutoplayNext,
					&tr::extras_ExperimentalDisableAutoplayNextDescription },
				{ Window::kOptionExternalMediaViewer, tr::extras_ExperimentalExternalMediaViewer,
					&tr::extras_ExperimentalExternalMediaViewerDescription },
				{ FFmpeg::kOptionFFmpegMultiThread, tr::extras_ExperimentalFFmpegMultiThread,
					&tr::extras_ExperimentalFFmpegMultiThreadDescription },
			}
		},
		{
			tr::extras_ExperimentalCategoryNotifications(tr::now),
			{
				{ Window::Notifications::kOptionHideReplyButton, tr::extras_ExperimentalHideReplyButton,
					&tr::extras_ExperimentalHideReplyButtonDescription },
				{ Window::Notifications::kOptionCustomNotification, tr::extras_ExperimentalCustomNotification,
					&tr::extras_ExperimentalCustomNotificationDescription },
				{ Window::Notifications::kOptionGNotification, tr::extras_ExperimentalGNotification,
					&tr::extras_ExperimentalGNotificationDescription },
				{ Window::Notifications::kOptionMacModernNotifications, tr::extras_ExperimentalMacModernNotifications,
					&tr::extras_ExperimentalMacModernNotificationsDescription },
			}
		},
		{
			tr::extras_ExperimentalCategoryInterface(tr::now),
			{
				{ Core::kOptionFractionalScalingEnabled, tr::extras_ExperimentalFractionalScalingEnabled,
					&tr::extras_ExperimentalFractionalScalingEnabledDescription },
				{ Core::kOptionHighDpiDownscale, tr::extras_ExperimentalHighDpiDownscale,
					&tr::extras_ExperimentalHighDpiDownscaleDescription },
				{ Ui::GL::kOptionUseQtRhi, tr::extras_ExperimentalUseQtRhi,
					&tr::extras_ExperimentalUseQtRhiDescription },
				{ Ui::GL::kOptionEnableVulkanRhi, tr::extras_ExperimentalEnableVulkanRhi,
					&tr::extras_ExperimentalEnableVulkanRhiDescription },
				{ Core::kOptionFreeType, tr::extras_ExperimentalFreeType,
					&tr::extras_ExperimentalFreeTypeDescription },
#if QT_VERSION >= QT_VERSION_CHECK(6, 0, 0)
				{ Ui::kOptionKineticScroller, tr::extras_ExperimentalKineticScroller },
#endif
				{ Window::kOptionDisableTouchbar, tr::extras_ExperimentalDisableTouchbar },
				{ Window::kOptionNewWindowsSizeAsFirst, tr::extras_ExperimentalNewWindowsSizeAsFirst,
					&tr::extras_ExperimentalNewWindowsSizeAsFirstDescription },
			}
		},
		{
			tr::extras_ExperimentalCategorySystem(tr::now),
			{
				{ MTP::details::kOptionPreferIPv6, tr::extras_ExperimentalPreferIPv6,
					&tr::extras_ExperimentalPreferIPv6Description },
				{ Core::kOptionSkipUrlSchemeRegister, tr::extras_ExperimentalSkipUrlSchemeRegister,
					&tr::extras_ExperimentalSkipUrlSchemeRegisterDescription },
				{ Core::kOptionDeadlockDetector, tr::extras_ExperimentalDeadlockDetector,
					&tr::extras_ExperimentalDeadlockDetectorDescription },
				{ Webview::kOptionWebviewDebugEnabled, tr::extras_ExperimentalWebviewDebugEnabled,
					&tr::extras_ExperimentalWebviewDebugEnabledDescription },
				{ Webview::kOptionWebviewLegacyEdge, tr::extras_ExperimentalWebviewLegacyEdge,
					&tr::extras_ExperimentalWebviewLegacyEdgeDescription },
			}
		},
	};
}

void SetupExperimental(
		not_null<Window::Controller*> window,
		not_null<Window::SessionController*> controller,
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<> reloadOptionsRequests,
		Fn<void(const QString&, not_null<QWidget*>)> registerHighlight) {
	const auto header = container->add(
		object_ptr<Ui::VerticalLayout>(container));

	header->add(
		object_ptr<Ui::FlatLabel>(
			header,
			tr::lng_settings_experimental_about(),
			st::boxLabel),
		st::settingsCardHintPadding);

	auto reset = (Button*)nullptr;
	if (base::options::changed()) {
		const auto wrap = header->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				header,
				object_ptr<Ui::VerticalLayout>(header)));
		const auto inner = AddCardGroup(wrap->entity());
		reset = inner->add(CreateButtonWithIcon(
			inner,
			tr::lng_settings_experimental_restore(),
			st::settingsButtonNoIcon,
			{}));
		reset->addClickHandler([=] {
			base::options::reset();
			wrap->hide(anim::type::normal);
		});
	}
	const auto categories = experimentalCategories();

	const auto addOption = [&](
			not_null<Ui::VerticalLayout*> inner,
			const ExperimentalOption &entry) {
		return AddOption(
			window,
			controller,
			inner,
			entry,
			(reset
				? (reset->clicks() | rpl::to_empty)
				: rpl::producer<>()),
			rpl::duplicate(reloadOptionsRequests),
			registerHighlight);
	};
	const auto addCategory = [&](
			const QString &title,
			auto &&fill) {
		AddCardTitle(container, rpl::single(title));
		const auto inner = AddCardGroup(container);
		fill(inner);
	};

	for (const auto &category : categories) {
		addCategory(category.title, [&](
				not_null<Ui::VerticalLayout*> inner) {
			for (const auto &entry : category.options) {
				addOption(inner, entry);
			}
		});
	}

	addCategory(tr::extras_ExperimentalCategoryOther(tr::now), [&](
			not_null<Ui::VerticalLayout*> inner) {
		if (base::options::lookup<bool>(kOptionFastButtonsMode).value()) {
			addOption(inner, {
				kOptionFastButtonsMode,
				tr::extras_ExperimentalFastButtonsMode,
				&tr::extras_ExperimentalFastButtonsModeDescription,
			});
		}
		AddFavoriteLinkButton(
			window,
			inner,
			registerHighlight);
	});
}

} // namespace

#ifdef _DEBUG
std::vector<not_null<base::options::details::BasicOption*>> experimentalOptionsForDebug() {
	auto result = std::vector<not_null<base::options::details::BasicOption*>>();
	for (const auto &category : experimentalCategories()) {
		for (const auto &entry : category.options) {
			if (!OptionReferrer(base::options::lookup<bool>(entry.id)).isEmpty()) {
				continue;
			}
			result.push_back(&base::options::details::Lookup(entry.id));
		}
	}
	result.push_back(&base::options::details::Lookup(kOptionFastButtonsMode));
	result.push_back(&base::options::details::Lookup(Window::kOptionFolderFavoriteLink));
	return result;
}
#endif

Experimental::Experimental(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	setupContent();
}

Experimental::~Experimental() = default;

rpl::producer<QString> Experimental::title() {
	return tr::lng_settings_experimental();
}

void Experimental::fillTopBarMenu(const Ui::Menu::MenuCallback &addAction) {
	const auto window = &controller()->window();
	addAction(
		tr::extras_ExperimentalExport(tr::now),
		[=] {
			TextUtilities::SetClipboardText(
				{ EncodeOptionsToText(base::options::serialize()) });
			window->showToast({
				.text = { tr::extras_ExperimentalExported(tr::now) },
				.iconLottie = u"toast/copy"_q,
				.iconLottieSize = st::toastLottieIconSize,
			});
		},
		&st::menuIconCopy);
	if (!DecodeOptionsFromText(QGuiApplication::clipboard()->text()).ok) {
		return;
	}
	addAction(
		tr::extras_ExperimentalImport(tr::now),
		[=] {
			const auto decoded = DecodeOptionsFromText(
				QGuiApplication::clipboard()->text());
			if (!decoded.ok) {
				window->showToast(tr::extras_ExperimentalInvalidCode(tr::now));
				return;
			}
			if (!base::options::deserialize(decoded.json)) {
				window->showToast(tr::extras_ExperimentalUnsupportedCode(tr::now));
				return;
			}
			_reloadOptionsRequests.fire({});
			window->showToast(tr::extras_ExperimentalImported(tr::now));
		},
		&st::menuIconImportTheme);
}

void Experimental::showFinished() {
	AbstractSection::showFinished();
	for (const auto &[id, widget] : _highlights) {
		if (widget) {
			controller()->checkHighlightControl(id, widget);
		}
	}
}

void Experimental::setupContent() {
	const auto page = Ui::CreateChild<CardPage>(this);
	const auto content = page->content();

	SetupExperimental(
		&controller()->window(),
		controller(),
		content,
		_reloadOptionsRequests.events(),
		[this](const QString &id, not_null<QWidget*> widget) {
			_highlights.push_back({ id, widget.get() });
		});

	Ui::ResizeFitChild(this, page);
}

} // namespace Settings
