#include "settings/sections/settings_file_confirmations.h"

#include "settings/settings_common.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "settings/sections/settings_privacy_security.h"
#include "settings/settings_builder.h"
#include "settings/settings_card_layout.h"
#include "settings/settings_common_session.h"
#include "ui/boxes/dialog_layout.h"
#include "ui/layers/generic_box.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Settings {
namespace {

void SaveExtensions(not_null<Ui::InputField*> field) {
	const auto extensions = field->getLastText()
		.mid(0, 10240)
		.split(' ', Qt::SkipEmptyParts)
		.mid(0, 1024);
	auto values = base::flat_set<QString>(extensions.begin(), extensions.end());
	const auto settings = &Core::App().settings();
	if (values == settings->noWarningExtensions()) {
		return;
	}
	settings->setNoWarningExtensions(std::move(values));
	Core::App().saveSettingsDelayed();
}

not_null<Ui::InputField*> SetupFileConfirmationsContent(
		not_null<Ui::VerticalLayout*> root,
		bool dialog) {
	const auto addGroup = [&]() -> not_null<Ui::VerticalLayout*> {
		if (dialog) {
			return root->add(object_ptr<Ui::VerticalLayout>(root),
				QMargins(0, 0, 0, st::boxMediumSkip));
		}
		return AddCardGroup(root);
	};
	const auto settings = &Core::App().settings();
	const auto &list = settings->noWarningExtensions();
	const auto text = QStringList(begin(list), end(list)).join(' ');

	AddCardTitle(root, tr::lng_settings_edit_extensions());
	const auto extensionsGroup = addGroup();
	const auto extensions = extensionsGroup->add(
		object_ptr<Ui::InputField>(
			extensionsGroup,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_settings_edit_extensions(),
			TextWithTags{ text }),
		dialog
			? QMargins(st::boxRowPadding.left(), st::boxLittleSkip,
				st::boxRowPadding.right(), st::boxLittleSkip)
			: QMargins(14, 12, 14, 16));
	extensions->setObjectName(u"privacy/extensions"_q);
	extensions->setInputMethodHints(Qt::ImhLatinOnly
		| Qt::ImhNoAutoUppercase
		| Qt::ImhNoPredictiveText);
	extensions->focusedChanges(
	) | rpl::filter(rpl::mappers::_1 == false) | rpl::on_next([=] {
		SaveExtensions(extensions);
	}, extensions->lifetime());
	AddCardDescription(root, tr::lng_settings_edit_extensions_about());

	const auto ipGroup = addGroup();
	const auto ip = ipGroup->add(object_ptr<Settings::RowButton>(
		ipGroup,
		tr::lng_settings_edit_ip_confirm(),
		dialog
			? Ui::DialogButtonStyle(ipGroup, st::settingsButtonNoIcon)
			: st::settingsButtonNoIcon
	))->toggleOn(rpl::single(settings->ipRevealWarning()));
	ip->setObjectName(u"privacy/ipRevealWarning"_q);
	ip->toggledChanges() | rpl::on_next([=](bool enabled) {
		settings->setIpRevealWarning(enabled);
		Core::App().saveSettingsDelayed();
	}, ip->lifetime());
	AddCardDescription(root, tr::lng_settings_edit_ip_confirm_about());
	return extensions;
}

class FileConfirmations final : public Section<FileConfirmations> {
public:
	FileConfirmations(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
	: Section(parent, controller) {
		const auto page = Ui::CreateChild<CardPage>(this);
		_extensions = SetupFileConfirmationsContent(page->content(), false).get();
		Ui::ResizeFitChild(this, page);
	}

	rpl::producer<QString> title() override {
		return tr::lng_settings_file_confirmations();
	}
	void sectionSaveChanges(FnMut<void()> done) override {
		SaveExtensions(_extensions);
		done();
	}

private:
	Ui::InputField *_extensions = nullptr;
};

const auto kMeta = Builder::BuildHelper({
	.id = FileConfirmations::Id(),
	.parentId = PrivacySecurityId(),
	.title = &tr::lng_settings_file_confirmations,
	.icon = &st::menuIconFile,
}, [](Builder::SectionBuilder &) {});

} // namespace

Type FileConfirmationsId() {
	return FileConfirmations::Id();
}

void ShowFileConfirmations(not_null<Window::SessionController*> controller) {
	controller->show(Box([](not_null<Ui::GenericBox*> box) {
		box->setObjectName(u"settings.fileConfirmations"_q);
		box->setTitle(tr::lng_settings_file_confirmations());
		box->setWidth(st::boxWideWidth);
		const auto content = box->verticalLayout();
		Ui::AddSkip(content, st::boxPadding.top());
		const auto extensions = SetupFileConfirmationsContent(content, true);
		Ui::AddSkip(content, st::boxPadding.bottom());
		box->setFocusCallback([=] { extensions->setFocus(); });
		box->boxClosing() | rpl::on_next([=] {
			SaveExtensions(extensions);
		}, box->lifetime());
	}));
}

} // namespace Settings
