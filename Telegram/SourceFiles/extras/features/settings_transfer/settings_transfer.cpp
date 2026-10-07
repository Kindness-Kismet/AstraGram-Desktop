#include "extras/features/settings_transfer/settings_transfer.h"

#include "extras/features/settings_transfer/settings_transfer_fields.h"
#include "extras/extras_settings.h"
#include "core/application.h"
#include "core/core_settings.h"
#include "core/version.h"
#include "data/data_session.h"
#include "data/data_user.h"
#include "main/main_session.h"
#include "platform/platform_translate_provider.h"
#include "storage/localstorage.h"
#include "storage/storage_account.h"
#include "window/notifications_manager.h"
#include "crl/crl_async.h"
#include "crl/crl_on_main.h"

#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

namespace Extras::SettingsTransfer {
namespace {

constexpr auto kMaximumFileSize = 4 * 1024 * 1024;

Json values(const Fields &fields) {
	auto result = Json::object();
	for (const auto &[key, field] : fields) {
		result[key] = field.value;
	}
	return result;
}

bool inspectSection(
		Inspection &result,
		const Json &source,
		Json &target,
		const Fields &fields,
		const QString &prefix,
		bool selected) {
	if (!source.is_object()) {
		result.error = prefix;
		return false;
	}
	for (const auto &[key, value] : source.items()) {
		const auto name = prefix + u"."_q + QString::fromStdString(key);
		const auto i = fields.find(key);
		if (i == fields.end()) {
			result.skipped.push_back(name);
			continue;
		}
		const auto &field = i->second;
		if (!field.valid(value)) {
			result.error = name;
			return false;
		}
		if (!selected) {
			continue;
		}
		if (prefix == u"custom"_q && key == "translationProvider"
			&& value == int(TranslationProvider::Native)
			&& !Platform::IsTranslateProviderAvailable()) {
			result.unavailable.push_back(name);
			continue;
		}
		target[key] = value;
		++result.count;
		if (field.restart && field.value != value) {
			result.restart.push_back(name);
		}
	}
	return true;
}

void applySection(const Json &section, const Fields &fields) {
	for (const auto &[key, value] : section.items()) {
		const auto &field = fields.at(key);
		if (field.value != value) {
			field.set(value);
		}
	}
}

void applyNotificationChanges(
		const Json &section,
		const Fields &previous,
		Inspection &result) {
	using Change = Window::Notifications::ChangeType;
	const auto changed = [&](const char *key) {
		const auto i = section.find(key);
		return i != section.end() && previous.at(key).value != *i;
	};
	auto &notifications = Core::App().notifications();
	if (changed("nativeNotifications")) {
		notifications.createManager();
		if (Core::App().settings().nativeNotifications()
			!= section["nativeNotifications"].get<bool>()) {
			result.unavailable.push_back(u"official.nativeNotifications"_q);
		}
	}
	const auto notify = [&](Change change, bool required) {
		if (required) {
			notifications.notifySettingsChanged(change);
		}
	};
	notify(Change::DesktopEnabled, changed("desktopNotify")
		|| changed("nativeNotifications") || changed("skipToastsInFocus"));
	notify(Change::SoundEnabled, changed("soundNotify"));
	notify(Change::FlashBounceEnabled, changed("flashBounceNotify"));
	notify(Change::ViewParams, changed("notifyView"));
	notify(Change::MaxCount, changed("notificationsCount"));
	notify(Change::Corner, changed("notificationsCorner"));
	notify(Change::IncludeMuted, changed("includeMutedCounter")
		|| changed("includeMutedCounterFolders"));
	notify(Change::CountMessages, changed("countUnreadMessages"));
}

} // namespace

Json snapshot(not_null<Main::Session*> session, Selection selection) {
	auto result = Json{
		{ "format", "astragram-settings" },
		{ "formatVersion", 1 },
		{ "appVersion", AppVersionStr },
	};
	if (selection.official) {
		result["official"] = values(officialFields());
	}
	if (selection.custom) {
		result["custom"] = values(customFields());
	}
	if (selection.account) {
		result["account"] = {
			{ "sourceUserId", QString::number(session->userId().bare) },
			{ "sourceName", session->user()->name() },
			{ "settings", values(accountFields(session)) },
		};
	}
	return result;
}

Inspection inspect(
		not_null<Main::Session*> session,
		const Json &document,
		Selection selection) {
	auto result = Inspection();
	if (!document.is_object()
		|| !document.contains("format")
		|| document["format"] != "astragram-settings"
		|| !document.contains("formatVersion")
		|| !document["formatVersion"].is_number_integer()
		|| document["formatVersion"] != 1
		|| !document.contains("appVersion")
		|| !document["appVersion"].is_string()) {
		result.error = u"Invalid settings file or unsupported format version"_q;
		return result;
	}
	result.document = Json::object();
	for (const auto &[key, value] : document.items()) {
		if (key == "format" || key == "formatVersion" || key == "appVersion") {
			result.document[key] = value;
			continue;
		}
		if (key == "official" || key == "custom") {
			auto target = Json::object();
			const auto official = key == "official";
			if (!inspectSection(result, value, target,
				official ? officialFields() : customFields(),
				QString::fromStdString(key),
				official ? selection.official : selection.custom)) {
				return result;
			}
			if (!target.empty()) {
				result.document[key] = std::move(target);
			}
			continue;
		}
		if (key != "account") {
			result.skipped.push_back(QString::fromStdString(key));
			continue;
		}
		if (!value.is_object() || !value.contains("sourceUserId")
			|| !value["sourceUserId"].is_string()
			|| !value.contains("settings")
			|| (value.contains("sourceName") && !value["sourceName"].is_string())) {
			result.error = u"Invalid account settings"_q;
			return result;
		}
		const auto id = value["sourceUserId"].get<QString>();
		auto validId = false;
		const auto number = id.toULongLong(&validId);
		if (!validId || !number || number > PeerId::kChatTypeMask
			|| QString::number(number) != id) {
			result.error = u"Invalid source account identifier"_q;
			return result;
		}
		auto target = Json::object();
		if (!inspectSection(result, value["settings"], target,
			accountFields(session), u"account"_q, selection.account)) {
			return result;
		}
		if (!target.empty()) {
			result.document["account"] = {
				{ "sourceUserId", value["sourceUserId"] },
				{ "settings", std::move(target) },
			};
		}
		for (const auto &[accountKey, unused] : value.items()) {
			if (accountKey != "sourceUserId" && accountKey != "sourceName"
				&& accountKey != "settings") {
				result.skipped.push_back(u"account."_q
					+ QString::fromStdString(accountKey));
			}
		}
	}
	return result;
}

void prepare(
		not_null<Main::Session*> session,
		const Json &document,
		Selection selection,
		Fn<void(Inspection)> done) {
	auto checked = inspect(session, document, selection);
	if (!checked.error.isEmpty()
		|| !checked.document.contains("official")
		|| !checked.document["official"].contains("downloadPath")) {
		done(std::move(checked));
		return;
	}
	const auto path = checked.document["official"]["downloadPath"].get<QString>();
	if (path.isEmpty() || path == u"tmp"_q) {
		done(std::move(checked));
		return;
	}
	crl::async([path, checked = std::move(checked),
		done = std::move(done)]() mutable {
		const auto exists = QFileInfo(path).isDir();
		crl::on_main([exists, checked = std::move(checked),
			done = std::move(done)]() mutable {
			if (!exists) {
				auto &official = checked.document["official"];
				official.erase("downloadPath");
				if (official.empty()) {
					checked.document.erase("official");
				}
				checked.missingPaths.push_back(u"official.downloadPath"_q);
				--checked.count;
			}
			done(std::move(checked));
		});
	});
}

namespace {

void applyChecked(
		not_null<Main::Session*> session,
		Inspection checked,
		Fn<void(Inspection, bool)> done) {
	const auto &data = checked.document;
	const auto official = data.contains("official");
	const auto custom = data.contains("custom");
	const auto account = data.contains("account");
	ExtrasSettings::beginBatchUpdate();
	if (official) {
		const auto fields = officialFields();
		applySection(data["official"], fields);
		applyNotificationChanges(data["official"], fields, checked);
	}
	if (custom) {
		applySection(data["custom"], customFields());
		// 保存失败后重试时，内存中的值可能已经与导入文件一致。
		ExtrasSettings::save();
	}
	if (account) {
		applySection(data["account"]["settings"], accountFields(session));
		session->data().photoLoadSettingsChanged();
		session->data().documentLoadSettingsChanged();
		session->data().checkPlayingAnimations();
	}
	struct Saving {
		Inspection result;
		Fn<void(Inspection, bool)> done;
		int pending = 0;
		bool success = true;
	};
	const auto saving = std::make_shared<Saving>(Saving{
		.result = std::move(checked),
		.done = std::move(done),
		.pending = 1 + int(official) + int(account),
	});
	const auto completed = [saving](bool success) {
		saving->success = saving->success && success;
		if (!--saving->pending) {
			saving->done(std::move(saving->result), saving->success);
		}
	};
	if (official) {
		Local::writeSettings(completed);
	}
	if (account) {
		session->local().writeSessionSettings(completed);
	}
	ExtrasSettings::endBatchUpdate(completed);
}

} // namespace

void apply(
		not_null<Main::Session*> session,
		const Json &document,
		Selection selection,
		Fn<void(Inspection, bool)> done) {
	const auto weakSession = base::make_weak(session);
	prepare(session, document, selection,
		[weakSession, done = std::move(done)](Inspection checked) mutable {
			if (!weakSession) {
				checked.error = u"Account session is no longer available"_q;
			}
			if (!checked.error.isEmpty() || !checked.count) {
				done(std::move(checked), false);
				return;
			}
			applyChecked(weakSession.get(), std::move(checked), std::move(done));
		});
}

void readFile(QString path, Fn<void(Json, bool)> done) {
	crl::async([path = std::move(path), done = std::move(done)]() mutable {
		auto file = QFile(path);
		auto document = Json();
		auto success = file.open(QIODevice::ReadOnly)
			&& file.size() <= kMaximumFileSize;
		if (success) {
			const auto bytes = file.read(kMaximumFileSize + 1);
			success = file.error() == QFileDevice::NoError
				&& bytes.size() <= kMaximumFileSize;
			if (success) {
				document = Json::parse(bytes.begin(), bytes.end(), nullptr, false);
			}
		}
		crl::on_main([done = std::move(done),
			document = std::move(document), success]() mutable {
			done(std::move(document), success);
		});
	});
}

void writeFile(QString path, Json document, Fn<void(bool)> done) {
	crl::async([path = std::move(path), document = std::move(document),
		done = std::move(done)]() mutable {
		const auto bytes = document.dump(2);
		auto file = QSaveFile(path);
		const auto success = file.open(QIODevice::WriteOnly)
			&& file.write(bytes.data(), bytes.size()) == bytes.size()
			&& file.commit();
		crl::on_main([done = std::move(done), success]() mutable {
			done(success);
		});
	});
}

} // namespace Extras::SettingsTransfer
