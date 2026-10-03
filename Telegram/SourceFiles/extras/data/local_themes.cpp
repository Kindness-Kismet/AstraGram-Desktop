#include "extras/data/local_themes.h"

#include "window/themes/window_theme.h"
#include "window/themes/window_themes_embedded.h"
#include "storage/localstorage.h"

#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QSaveFile>

namespace Extras::LocalThemes {
namespace {

rpl::event_stream<> changed;
constexpr auto kDigestLength = 64;

[[nodiscard]] QString directory() {
	return cWorkingDir() + u"tdata/local-themes/"_q;
}

[[nodiscard]] QString digest(const QByteArray &content) {
	return QString::fromLatin1(QCryptographicHash::hash(
		content,
		QCryptographicHash::Sha256).toHex());
}

} // namespace

std::vector<Data::CloudTheme> list() {
	auto result = std::vector<Data::CloudTheme>();
	const auto files = QDir(directory()).entryInfoList(
		{ u"*.tdesktop-theme"_q, u"*.tdesktop-palette"_q },
		QDir::Files,
		QDir::Name);
	for (const auto &file : files) {
		const auto stem = file.completeBaseName();
		if (stem.size() <= kDigestLength + 1
			|| stem[stem.size() - kDigestLength - 1] != '-') {
			continue;
		}
		auto theme = Data::CloudTheme();
		theme.id = 0xF000000000000000ULL
			| digest(file.absoluteFilePath().toUtf8()).left(15).toULongLong(nullptr, 16);
		theme.slug = file.absoluteFilePath();
		theme.title = stem.left(stem.size() - kDigestLength - 1);
		result.push_back(std::move(theme));
	}
	return result;
}

bool save(Window::Theme::Object &object) {
	if (object.cloud.id
		|| Window::Theme::IsEmbeddedTheme(object.pathAbsolute)
		|| object.content.isEmpty()) {
		return true;
	}
	const auto dir = QDir(directory());
	const auto hash = digest(object.content);
	const auto existing = dir.entryList(
		{ u"*-"_q + hash + u".tdesktop-*"_q }, QDir::Files);
	if (!existing.empty()) {
		object.pathAbsolute = dir.absoluteFilePath(existing.front());
		object.pathRelative = QDir().relativeFilePath(object.pathAbsolute);
		return true;
	}
	const auto original = QFileInfo(object.pathAbsolute);
	auto title = original.completeBaseName();
	if (original.absolutePath() == dir.absolutePath()) {
		title.chop(kDigestLength + 1);
	}
	const auto path = dir.absoluteFilePath(title
		+ '-' + hash + u".tdesktop-theme"_q);
	if (!QDir().mkpath(directory())) {
		return false;
	}
	auto file = QSaveFile(path);
	if (!file.open(QIODevice::WriteOnly)
		|| file.write(object.content) != object.content.size()
		|| !file.commit()) {
		LOG(("LocalThemes: could not save theme: %1").arg(file.errorString()));
		return false;
	}
	// 保存独立副本，移动或删除导入源文件不会影响已保存的主题。
	object.pathAbsolute = path;
	object.pathRelative = QDir().relativeFilePath(path);
	return true;
}

bool remove(const QString &path) {
	if (QFileInfo(path).absolutePath() != QDir(directory()).absolutePath()) {
		return false;
	}
	if (!QFile::remove(path)) {
		return false;
	}
	Local::clearThemeByPath(path);
	changed.fire({});
	return true;
}

rpl::producer<> changes() {
	return changed.events();
}

} // namespace Extras::LocalThemes
