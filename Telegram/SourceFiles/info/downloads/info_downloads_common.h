#pragma once

namespace Info::Downloads {

enum class TypeFilter {
	All,
	Archive,
	Music,
	Video,
	Other,
};

[[nodiscard]] TypeFilter ClassifyFile(
	not_null<HistoryItem*> item,
	const QString &path);

} // namespace Info::Downloads
