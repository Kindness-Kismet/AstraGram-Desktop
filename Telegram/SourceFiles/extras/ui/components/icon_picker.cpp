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

void applyIcon() {
#ifdef Q_OS_WIN
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
	setObjectName(u"extras/appIconPicker"_q);
	widthValue() | rpl::on_next([=](int w) {
		resize(w, rowsCount() * cellSize());
	}, lifetime());
}

void IconPicker::drawIcon(
		QPainter &p,
		const QImage &icon,
		const QRect &rect,
		float strokeOpacity) {
	PainterHighQualityEnabler hq(p);
	{
		const auto padding = st::iconPickerSelectedPadding;
		p.save();
		p.setPen(QPen(st::windowBgActive, style::ConvertScale(2)));
		p.setBrush(Qt::NoBrush);
		p.setOpacity(strokeOpacity);
		p.drawRoundedRect(
			rect.adjusted(-padding, -padding, padding, padding),
			st::iconPickerSelectedRounding,
			st::iconPickerSelectedRounding
		);
		p.restore();
	}

	p.drawImage(rect, icon);
}

int IconPicker::cellSize() const {
	return st::iconPickerIconSize + st::iconPickerCellPadding * 2;
}

int IconPicker::columnsCount() const {
	return std::clamp(width() / cellSize(), 1, kColumns);
}

int IconPicker::rowsCount() const {
	const auto count = int(ExtrasAssets::appIcons().size());
	const auto columns = columnsCount();
	return (count + columns - 1) / columns;
}

QRect IconPicker::iconRect(int index) const {
	const auto columns = columnsCount();
	const auto cell = cellSize();
	const auto padding = st::iconPickerCellPadding;
	return QRect(
		(index % columns) * cell + padding,
		(index / columns) * cell + padding,
		st::iconPickerIconSize,
		st::iconPickerIconSize);
}

void IconPicker::paintEvent(QPaintEvent *e) {
	Painter p(this);

	const auto &icons = ExtrasAssets::appIcons();
	const auto selected = ExtrasSettings::getInstance().appIcon();
	for (auto index = 0; index < int(icons.size()); ++index) {
		const auto &iconName = icons[index];
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
		} else if (iconName == selected) {
			opacity = _wasSelected.isEmpty() ? 1.0f : _animation.value(1.0f);
		}

		drawIcon(p, icon, iconRect(index), opacity);
	}
}

void IconPicker::mousePressEvent(QMouseEvent *e) {
	const auto pos = e->pos();
	const auto &icons = ExtrasAssets::appIcons();
	const auto iconName = [&] {
		for (auto index = 0; index < int(icons.size()); ++index) {
			if (iconRect(index).contains(pos)) {
				return icons[index];
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
