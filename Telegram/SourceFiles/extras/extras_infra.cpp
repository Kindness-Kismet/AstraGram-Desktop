#include "extras/extras_infra.h"

#include "extras/extras_lang.h"
#include "extras/extras_settings.h"
#include "extras/extras_ui_settings.h"
#include "extras/extras_worker.h"
#include "extras/data/extras_database.h"
#ifdef _DEBUG
#include "extras/debug/debug_server.h"
#endif
#include "extras/ui/extras_logo.h"
#include "features/translator/extras_translator.h"
#include "lang/lang_instance.h"
#include "ui/chat/chat_style_radius.h"
#include "utils/rc_manager.h"

#ifdef Q_OS_WIN
#include "extras/utils/windows_utils.h"
#endif

namespace ExtrasInfra {

void initLang() {
	QString id = Lang::GetInstance().id();
	if (id.isEmpty()) {
		LOG(("Language is not loaded"));
		return;
	}
	ExtrasLanguage::init();
}

void initUiSettings() {
	const auto &settings = ExtrasSettings::getInstance();

	ExtrasUiSettings::setMonoFont(settings.monoFont());
	ExtrasUiSettings::setWideMultiplier(settings.wideMultiplier());
	ExtrasUiSettings::setMaterialSwitches(settings.materialSwitches());
	ExtrasUiSettings::setAvatarCorners(settings.avatarCorners());
	Ui::SetAppliedBubbleRadius(settings.messageBubbleRadius());
}

void initDatabase() {
	Database::initialize();
}

void initWorker() {
	ExtrasWorker::initialize();
}

void initRCManager() {
	RCManager::getInstance().start();
}

void initTranslator() {
	Extras::Translator::TranslateManager::init();
}

void initIcon() {
#ifdef Q_OS_WIN
	ExtrasAssets::loadAppIco();
	reloadAppIconFromTaskBar();
#endif
}

void initDebugServer() {
#ifdef _DEBUG
	ExtrasDebug::StartServer();
#endif
}

void init() {
	initLang();
	initDatabase();
	initUiSettings();
	initIcon();
	initWorker();
	initRCManager();
	initTranslator();
	initDebugServer();
}

}
