#pragma once

namespace Ui {
class RpWidget;
} // namespace Ui

namespace ExtrasFeatures::CustomLogo {

enum class ImportError {
	None,
	TooLarge,
	Unreadable,
	WriteFailed,
};

// 文件不超过 10 MB；裁掉透明边距后等比缩到 256px 内，另存到 tdata。
[[nodiscard]] ImportError importFile(const QString &path);

// 恢复默认图标，并删除已保存的图片。
void reset();

// 设置只接受导入时生成的文件名，避免指向 tdata 以外的文件。
[[nodiscard]] bool isStoredName(const QString &name);

// 显示尺寸：与原图标图形的长边一致，原图标自带的留白不计入。
[[nodiscard]] QSize displaySize(const style::icon &original);

// 等比缩放并居中到 box 内，开启主题色时只保留透明轮廓；未设置时为空。
[[nodiscard]] QImage prepare(QSize box);

// 有自定义图标时调用 customChanged(true) 隐藏原图标，在按钮中央按 displaySize 绘制。
void attach(
	not_null<Ui::RpWidget*> button,
	const style::icon &original,
	Fn<void(bool custom)> customChanged);

} // namespace ExtrasFeatures::CustomLogo
