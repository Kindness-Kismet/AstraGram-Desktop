#ifdef _DEBUG
#include "extras/debug/commands/commands_internal.h"
#include "extras/debug/debug_login.h"
#include "api/api_common.h"
#include "boxes/about_box.h"
#include "boxes/add_contact_box.h"
#include "boxes/auto_lock_box.h"
#include "boxes/background_box.h"
#include "boxes/connection_box.h"
#include "boxes/create_poll_box.h"
#include "boxes/delete_messages_box.h"
#include "boxes/edit_todo_list_box.h"
#include "boxes/download_path_box.h"
#include "boxes/dictionaries_manager.h"
#include "boxes/peer_lists_box.h"
#include "boxes/share_box.h"
#include "countries/countries_instance.h"
#include "info/channel_statistics/boosts/giveaway/select_countries_box.h"
#include "ui/boxes/choose_font_box.h"
#include "ui/boxes/choose_language_box.h"
#include "spellcheck/spellcheck_types.h"
#include "boxes/language_box.h"
#include "boxes/passcode_box.h"
#include "boxes/peer_list_controllers.h"
#include "boxes/peers/edit_peer_info_box.h"
#include "boxes/peers/prepare_short_info_box.h"
#include "boxes/premium_preview_box.h"
#include "boxes/self_destruction_box.h"
#include "boxes/stickers_box.h"
#include "chat_helpers/emoji_sets_manager.h"
#include "data/data_poll.h"
#include "data/data_user.h"
#include "extras/ui/boxes/donate_qr_box.h"
#include "extras/ui/boxes/edit_mark_box.h"
#include "extras/ui/boxes/font_selector.h"
#include "extras/ui/boxes/import_filters_box.h"
#include "extras/ui/boxes/theme_selector_box.h"
#include "history/view/history_view_schedule_box.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "menu/menu_send.h"
#include "settings.h"
#include "ui/boxes/calendar_box.h"
#include "ui/boxes/collectible_info_box.h"
#include "ui/boxes/confirm_box.h"
#include "ui/boxes/country_select_box.h"
#include "ui/boxes/single_choice_box.h"
#include "ui/dynamic_thumbnails.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/option_list.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/fields/input_field.h"
#include "window/window_lock_widgets.h"
#include "window/window_peer_menu.h"
#include "window/window_session_controller.h"
#include "styles/style_calls.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"

namespace ExtrasDebug::Commands {
using json = nlohmann::json;
namespace {

using Controller = not_null<Window::SessionController*>;
using Factory = object_ptr<Ui::BoxContent>(*)(Controller controller);

struct Entry {
	const char *key = nullptr;
	const char *kind = nullptr;
	Factory factory = nullptr;
};

[[nodiscard]] not_null<PeerData*> samplePeer(
		Controller controller,
		const QString &key) {
	const auto session = &controller->session();
	const auto peer = simulationPeer(session, key);
	return peer ? not_null<PeerData*>(peer) : not_null<PeerData*>(session->user());
}

// 左下角辅助按钮与危险操作的组合，覆盖公共底栏的完整布局。
void resetDemoBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(u"恢复默认设置"_q));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(u"界面、通知和聊天设置会恢复为默认值，聊天记录不受影响。"_q),
		st::boxLabel));
	box->addButton(rpl::single(u"恢复"_q), [=] { box->closeBox(); });
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->addLeftButton(rpl::single(u"导出备份"_q), [] {});
}

// 深色对话框样式，检查标题、关闭按钮和按钮在固定深色底上的表现。
void darkDemoBox(not_null<Ui::GenericBox*> box) {
	box->setStyle(st::groupCallBox);
	box->setTitle(rpl::single(u"通话设置"_q));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(u"深色对话框沿用同一套标题、关闭按钮与底部操作区。"_q),
		st::groupCallBoxLabel));
	box->addButton(tr::lng_settings_save(), [=] { box->closeBox(); });
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

// 带输入框与复选框的表单，覆盖输入、勾选和长标题截断。
void formDemoBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(u"新建聊天文件夹，名称较长时应在关闭按钮前截断"_q));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(u"把相关的对话放在一起，查找时更方便。"_q),
		st::boxLabel));
	const auto field = box->addRow(object_ptr<Ui::InputField>(
		box,
		st::defaultInputField,
		rpl::single(u"文件夹名称"_q),
		u"日常"_q));
	box->setFocusCallback([=] { field->setFocusFast(); });
	const auto options = box->addRow(object_ptr<Ui::OptionList>(box));
	options->setObjectName(u"dialog.options"_q);
	const auto unread = options->addOption(object_ptr<Ui::Checkbox>(
		options,
		u"包括未读对话"_q,
		true,
		st::defaultBoxCheckbox));
	unread->setObjectName(u"dialog.options.unread"_q);
	const auto archived = options->addOption(object_ptr<Ui::Checkbox>(
		options,
		u"包括已归档对话"_q,
		false,
		st::defaultBoxCheckbox));
	archived->setObjectName(u"dialog.options.archived"_q);
	box->addButton(rpl::single(u"创建文件夹"_q), [=] { box->closeBox(); });
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

// 长文本与禁用项共用行布局，切换文案时验证后续行自动重排。
void optionsDemoBox(not_null<Ui::GenericBox*> box) {
	box->setTitle(rpl::single(u"选项列表"_q));
	const auto expanded = box->lifetime().make_state<rpl::variable<bool>>(true);
	const auto options = box->addRow(object_ptr<Ui::OptionList>(box));
	options->setObjectName(u"dialog.options"_q);
	const auto shortOption = options->addOption(object_ptr<Ui::Checkbox>(
		options,
		u"只显示未读对话"_q,
		true,
		st::defaultBoxCheckbox));
	shortOption->setObjectName(u"dialog.options.short"_q);
	const auto longOption = options->addOption(object_ptr<Ui::Checkbox>(
		options,
		expanded->value() | rpl::map([](bool expanded) {
			return expanded
				? u"包含所有已归档的对话，并保留尚未阅读的消息与提醒；较长的选项说明应自动换行，不能遮挡下一项。"_q
				: u"包含已归档对话"_q;
		}),
		false,
		st::defaultBoxCheckbox));
	longOption->setObjectName(u"dialog.options.long"_q);
	const auto disabled = options->addOption(object_ptr<Ui::Checkbox>(
		options,
		u"此选项暂不可用"_q,
		false,
		st::defaultBoxCheckbox));
	disabled->setObjectName(u"dialog.options.disabled"_q);
	disabled->setDisabled(true);
	box->addLeftButton(rpl::single(u"切换长文案"_q), [=] {
		*expanded = !expanded->current();
	});
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
}

// 不可关闭的对话框，公共关闭按钮应隐藏。
void lockedDemoBox(not_null<Ui::GenericBox*> box) {
	box->setCloseByEscape(false);
	box->setCloseByOutsideClick(false);
	box->setTitle(rpl::single(u"正在迁移数据"_q));
	box->addRow(object_ptr<Ui::FlatLabel>(
		box,
		rpl::single(u"迁移完成前不能关闭，完成后点击下方按钮继续。"_q),
		st::boxLabel));
	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });
}

[[nodiscard]] const std::vector<Entry> &entries() {
	static const auto result = std::vector<Entry>{
		{ "confirm", "confirm", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Ui::MakeConfirmBox({
				.text = u"归档后，对话仍可在归档列表中找到，有新消息时也可以继续收到提醒。"_q,
			});
		} },
		{ "confirm-title", "confirm", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Ui::MakeConfirmBox({
				.text = u"归档后，对话仍可在归档列表中找到。"_q,
				.confirmText = u"归档对话"_q,
				.title = u"归档这些对话？"_q,
			});
		} },
		{ "confirm-danger", "confirm", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Ui::MakeConfirmBox({
				.text = u"删除后无法恢复，对方也会同时删除这些消息。"_q,
				.confirmText = tr::lng_box_delete(tr::now),
				.confirmStyle = &st::attentionBoxButton,
				.title = u"删除 3 条消息？"_q,
			});
		} },
		{ "inform", "confirm", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Ui::MakeInformBox(u"文件过大，无法发送。"_q);
		} },
		{ "form", "form", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box(formDemoBox);
		} },
		{ "options", "options", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box(optionsDemoBox);
		} },
		{ "single-choice", "options", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box([](not_null<Ui::GenericBox*> box) {
				const auto options = std::vector<QString>{
					u"全部对话"_q,
					u"只保留尚未阅读的对话，同时包含已归档但仍有新消息的对话"_q,
					u"仅置顶对话"_q,
				};
				SingleChoiceBox(box, {
					.title = rpl::single(u"选择对话范围"_q),
					.options = options,
					.initialSelection = 0,
					.callback = [](int) {},
				});
			});
		} },
		{ "todo-list", "form", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<EditTodoListBox>(
				controller,
				rpl::single(0),
				Api::SendType::Normal,
				SendMenu::Details());
		} },
		{ "reset", "form", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box(resetDemoBox);
		} },
		{ "locked", "form", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box(lockedDemoBox);
		} },
		{ "dark", "dark", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box(darkDemoBox);
		} },
		{ "about", "info", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box(AboutBox, controller.get());
		} },
		{ "language", "list", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<LanguageBox>(controller.get());
		} },
		{ "auto-lock", "options", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box<AutoLockBox>();
		} },
		{ "download-path", "options", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<DownloadPathBox>(controller);
		} },
		{ "self-destruction", "options", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<SelfDestructionBox>(
				&controller->session(),
				SelfDestructionBox::Type::Account,
				rpl::single(365));
		} },
		{ "passcode", "form", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<PasscodeBox>(&controller->session(), false);
		} },
		{ "proxies", "list", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return ProxiesBoxController::CreateOwningBox(
				&controller->session().account());
		} },
		{ "edit-name", "form", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<EditNameBox>(controller->session().user());
		} },
		{ "add-contact", "form", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<AddContactBox>(&controller->session());
		} },
		{ "new-group", "form", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<GroupInfoBox>(controller, GroupInfoBox::Type::Group);
		} },
		{ "new-channel", "form", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<GroupInfoBox>(controller, GroupInfoBox::Type::Channel);
		} },
		{ "calendar", "picker", [](Controller) -> object_ptr<Ui::BoxContent> {
			const auto today = QDate::currentDate();
			return Box<Ui::CalendarBox>(Ui::CalendarBoxArgs{
				.month = today,
				.highlighted = today,
				.callback = [](QDate, Fn<void()> close) { close(); },
			});
		} },
		{ "country", "list", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box<Ui::CountrySelectBox>();
		} },
		{ "giveaway-countries", "list", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box(Ui::SelectCountriesBox,
				std::vector<QString>(), [](std::vector<QString>) {},
				[](int) { return false; }, Countries::Naming::Default);
		} },
		{ "translate-language", "list", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box(Ui::ChooseLanguageBox, tr::lng_dlg_filter(),
				[](std::vector<LanguageId>) {}, std::vector<LanguageId>(),
				true, [](LanguageId) { return true; });
		} },
		{ "official-font", "list", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box(Ui::ChooseFontBox, [] {
				auto image = QImage(240, 120, QImage::Format_ARGB32_Premultiplied);
				image.fill(st::boxBg->c);
				return image;
			}, QString(), [](QString) {});
		} },
#ifndef TDESKTOP_DISABLE_SPELLCHECK
		{ "dictionary", "list", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<Ui::ManageDictionariesBox>(&controller->session());
		} },
#endif
		{ "peer-lists", "list", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			auto lists = std::vector<std::unique_ptr<PeerListController>>();
			lists.push_back(std::make_unique<ContactsBoxController>(&controller->session()));
			lists.push_back(std::make_unique<ContactsBoxController>(&controller->session()));
			return Box<PeerListsBox>(std::move(lists), [](not_null<PeerListsBox*>) {});
		} },
		{ "share", "list", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<ShareBox>(ShareBox::Descriptor{
				.session = &controller->session(),
				.submitCallback = [](auto &&, auto, auto &&, auto, auto) {},
				.filterCallback = [](not_null<Data::Thread*>) { return true; },
			});
		} },
		{ "terms", "info", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box<Window::TermsBox>(
				TextWithEntities{ u"继续使用前，请阅读并同意更新后的服务条款。条款说明了账号、隐私和内容使用的基本规则。"_q },
				tr::lng_box_ok(),
				tr::lng_cancel());
		} },
		{ "stickers", "list", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<StickersBox>(
				controller->uiShow(),
				StickersBox::Section::Installed);
		} },
		{ "emoji-sets", "list", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<Ui::Emoji::ManageSetsBox>(&controller->session());
		} },
		{ "background", "picker", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<BackgroundBox>(controller);
		} },
		{ "contacts", "list", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return PrepareContactsBox(controller);
		} },
		{ "choose-recipient", "list", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Window::PrepareChooseRecipientBox(
				&controller->session(),
				[](not_null<Data::Thread*>) { return true; });
		} },
		{ "clear-history", "confirm", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<DeleteMessagesBox>(samplePeer(controller, u"group"_q), true);
		} },
		{ "short-info", "custom", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return PrepareShortInfoBox(
				samplePeer(controller, u"private"_q),
				controller);
		} },
		{ "edit-group", "form", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<EditPeerInfoBox>(
				controller,
				samplePeer(controller, u"requests"_q));
		} },
		{ "schedule", "picker", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return HistoryView::PrepareScheduleBox(
				controller.get(),
				controller->uiShow(),
				SendMenu::Details(),
				[](Api::SendOptions) {});
		} },
		{ "poll", "form", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<CreatePollBox>(
				controller,
				samplePeer(controller, u"group"_q),
				PollData::Flags(),
				PollData::Flags(),
				rpl::single(0),
				Api::SendType::Normal,
				SendMenu::Details());
		} },
		{ "collectible", "custom", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			const auto user = controller->session().user();
			return Box(Ui::CollectibleInfoBox, Ui::CollectibleInfo{
				.entity = u"@astragram"_q,
				.copyText = u"https://t.me/astragram"_q,
				.ownerUserpic = Ui::MakeUserpicThumbnail(user),
				.ownerName = user->name(),
				.cryptoAmount = 1000000000,
				.amount = 520,
				.cryptoCurrency = u"TON"_q,
				.currency = u"USD"_q,
				.url = u"https://fragment.com"_q,
				.date = TimeId(1700000000),
			});
		} },
		{ "theme-selector", "list", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<ThemeSelectorBox>(controller);
		} },
		{ "font-selector", "list", [](Controller controller) -> object_ptr<Ui::BoxContent> {
			return Box<ExtrasUi::FontSelectorBox>(
				controller.get(),
				[](QString) {});
		} },
		{ "edit-mark", "form", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box<EditMarkBox>(
				rpl::single(u"已删除标记"_q),
				u"🗑"_q,
				u"🗑"_q,
				[](const QString &) {});
		} },
		{ "import-filters", "form", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box(Ui::FillImportFiltersBox, true);
		} },
		{ "donate-qr", "custom", [](Controller) -> object_ptr<Ui::BoxContent> {
			return Box(
				Ui::fillDonateQrBox,
				u"https://example.com/donate"_q,
				u":/gui/icons/extras/donates/support_logo.svg"_q);
		} },
	};
	return result;
}

[[nodiscard]] const Entry *findEntry(const QString &key) {
	for (const auto &entry : entries()) {
		if (key == QLatin1String(entry.key)) {
			return &entry;
		}
	}
	return nullptr;
}

[[nodiscard]] Window::SessionController *simulationController() {
	const auto session = ActiveSession();
	if (!cDebugProfile() || !session || !isSimulationSession(session)) {
		return nullptr;
	}
	return session->tryResolveWindow();
}

Result listDialogs(const QStringList &args) {
	if (!args.isEmpty()) {
		return Result::Err(u"usage: dialog.list"_q);
	}
	auto list = json::array();
	for (const auto &entry : entries()) {
		list.push_back({ { "key", entry.key }, { "kind", entry.kind } });
	}
	// 会员预览需要独立入口：列表版与封面翻页版各一个。
	list.push_back({ { "key", "premium-preview" }, { "kind", "custom" } });
	list.push_back({ { "key", "premium-feature" }, { "kind", "custom" } });
	return Result::Ok(Compact(json{ { "dialogs", list } }));
}

Result openDialog(const QStringList &args) {
	if (args.size() != 1) {
		return Result::Err(u"usage: dialog.open <key>; see dialog.list"_q);
	}
	const auto controller = simulationController();
	if (!controller) {
		return Result::Err(u"an isolated simulation profile with a window is required"_q);
	}
	const auto &key = args.front();
	controller->hideLayer(anim::type::instant);
	if (key == u"premium-preview"_q || key == u"premium-feature"_q) {
		ShowPremiumPreviewBox(
			controller,
			(key == u"premium-preview"_q)
				? PremiumFeature::Stories
				: PremiumFeature::FasterDownload);
		return Result::Ok(Compact(json{ { "key", key.toStdString() } }));
	}
	const auto entry = findEntry(key);
	if (!entry) {
		return Result::Err(u"unknown dialog; see dialog.list"_q);
	}
	controller->show(
		entry->factory(controller),
		Ui::LayerOption::KeepOther,
		anim::type::instant);
	return Result::Ok(Compact(json{
		{ "key", entry->key },
		{ "kind", entry->kind },
	}));
}

Result closeDialogs(const QStringList &args) {
	if (!args.isEmpty()) {
		return Result::Err(u"usage: dialog.close"_q);
	}
	const auto controller = simulationController();
	if (!controller) {
		return Result::Err(u"an isolated simulation profile with a window is required"_q);
	}
	controller->hideLayer(anim::type::instant);
	return Result::Ok();
}

} // namespace

const HandlerMap &dialogHandlers() {
	static const auto result = HandlerMap{
		{ u"dialog.list"_q, &listDialogs },
		{ u"dialog.open"_q, &openDialog },
		{ u"dialog.close"_q, &closeDialogs },
	};
	return result;
}

} // namespace ExtrasDebug::Commands
#endif
