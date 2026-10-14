#include "extras/extras_infra.h"

#include "extras/extras_lang.h"
#include "extras/extras_settings.h"
#include "extras/extras_ui_settings.h"
#include "extras/extras_worker.h"
#include "extras/data/extras_database.h"
#ifdef _DEBUG
#include "extras/debug/debug_server.h"
#endif
#include "extras/ui/components/floating_surface.h"
#include "features/translator/extras_translator.h"
#include "ui/chat/chat_style_radius.h"
#include "utils/rc_manager.h"

#include <QtGui/QFontDatabase>

#ifdef Q_OS_WIN
#include "extras/utils/windows_utils.h"

#include <private/qguiapplication_p.h>
#include <qpa/qplatformfontdatabase.h>
#include <qpa/qplatformintegration.h>
#endif

namespace ExtrasInfra {
namespace {

#ifdef Q_OS_WIN
// Qt 先试 Tahoma、系统字体、地区字体直到 Segoe UI Symbol，再按字母遍历全部字体且最多 256 个。
// 保留这段头部，后接系统自带的语种字体，让它们排在字母序之前。
[[nodiscard]] QStringList windowsFallbacks() {
	const auto database = QGuiApplicationPrivate::platformIntegration()->fontDatabase();
#if QT_VERSION >= QT_VERSION_CHECK(6, 9, 0)
	constexpr auto script = QFontDatabasePrivate::Script_Common;
#else
	constexpr auto script = QChar::Script_Common;
#endif
	auto result = database->fallbacksForFamily(
		u"Open Sans"_q,
		QFont::StyleNormal,
		QFont::AnyStyle,
		script);
	result = result.mid(0, result.indexOf(u"Segoe UI Symbol"_q) + 1);
	const auto installed = QFontDatabase().families();
	for (const auto &family : {
		u"Segoe UI Historic"_q,
		u"Leelawadee UI"_q,
		u"Javanese Text"_q,
		u"Myanmar Text"_q,
		u"Gadugi"_q,
		u"Microsoft Himalaya"_q,
		u"Microsoft New Tai Lue"_q,
		u"Microsoft PhagsPa"_q,
		u"Microsoft Tai Le"_q,
		u"Microsoft Yi Baiti"_q,
		u"Mongolian Baiti"_q,
		u"Segoe UI"_q,
		u"Sylfaen"_q,
		u"MV Boli"_q,
		u"Cambria Math"_q,
		u"Sans Serif Collection"_q,
	}) {
		if (installed.contains(family)) {
			result.push_back(family);
		}
	}
	return result;
}
#endif

} // namespace

void initFontFallbacks() {
	// 字体加载后、创建界面字体前注册，覆盖系统字体与自定义字体。
	auto fallbacks = QStringList(u"Noto Sans Bamum"_q);
#ifdef Q_OS_WIN
	fallbacks += windowsFallbacks();
#endif
	LOG(("Font Fallbacks: %1").arg(fallbacks.join(u", "_q)));
	for (const auto &family : QFontDatabase().families()) {
		for (const auto &fallback : fallbacks) {
			if (fallback != family) {
				QFont::insertSubstitution(family, fallback);
			}
		}
	}
}

void initLang() {
	ExtrasLanguage::init();
}

void initUiSettings() {
	const auto &settings = ExtrasSettings::getInstance();

	ExtrasUiSettings::setMonoFont(settings.monoFont());
	ExtrasUiSettings::setWideMultiplier(settings.wideMultiplier());
	ExtrasUiSettings::setMaterialSwitches(true);
	ExtrasUiSettings::setAvatarCorners(settings.avatarCorners());
	ExtrasUiSettings::setHoverOverlay(ExtrasUi::chatSurfaceHover);
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
