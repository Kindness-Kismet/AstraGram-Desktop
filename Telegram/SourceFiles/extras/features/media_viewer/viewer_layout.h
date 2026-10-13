#pragma once

namespace ExtrasFeatures::MediaViewer {

// 关闭、更多、全屏三个按钮一组贴右，其余媒体操作排在分隔线左侧。
inline constexpr auto kTopBarGroupSize = 3;

[[nodiscard]] int topBarHeight();
// 顶栏右起第 index 个按钮，index 从 1 开始。
[[nodiscard]] QRect topBarButtonRect(int width, int top, int index);
[[nodiscard]] QRect topBarSeparatorRect(int width, int top);

// 顶栏左侧：头像，名字一行，时间和标题一行；没有名字时只剩第二行。
struct TopBarHeader {
	QRect userpic;
	QRect name;
	QRect date;
	QRect separator;
	QRect header;
	QString headerText;
};

struct TopBarHeaderArgs {
	int top = 0;
	int right = 0; // 文字可用区域的右边界
	bool userpic = false;
	int nameWidth = 0; // 0 表示没有名字
	QString date;
	QString header;
};

[[nodiscard]] TopBarHeader layoutTopBarHeader(const TopBarHeaderArgs &args);

} // namespace ExtrasFeatures::MediaViewer
