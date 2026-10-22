#include "window/themes/window_theme_editor_block.h"

#include "window/themes/window_theme_editor.h"
#include "lang/lang_keys.h"
#include "ui/painter.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"

#include <QtCore/QRegularExpression>

namespace Window::Theme {
namespace {

constexpr auto kRowHeight = 70;

} // namespace

EditorCategory EditorColorCategory(const QString &name) {
	static const auto basic = QRegularExpression(
		u"^(window|card|stroke|accent|settings|shadow|slideFade)"_q);
	static const auto controls = QRegularExpression(
		u"^(active|light|attention|outline|control|input|filterInput|placeholder|checkbox|slider|box|menu|scroll|smallClose|tooltip)"_q);
	if (basic.match(name).hasMatch()) {
		return EditorCategory::Basic;
	}
	if (name.startsWith(u"msg"_q) || name.startsWith(u"history"_q)) {
		return EditorCategory::Messages;
	}
	if (name.startsWith(u"dialogs"_q)) {
		return EditorCategory::Dialogs;
	}
	return controls.match(name).hasMatch()
		? EditorCategory::Controls
		: EditorCategory::Other;
}

QString EditorCategoryTitle(EditorCategory category) {
	switch (category) {
	case EditorCategory::Basic: return tr::extras_ThemeCreatorBasic(tr::now);
	case EditorCategory::Messages: return tr::extras_ThemeCreatorMessages(tr::now);
	case EditorCategory::Dialogs: return tr::extras_ThemeCreatorDialogs(tr::now);
	case EditorCategory::Controls: return tr::extras_ThemeCreatorControls(tr::now);
	case EditorCategory::Other: return tr::extras_ThemeCreatorOther(tr::now);
	}
	Unexpected("Unknown theme color category");
}

EditorBlock::EditorBlock(QWidget *parent)
: RpWidget(parent)
, _transparent(style::TransparentPlaceholder()) {
	setObjectName(u"theme_creator_colors"_q);
	setMouseTracking(true);
}

int EditorBlock::rowHeight() {
	return style::ConvertScale(kRowHeight);
}

void EditorBlock::setRows(std::vector<EditorColor> rows) {
	_rows = std::move(rows);
	refreshFilter();
}

void EditorBlock::filterRows(
		const QString &query,
		EditorCategory category,
		bool modified) {
	_query = query.trimmed();
	_category = category;
	_onlyModified = modified;
	refreshFilter();
}

void EditorBlock::refreshFilter() {
	_selected = _pressed = -1;
	_visible.clear();
	const auto words = _query.split(QChar(' '), Qt::SkipEmptyParts);
	for (auto index = 0; index != _rows.size(); ++index) {
		const auto &row = _rows[index];
		if (_onlyModified && !row.modified) {
			continue;
		}
		if (_query.isEmpty() && row.category != _category) {
			continue;
		}
		const auto text = row.title + ' ' + row.name + ' '
			+ row.description + ' ' + row.value + ' '
			+ QString::fromLatin1(ColorHexString(row.color));
		if (!ranges::all_of(words, [&](const QString &word) {
			return text.contains(word, Qt::CaseInsensitive);
		})) {
			continue;
		}
		_visible.push_back(index);
	}
	resizeToWidth(width());
	update();
}

void EditorBlock::setActive(const QString &name) {
	_active = name;
	update();
}

int EditorBlock::visibleCount() const {
	return int(_visible.size());
}

rpl::producer<QString> EditorBlock::chosen() const {
	return _chosen.events();
}

rpl::producer<int> EditorBlock::scrollRequests() const {
	return _scrollRequests.events();
}

void EditorBlock::chooseRow() {
	if (_visible.empty()) {
		return;
	}
	const auto index = (_selected >= 0) ? _selected : 0;
	_chosen.fire_copy(_rows[_visible[index]].name);
}

void EditorBlock::selectSkip(int direction) {
	if (_visible.empty()) {
		return;
	}
	setSelected(std::clamp(_selected + direction, 0, int(_visible.size()) - 1));
	_scrollRequests.fire(_selected * rowHeight());
}

int EditorBlock::resizeGetHeight(int newWidth) {
	return std::max(visibleCount(), 1) * rowHeight();
}

int EditorBlock::indexAt(QPoint position) const {
	if (position.x() < 0 || position.x() >= width() || position.y() < 0) {
		return -1;
	}
	const auto index = position.y() / rowHeight();
	return (index < visibleCount()) ? index : -1;
}

void EditorBlock::setSelected(int index) {
	if (_selected == index) {
		return;
	}
	_selected = index;
	setCursor(index >= 0 ? style::cur_pointer : style::cur_default);
	update();
}

void EditorBlock::mousePressEvent(QMouseEvent *e) {
	setSelected(indexAt(e->pos()));
	_pressed = _selected;
}

void EditorBlock::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = std::exchange(_pressed, -1);
	if (pressed >= 0 && pressed == indexAt(e->pos())) {
		_chosen.fire_copy(_rows[_visible[pressed]].name);
	}
}

void EditorBlock::mouseMoveEvent(QMouseEvent *e) {
	setSelected(indexAt(e->pos()));
}

void EditorBlock::leaveEventHook(QEvent *e) {
	setSelected(-1);
}

void EditorBlock::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.fillRect(e->rect(), st::boxBg);
	if (_visible.empty()) {
		p.setFont(st::boxTextFont);
		p.setPen(st::windowSubTextFg);
		p.drawText(rect(), Qt::AlignCenter,
			tr::extras_ThemeCreatorNoResults(tr::now));
		return;
	}
	const auto height = rowHeight();
	const auto margin = st::boxRowPadding.left();
	const auto swatchSize = style::ConvertScale(34);
	const auto skip = style::ConvertScale(12);
	const auto first = std::max(e->rect().top() / height, 0);
	const auto last = std::min(e->rect().bottom() / height + 1, visibleCount());
	for (auto index = first; index != last; ++index) {
		const auto &row = _rows[_visible[index]];
		const auto top = index * height;
		const auto active = (row.name == _active);
		const auto item = QRect(style::ConvertScale(10), top + style::ConvertScale(2),
			width() - style::ConvertScale(20), height - style::ConvertScale(4));
		if (active || index == _selected) {
			p.setPen(Qt::NoPen);
			p.setBrush(active ? st::controlActiveBg : st::windowBgOver);
			p.drawRoundedRect(item, style::ConvertScale(8), style::ConvertScale(8));
		}
		const auto swatch = QRect(margin, top + (height - swatchSize) / 2,
			swatchSize, swatchSize);
		{
			const auto hq = PainterHighQualityEnabler(p);
			auto path = QPainterPath();
			path.addRoundedRect(swatch, style::ConvertScale(8), style::ConvertScale(8));
			p.save();
			p.setClipPath(path);
			p.fillRect(swatch, _transparent);
			p.fillRect(swatch, row.color);
			p.restore();
			p.setPen(st::controlBorder);
			p.setBrush(Qt::NoBrush);
			p.drawPath(path);
		}
		const auto left = margin + swatchSize + skip;
		const auto available = width() - left - margin;
		p.setFont(st::themeEditorNameFont);
		p.setPen(st::windowFg);
		p.drawTextLeft(left, top + style::ConvertScale(11), width(),
			st::themeEditorNameFont->elided(row.title, available));
		p.setFont(st::boxTextFont);
		p.setPen(st::windowSubTextFg);
		p.drawTextLeft(left, top + style::ConvertScale(30), width(),
			st::boxTextFont->elided(row.name, available));
		p.drawTextLeft(left, top + style::ConvertScale(47), width(),
			st::boxTextFont->elided(row.description, available));
		if (row.modified) {
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowActiveTextFg);
			p.drawEllipse(item.right() - style::ConvertScale(9),
				top + style::ConvertScale(13), style::ConvertScale(5), style::ConvertScale(5));
		}
	}
}

} // namespace Window::Theme
