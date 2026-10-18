#pragma once

#include <array>

namespace Info::Downloads {

enum class TypeFilter {
	All,
	Archive,
	Music,
	Video,
	Other,
};

// 界面上的分类顺序，与枚举取值一致。
inline constexpr auto kTypeFilters = std::array{
	TypeFilter::All,
	TypeFilter::Archive,
	TypeFilter::Music,
	TypeFilter::Video,
	TypeFilter::Other,
};

// 各分类的文件数，已应用搜索词；全部 = 其余四类之和。
struct TypeCounts {
	std::array<int, kTypeFilters.size()> values = {};

	[[nodiscard]] int operator[](TypeFilter filter) const {
		return values[int(filter)];
	}
	friend inline bool operator==(
		const TypeCounts &a,
		const TypeCounts &b) = default;
};

[[nodiscard]] TypeFilter ClassifyFile(
	not_null<HistoryItem*> item,
	const QString &path);

[[nodiscard]] QString TypeFilterLabel(TypeFilter filter);

} // namespace Info::Downloads
