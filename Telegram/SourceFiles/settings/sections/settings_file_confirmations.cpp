#include "settings/sections/settings_file_confirmations.h"

#include "core/application.h"
#include "core/core_settings.h"
#include "lang/lang_keys.h"
#include "settings/sections/settings_privacy_security.h"
#include "settings/settings_builder.h"
#include "settings/settings_card_layout.h"
#include "settings/settings_common_session.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

namespace Settings {
namespace {

class FileConfirmations final : public Section<FileConfirmations> {
public:
	FileConfirmations(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	rpl::producer<QString> title() override {
		return tr::lng_settings_file_confirmations();
	}
	void sectionSaveChanges(FnMut<void()> done) override {
		saveExtensions();
		done();
	}

private:
	void saveExtensions();

	Ui::InputField *_extensions = nullptr;
};

FileConfirmations::FileConfirmations(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto page = Ui::CreateChild<CardPage>(this);
	const auto root = page->content();
	const auto settings = &Core::App().settings();
	const auto &list = settings->noWarningExtensions();
	const auto text = QStringList(begin(list), end(list)).join(' ');

	AddCardTitle(root, tr::lng_settings_edit_extensions());
	const auto extensionsCard = AddCardGroup(root);
	_extensions = extensionsCard->add(
		object_ptr<Ui::InputField>(
			extensionsCard,
			st::defaultInputField,
			Ui::InputField::Mode::MultiLine,
			tr::lng_settings_edit_extensions(),
			TextWithTags{ text }),
		QMargins(14, 12, 14, 16));
	_extensions->setObjectName(u"privacy/extensions"_q);
	_extensions->setInputMethodHints(Qt::ImhLatinOnly
		| Qt::ImhNoAutoUppercase
		| Qt::ImhNoPredictiveText);
	_extensions->focusedChanges(
	) | rpl::filter(rpl::mappers::_1 == false) | rpl::on_next([=] {
		saveExtensions();
	}, lifetime());
	AddCardDescription(root, tr::lng_settings_edit_extensions_about());

	const auto ipCard = AddCardGroup(root);
	const auto ip = ipCard->add(object_ptr<Ui::SettingsButton>(
		ipCard,
		tr::lng_settings_edit_ip_confirm(),
		st::settingsButtonNoIcon
	))->toggleOn(rpl::single(settings->ipRevealWarning()));
	ip->setObjectName(u"privacy/ipRevealWarning"_q);
	ip->toggledChanges() | rpl::on_next([=](bool enabled) {
		settings->setIpRevealWarning(enabled);
		Core::App().saveSettingsDelayed();
	}, ip->lifetime());
	AddCardDescription(root, tr::lng_settings_edit_ip_confirm_about());
	Ui::ResizeFitChild(this, page);
}

void FileConfirmations::saveExtensions() {
	const auto extensions = _extensions->getLastText()
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

} // namespace Settings
