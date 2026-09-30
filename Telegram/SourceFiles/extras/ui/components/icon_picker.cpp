#include "extras/ui/components/icon_picker.h"

#include "tray.h"
#include "extras/extras_settings.h"
#include "extras/ui/extras_logo.h"
#include "core/application.h"
#include "main/main_domain.h"
#include "styles/style_extras_styles.h"
#include "ui/painter.h"
#include "window/main_window.h"

#ifdef Q_OS_WIN
#include "extras/utils/windows_utils.h"
#endif

namespace {

// 预设常量定义在其他编译单元，不能在静态初始化阶段读取列表。
[[nodiscard]] int rowsCount() {
	const auto count = int(ExtrasAssets::appIcons().size());
	return (count + IconPicker::kColumns - 1) / IconPicker::kColumns;
}

void applyIcon() {
#ifdef Q_OS_WIN
	ExtrasAssets::loadAppIco();
	reloadAppIconFromTaskBar();
#endif

	Window::OverrideApplicationIcon(ExtrasAssets::currentAppLogo());
	Core::App().refreshApplicationIcon();
	Core::App().tray().updateIconCounters();
	Core::App().domain().notifyUnreadBadgeChanged();
}

} // namespace

IconPicker::IconPicker(QWidget *parent)
	: RpWidget(parent) {
	widthValue() | rpl::on_next([=](int w) {
		const auto cell = w / kColumns;
		const auto iconSize = st::iconPickerIconSize;
		const auto contentSize = iconSize + st::iconPickerImagePadding * 2;
		const auto h = rowsCount() * cell - (cell - contentSize);
		resize(w, h);
	}, lifetime());
}

void IconPicker::drawIcon(QPainter &p, const QImage &icon, int x, int y, float strokeOpacity) {
	{
		PainterHighQualityEnabler hq(p);
		p.save();
		p.setPen(QPen(st::boxDividerBg, 0));
		p.setBrush(QBrush(st::boxDividerBg));
		p.setOpacity(strokeOpacity);
		p.drawRoundedRect(
			x + st::iconPickerSelectedPadding,
			y + st::iconPickerSelectedPadding,
			st::iconPickerIconSize + st::iconPickerSelectedPadding * 2,
			st::iconPickerIconSize + st::iconPickerSelectedPadding * 2,
			st::iconPickerSelectedRounding,
			st::iconPickerSelectedRounding
		);
		p.restore();
	}

	const auto rect = QRect(
		x + st::iconPickerImagePadding,
		y + st::iconPickerImagePadding,
		st::iconPickerIconSize,
		st::iconPickerIconSize
	);
	p.drawImage(rect, icon);
}

int IconPicker::cellWidth() const {
	return width() / kColumns;
}

void IconPicker::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto cell = cellWidth();
	const auto iconSize = st::iconPickerIconSize;
	const auto &icons = ExtrasAssets::appIcons();
	const auto rows = rowsCount();

	for (int row = 0; row < rows; row++) {
		const auto columns = std::min(kColumns, static_cast<int>(icons.size()) - row * kColumns);
		for (int i = 0; i < columns; i++) {
			auto const idx = i + row * kColumns;

			const auto &iconName = icons[idx];
			if (iconName.isEmpty()) {
				continue;
			}
			QImage icon;
			if (const auto cached = _cachedIcons.find(iconName); cached != _cachedIcons.end()) {
				icon = cached->second;
			} else {
				icon = _cachedIcons[iconName] = ExtrasAssets::loadPreview(iconName);
			}
			auto opacity = 0.0f;
			if (iconName == _wasSelected) {
				opacity = 1.0f - _animation.value(1.0f);
			} else if (iconName == ExtrasAssets::currentAppLogoName()) {
				opacity = _wasSelected.isEmpty() ? 1.0f : _animation.value(1.0f);
			}

			const auto x = i * cell + (cell - iconSize) / 2;
			const auto y = row * cell;

			drawIcon(p, icon, x, y, opacity);
		}
	}
}

void IconPicker::mousePressEvent(QMouseEvent *e) {
	const auto cell = cellWidth();
	const auto iconSize = st::iconPickerIconSize;
	const auto pos = e->pos();
	const auto &icons = ExtrasAssets::appIcons();
	const auto rows = rowsCount();
	const auto iconName = [&] {
		for (int row = 0; row < rows; row++) {
			const auto columns = std::min(kColumns, static_cast<int>(icons.size()) - row * kColumns);
			for (int i = 0; i < columns; i++) {
				const auto x = i * cell + (cell - iconSize) / 2;
				const auto y = row * cell;
				if (pos.x() >= x && pos.x() <= x + iconSize
					&& pos.y() >= y && pos.y() <= y + iconSize) {
					return icons[i + row * kColumns];
				}
			}
		}
		return QString();
	}();

	auto &settings = ExtrasSettings::getInstance();
	if (iconName.isEmpty() || settings.appIcon() == iconName) {
		return;
	}
	_wasSelected = settings.appIcon();
	_animation.start(
		[=]
		{
			update();
		},
		0.0,
		1.0,
		200,
		anim::easeOutCubic
	);
	settings.setAppIcon(iconName);
	applyIcon();
	repaint();
}
