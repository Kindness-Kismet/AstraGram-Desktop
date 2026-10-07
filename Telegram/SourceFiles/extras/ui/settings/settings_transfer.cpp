#include "extras/ui/settings/settings_transfer.h"

#include "extras/features/settings_transfer/settings_transfer.h"
#include "extras/ui/settings/settings_extras_utils.h"
#include "core/file_utilities.h"
#include "crl/crl_on_main.h"
#include "data/data_user.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "main/main_account.h"
#include "mainwindow.h"
#include "settings/settings_builder.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "ui/boxes/confirm_box.h"
#include "ui/layers/generic_box.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "window/window_session_controller.h"

namespace Settings {
namespace {

using namespace Extras::SettingsTransfer;

QString accountName(not_null<Main::Session*> session) {
	return session->user()->name() + u" ("_q
		+ QString::number(session->userId().bare) + u")"_q;
}

QString report(const Inspection &result) {
	const auto list = [](const QStringList &items) {
		return items.join(u", "_q);
	};
	auto text = tr::extras_SettingsTransferCount(tr::now,
		lt_amount, QString::number(result.count));
	if (!result.skipped.isEmpty()) {
		text += u"\n\n"_q + tr::extras_SettingsTransferUnknown(tr::now)
			+ u"\n"_q + list(result.skipped);
	}
	if (!result.missingPaths.isEmpty()) {
		text += u"\n\n"_q + tr::extras_SettingsTransferMissingPath(tr::now)
			+ u"\n"_q + list(result.missingPaths);
	}
	if (!result.unavailable.isEmpty()) {
		text += u"\n\n"_q + tr::extras_SettingsTransferUnavailable(tr::now)
			+ u"\n"_q + list(result.unavailable);
	}
	if (!result.restart.isEmpty()) {
		text += u"\n\n"_q + tr::extras_SettingsTransferRestart(tr::now)
			+ u"\n"_q + list(result.restart);
	}
	return text;
}

void transferBox(
		not_null<Ui::GenericBox*> box,
		not_null<Window::SessionController*> controller,
		Json document) {
	const auto importing = !document.is_null();
	const auto session = &controller->session();
	const auto weakBox = base::make_weak(box);
	const auto weakController = base::make_weak(controller);
	const auto closed = box->lifetime().make_state<bool>(false);
	box->boxClosing() | rpl::on_next([=] {
		*closed = true;
	}, box->lifetime());
	controller->lifetime().add(crl::guard(box, [=] { box->closeBox(); }));
	session->account().sessionChanges() | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());
	box->setTitle(importing
		? tr::extras_SettingsTransferImport()
		: tr::extras_SettingsTransferExport());
	box->setWidth(st::boxWideWidth);
	box->addRow(object_ptr<Ui::FlatLabel>(box,
		tr::extras_SettingsTransferScope(), st::boxLabel));
	box->addSkip(st::boxPadding.bottom());
	const auto official = box->addRow(object_ptr<Ui::Checkbox>(box,
		tr::extras_SettingsTransferOfficial(tr::now),
		!importing || document.contains("official"), st::defaultBoxCheckbox));
	official->setObjectName(u"settingsTransfer/official"_q);
	official->setAllowTextLines();
	const auto custom = box->addRow(object_ptr<Ui::Checkbox>(box,
		tr::extras_SettingsTransferCustom(tr::now),
		!importing || document.contains("custom"), st::defaultBoxCheckbox));
	custom->setObjectName(u"settingsTransfer/custom"_q);
	custom->setAllowTextLines();
	const auto account = box->addRow(object_ptr<Ui::Checkbox>(box,
		tr::extras_SettingsTransferAccount(tr::now, lt_user, accountName(session)),
		!importing || document.contains("account"), st::defaultBoxCheckbox));
	account->setObjectName(u"settingsTransfer/account"_q);
	account->setAllowTextLines();
	if (importing) {
		official->setDisabled(!document.contains("official"));
		custom->setDisabled(!document.contains("custom"));
		account->setDisabled(!document.contains("account"));
	}
	box->addSkip(st::boxPadding.bottom());
	box->addRow(object_ptr<Ui::FlatLabel>(box,
		tr::extras_SettingsTransferExcluded(), st::boxLabel));
	if (importing && document.contains("account")) {
		box->addSkip(st::boxPadding.bottom());
		box->addRow(object_ptr<Ui::FlatLabel>(box,
			tr::extras_SettingsTransferSource(tr::now, lt_user,
				document["account"]["sourceUserId"].get<QString>()), st::boxLabel));
	}
	const auto selection = [=] {
		return Selection{ official->checked(), custom->checked(), account->checked() };
	};
	const auto busy = box->lifetime().make_state<bool>(false);
	const auto setBusy = [=](bool value) {
		*busy = value;
		official->setEnabled(!value && (!importing || document.contains("official")));
		custom->setEnabled(!value && (!importing || document.contains("custom")));
		account->setEnabled(!value && (!importing || document.contains("account")));
	};
	box->addButton(importing
		? tr::extras_SettingsTransferReview()
		: tr::extras_SettingsTransferExport(), [=] {
		if (*busy) {
			return;
		}
		const auto chosen = selection();
		if (!chosen.official && !chosen.custom && !chosen.account) {
			controller->showToast(tr::extras_SettingsTransferChoose(tr::now));
			return;
		}
		if (!importing) {
			const auto data = snapshot(session, chosen);
			setBusy(true);
			FileDialog::GetWritePath(box.get(),
				tr::extras_SettingsTransferExport(tr::now),
				tr::extras_SettingsTransferFileFilter(tr::now),
				u"astragram-settings.json"_q,
				crl::guard(box, [=](QString &&path) {
					setBusy(false);
					if (path.isEmpty()) {
						return;
					}
					setBusy(true);
					writeFile(std::move(path), data, crl::guard(controller, [=](bool ok) {
						if (weakBox) {
							setBusy(false);
						}
						controller->showToast(ok
							? tr::extras_SettingsTransferExported(tr::now)
							: tr::extras_SettingsTransferWriteError(tr::now));
						if (ok && weakBox) {
							weakBox->closeBox();
						}
					}));
				}), crl::guard(box, [=] { setBusy(false); }));
			return;
		}
		setBusy(true);
		prepare(session, document, chosen, crl::guard(box, [=](Inspection checked) {
			if (!weakController || *closed) {
				return;
			}
			if (!checked.error.isEmpty()) {
				setBusy(false);
				controller->showToast(tr::extras_SettingsTransferInvalid(tr::now));
				return;
			}
			if (!checked.count) {
				setBusy(false);
				controller->show(Ui::MakeInformBox(report(checked)), Ui::LayerOption::KeepOther);
				return;
			}
			controller->show(Ui::MakeConfirmBox({
				.text = report(checked) + u"\n\n"_q
					+ tr::extras_SettingsTransferConfirm(tr::now),
				.confirmed = crl::guard(box, [=](Fn<void()> close) {
					if (!weakController || *closed) {
						close();
						return;
					}
					close();
					apply(session, checked.document, chosen,
						crl::guard(controller, [=](Inspection result, bool ok) {
							if (weakBox) {
								setBusy(false);
							}
							result.skipped.append(checked.skipped);
							result.missingPaths.append(checked.missingPaths);
							result.unavailable.append(checked.unavailable);
							const auto text = (ok
								? tr::extras_SettingsTransferImported(tr::now)
								: tr::extras_SettingsTransferSaveError(tr::now))
								+ u"\n\n"_q + report(result);
							if (ok && !result.restart.isEmpty()) {
								ShowRestartPrompt(controller, text);
								return;
							}
							crl::on_main(controller, [=] {
								controller->show(Ui::MakeInformBox(text));
							});
						}));
				}),
				.cancelled = crl::guard(box, [=](Fn<void()> close) {
					setBusy(false);
					close();
				}),
				.confirmText = tr::extras_SettingsTransferImport(),
			}), Ui::LayerOption::KeepOther);
		}));
	});
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
}

} // namespace

void addSettingsTransferButtons(Builder::SectionBuilder &builder) {
	const auto controller = builder.controller();
	builder.addSubsectionTitle(tr::extras_SettingsTransferTitle());
	builder.addButton({
		.id = u"extras/settingsExport"_q,
		.title = tr::extras_SettingsTransferExport(),
		.icon = { &st::menuIconDownload },
		.onClick = [=] {
			controller->show(Box(transferBox, controller, Json()));
		},
	});
	builder.addButton({
		.id = u"extras/settingsImport"_q,
		.title = tr::extras_SettingsTransferImport(),
		.icon = { &st::menuIconShowInFolder },
		.onClick = [=] {
			FileDialog::GetOpenPath(controller->widget().get(),
				tr::extras_SettingsTransferImport(tr::now),
				tr::extras_SettingsTransferFileFilter(tr::now),
				crl::guard(controller, [=](FileDialog::OpenResult &&result) {
					if (result.paths.isEmpty()) {
						controller->showToast(tr::extras_SettingsTransferInvalid(tr::now));
						return;
					}
					readFile(result.paths.front(), crl::guard(controller,
						[=](Json document, bool ok) {
							if (!ok || !inspect(&controller->session(),
								document, Selection()).error.isEmpty()) {
								controller->showToast(tr::extras_SettingsTransferInvalid(tr::now));
								return;
							}
							controller->show(Box(transferBox, controller, std::move(document)));
						}));
				}));
		},
	});
}

} // namespace Settings
