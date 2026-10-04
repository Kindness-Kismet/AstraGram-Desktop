#include "settings/sections/settings_archive.h"

#include "api/api_global_privacy.h"
#include "apiwrap.h"
#include "data/data_peer_values.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/sections/settings_chat.h"
#include "settings/settings_builder.h"
#include "settings/settings_common_session.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

class Archive final : public Section<Archive> {
public:
	Archive(QWidget *parent, not_null<Window::SessionController*> controller);

	rpl::producer<QString> title() override {
		return tr::lng_settings_archive_title();
	}
};

const auto kMeta = Builder::BuildHelper({
	.id = Archive::Id(),
	.parentId = ChatId(),
	.title = &tr::lng_settings_archive_title,
	.icon = &st::menuIconArchive,
}, [](Builder::SectionBuilder &builder) {
	using Unarchive = Api::UnarchiveOnNewMessage;
	const auto session = builder.session();
	const auto privacy = &session->api().globalPrivacy();
	privacy->reload();

	builder.addSubsectionTitle(tr::lng_settings_unmuted_chats());
	const auto always = builder.addButton({
		.id = u"archive/unmuted"_q,
		.title = tr::lng_settings_always_in_archive(),
		.st = &st::settingsButtonNoIcon,
		.toggled = privacy->unarchiveOnNewMessage(
			) | rpl::map(rpl::mappers::_1 == Unarchive::None),
	});
	if (always) {
		always->toggledChanges() | rpl::filter([=](bool enabled) {
			return enabled != (privacy->unarchiveOnNewMessageCurrent() == Unarchive::None);
		}) | rpl::on_next([=](bool enabled) {
			privacy->updateUnarchiveOnNewMessage(enabled
				? Unarchive::None
				: Unarchive::NotInFoldersUnmuted);
		}, always->lifetime());
	}
	builder.addDividerText(tr::lng_settings_unmuted_chats_about());

	builder.scope([&] {
		builder.addSubsectionTitle(tr::lng_settings_chats_from_folders());
		const auto folders = builder.addButton({
			.id = u"archive/folders"_q,
			.title = tr::lng_settings_always_in_archive(),
			.st = &st::settingsButtonNoIcon,
			.toggled = privacy->unarchiveOnNewMessage(
				) | rpl::map(rpl::mappers::_1 != Unarchive::AnyUnmuted),
		});
		if (folders) {
			folders->toggledChanges() | rpl::filter([=](bool enabled) {
				return enabled != (privacy->unarchiveOnNewMessageCurrent() != Unarchive::AnyUnmuted);
			}) | rpl::on_next([=](bool enabled) {
				privacy->updateUnarchiveOnNewMessage(enabled
					? Unarchive::NotInFoldersUnmuted
					: Unarchive::AnyUnmuted);
			}, folders->lifetime());
		}
		builder.addDividerText(tr::lng_settings_chats_from_folders_about());
	}, privacy->unarchiveOnNewMessage(
		) | rpl::map(rpl::mappers::_1 != Unarchive::None));

	builder.addDivider();
	auto shown = rpl::single(false) | rpl::then(
		privacy->showArchiveAndMute()
		| rpl::filter(rpl::mappers::_1)
		| rpl::take(1));
	builder.scope([&] {
		builder.addSubsectionTitle(tr::lng_settings_new_unknown());
		const auto automatic = builder.addButton({
			.id = u"archive/auto_archive"_q,
			.title = tr::lng_settings_auto_archive(),
			.st = &st::settingsButtonNoIcon,
			.toggled = privacy->archiveAndMute(),
		});
		if (automatic) {
			automatic->toggledChanges() | rpl::filter([=](bool enabled) {
				return enabled != privacy->archiveAndMuteCurrent();
			}) | rpl::on_next([=](bool enabled) {
				privacy->updateArchiveAndMute(enabled);
			}, automatic->lifetime());
		}
		builder.addDividerText(tr::lng_settings_auto_archive_about());
	}, rpl::combine(
		std::move(shown),
		Data::AmPremiumValue(session),
		rpl::mappers::_1 || rpl::mappers::_2));
});

Archive::Archive(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace

Type ArchiveId() {
	return Archive::Id();
}

void PreloadArchiveSettings(not_null<::Main::Session*> session) {
	session->api().globalPrivacy().reload();
}

} // namespace Settings
