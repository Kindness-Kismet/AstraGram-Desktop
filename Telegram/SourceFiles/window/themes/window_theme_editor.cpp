/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "window/themes/window_theme_editor.h"

#include "window/themes/window_theme.h"
#include "window/themes/window_theme_editor_block.h"
#include "window/themes/window_theme_editor_details.h"
#include "window/themes/window_theme_editor_box.h"
#include "window/window_controller.h"
#include "main/main_account.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/scroll_area.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/popup_menu.h"
#include "ui/style/style_palette_colorizer.h"
#include "ui/image/image_prepare.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "boxes/abstract_box.h"
#include "base/parse_helper.h"
#include "base/call_delayed.h"
#include "core/file_utilities.h"
#include "core/application.h"
#include "lang/lang_keys.h"
#include "styles/style_window.h"
#include "styles/style_layers.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"

#include <QtCore/QSaveFile>

namespace Window {
namespace Theme {
namespace {

template <size_t Size>
QByteArray qba(const char(&string)[Size]) {
	return QByteArray::fromRawData(string, Size - 1);
}

const auto kCloudInTextStart = qba("// THEME EDITOR SERVICE INFO START\n");
const auto kCloudInTextEnd = qba("// THEME EDITOR SERVICE INFO END\n\n");

struct ReadColorResult {
	ReadColorResult(QColor color, bool error = false) : color(color), error(error) {
	}
	QColor color;
	bool error = false;
};

ReadColorResult colorError(const QString &name) {
	return { QColor(), true };
}

ReadColorResult readColor(const QString &name, const char *data, int size) {
	if (size != 6 && size != 8) {
		return colorError(name);
	}
	auto readHex = [](char ch) {
		if (ch >= '0' && ch <= '9') {
			return (ch - '0');
		} else if (ch >= 'a' && ch <= 'f') {
			return (ch - 'a' + 10);
		} else if (ch >= 'A' && ch <= 'F') {
			return (ch - 'A' + 10);
		}
		return -1;
	};
	auto readValue = [readHex](const char *data) {
		auto high = readHex(data[0]);
		auto low = readHex(data[1]);
		return (high >= 0 && low >= 0) ? (high * 0x10 + low) : -1;
	};
	auto r = readValue(data);
	auto g = readValue(data + 2);
	auto b = readValue(data + 4);
	auto a = (size == 8) ? readValue(data + 6) : 255;
	if (r < 0 || g < 0 || b < 0 || a < 0) {
		return colorError(name);
	}
	return { QColor(r, g, b, a) };
}

bool skipComment(const char *&data, const char *end) {
	if (data == end) return false;
	if (*data == '/' && data + 1 != end) {
		if (*(data + 1) == '/') {
			data += 2;
			while (data != end && *data != '\n') {
				++data;
			}
			return true;
		} else if (*(data + 1) == '*') {
			data += 2;
			while (true) {
				while (data != end && *data != '*') {
					++data;
				}
				if (data != end) {
					++data;
					if (data != end && *data == '/') {
						++data;
						break;
					}
				}
				if (data == end) {
					break;
				}
			}
			return true;
		}
	}
	return false;
}

void skipWhitespacesAndComments(const char *&data, const char *end) {
	while (data != end) {
		if (!base::parse::skipWhitespaces(data, end)) return;
		if (!skipComment(data, end)) return;
	}
}

QLatin1String readValue(const char *&data, const char *end) {
	auto start = data;
	if (data != end && *data == '#') {
		++data;
	}
	base::parse::readName(data, end);
	return QLatin1String(start, data - start);
}

bool isValidColorValue(QLatin1String value) {
	auto isValidHexChar = [](char ch) {
		return (ch >= '0' && ch <= '9') || (ch >= 'A' && ch <= 'F') || (ch >= 'a' && ch <= 'f');
	};
	auto data = value.data();
	auto size = value.size();
	if ((size != 7 && size != 9) || data[0] != '#') {
		return false;
	}
	for (auto i = 1; i != size; ++i) {
		if (!isValidHexChar(data[i])) {
			return false;
		}
	}
	return true;
}

struct PaletteDefinition {
	QString name;
	QString value;
	int statement = 0;
	int valueStart = 0;
	int valueSize = 0;
};

[[nodiscard]] std::optional<std::vector<PaletteDefinition>> parsePaletteDefinitions(
		const QByteArray &content) {
	auto result = std::vector<PaletteDefinition>();
	auto validNames = QSet<QString>();
	const auto start = content.constData();
	const auto end = start + content.size();
	auto data = start;
	while (data != end) {
		skipWhitespacesAndComments(data, end);
		if (data == end) {
			break;
		}
		const auto statement = data;
		const auto name = base::parse::readName(data, end);
		skipWhitespacesAndComments(data, end);
		if (name.size() == 0 || data == end || *data != ':') {
			return std::nullopt;
		}
		++data;
		skipWhitespacesAndComments(data, end);
		const auto valueStart = data;
		const auto value = readValue(data, end);
		if (value.size() == 0) {
			return std::nullopt;
		}
		skipWhitespacesAndComments(data, end);
		if (data == end || *data != ';') {
			return std::nullopt;
		}
		++data;
		if (!isValidColorValue(value) && !validNames.contains(QString(value))) {
			continue;
		}
		validNames.insert(QString(name));
		result.push_back({
			.name = QString(name),
			.value = QString(value),
			.statement = int(statement - start),
			.valueStart = int(valueStart - start),
			.valueSize = value.size(),
		});
	}
	return result;
}

[[nodiscard]] QByteArray ColorizeInContent(
		QByteArray content,
		const style::colorizer &colorizer) {
	content.detach();
	auto start = content.constBegin(), data = start, end = data + content.size();
	while (data != end) {
		skipWhitespacesAndComments(data, end);
		if (data == end) break;

		[[maybe_unused]] auto foundName = base::parse::readName(data, end);
		skipWhitespacesAndComments(data, end);
		if (data == end || *data != ':') {
			return "error";
		}
		++data;
		skipWhitespacesAndComments(data, end);
		auto value = readValue(data, end);
		if (value.size() == 0) {
			return "error";
		}
		if (isValidColorValue(value)) {
			const auto colorized = style::colorize(value, colorizer);
			Assert(colorized.size() == value.size());
			memcpy(
				content.data() + (data - start) - value.size(),
				colorized.data(),
				value.size());
		}
		skipWhitespacesAndComments(data, end);
		if (data == end || *data != ';') {
			return "error";
		}
		++data;
	}
	return content;
}

QString bytesToUtf8(QLatin1String bytes) {
	return QString::fromUtf8(bytes.data(), bytes.size());
}

QByteArray removeColorFromContent(
		const QByteArray &content,
		const QByteArray &name) {
	auto result = QByteArray();
	const auto start = content.constData();
	const auto end = start + content.size();
	auto data = start;
	auto remaining = start;
	while (data != end) {
		skipWhitespacesAndComments(data, end);
		if (data == end) {
			break;
		}
		const auto statement = data;
		const auto found = base::parse::readName(data, end);
		skipWhitespacesAndComments(data, end);
		if (data == end || *data != ':') {
			return "error";
		}
		++data;
		skipWhitespacesAndComments(data, end);
		const auto value = readValue(data, end);
		if (value.size() == 0) {
			return "error";
		}
		skipWhitespacesAndComments(data, end);
		if (data == end || *data != ';') {
			return "error";
		}
		++data;
		if (found == name) {
			result.append(remaining, statement - remaining);
			remaining = data;
		}
	}
	result.append(remaining, end - remaining);
	return result;
}

QByteArray insertDefinitionsBeforeColor(
		const QByteArray &content,
		const QByteArray &name,
		const QByteArray &definitions) {
	const auto definitionsInContent = parsePaletteDefinitions(content);
	if (!definitionsInContent) {
		return "error";
	}
	auto position = content.size();
	for (const auto &definition : *definitionsInContent) {
		if (definition.name.toLatin1() == name) {
			position = definition.statement;
		}
	}
	const auto newline = (content.indexOf("\r\n") >= 0) ? "\r\n" : "\n";
	const auto prefix = content.left(position);
	return prefix + (prefix.endsWith('\n') ? "" : newline)
		+ definitions + content.mid(position);
}

} // namespace

QByteArray ColorHexString(const QColor &color) {
	auto result = QByteArray();
	result.reserve(9);
	result.append('#');
	const auto addHex = [&](int code) {
		if (code >= 0 && code < 10) {
			result.append('0' + code);
		} else if (code >= 10 && code < 16) {
			result.append('a' + (code - 10));
		}
	};
	const auto addValue = [&](int code) {
		addHex(code / 16);
		addHex(code % 16);
	};
	addValue(color.red());
	addValue(color.green());
	addValue(color.blue());
	if (color.alpha() != 255) {
		addValue(color.alpha());
	}
	return result;
}

QByteArray ReplaceValueInPaletteContent(
		const QByteArray &content,
		const QByteArray &name,
		const QByteArray &value) {
	const auto definitions = parsePaletteDefinitions(content);
	if (!definitions) {
		return "error";
	}
	for (auto i = definitions->crbegin(); i != definitions->crend(); ++i) {
		if (i->name.toLatin1() == name) {
			return content.left(i->valueStart) + value
				+ content.mid(i->valueStart + i->valueSize);
		}
	}
	auto newline = (content.indexOf("\r\n") >= 0 ? "\r\n" : "\n");
	auto addedline = (content.endsWith('\n') ? "" : newline);
	return content + addedline + name + ": " + value + ";" + newline;
}

[[nodiscard]] QByteArray WriteCloudToText(const Data::CloudTheme &cloud) {
	auto result = QByteArray();
	const auto add = [&](const QByteArray &key, const QString &value) {
		result.append("// " + key + ": " + value.toLatin1() + "\n");
	};
	result.append(kCloudInTextStart);
	add("ID", QString::number(cloud.id));
	add("ACCESS", QString::number(cloud.accessHash));
	result.append(kCloudInTextEnd);
	return result;
}

[[nodiscard]] Data::CloudTheme ReadCloudFromText(const QByteArray &text) {
	const auto index = text.indexOf(kCloudInTextEnd);
	if (index <= 1) {
		return Data::CloudTheme();
	}
	auto result = Data::CloudTheme();
	const auto list = text.mid(0, index - 1).split('\n');
	const auto take = [&](uint64 &value, int index) {
		if (list.size() <= index) {
			return false;
		}
		const auto &entry = list[index];
		const auto position = entry.indexOf(": ");
		if (position < 0) {
			return false;
		}
		value = QString::fromLatin1(entry.mid(position + 2)).toULongLong();
		return true;
	};
	if (!take(result.id, 1) || !take(result.accessHash, 2)) {
		return Data::CloudTheme();
	}
	return result;
}

QByteArray StripCloudTextFields(const QByteArray &text) {
	const auto firstValue = text.indexOf(": #");
	auto start = 0;
	while (true) {
		const auto index = text.indexOf(kCloudInTextEnd, start);
		if (index < 0 || index > firstValue) {
			break;
		}
		start = index + kCloudInTextEnd.size();
	}
	return (start > 0) ? text.mid(start) : text;
}

Editor::Editor(
	QWidget*,
	not_null<Window::Controller*> window,
	const Data::CloudTheme &cloud)
: _window(window)
, _cloud(cloud)
, _search(this, st::defaultInputField, tr::extras_ThemeCreatorSearch())
, _modified(this, tr::extras_ThemeCreatorModified(tr::now))
, _category(this, rpl::single(EditorCategoryTitle(EditorCategory::Basic)),
	st::defaultBoxButton)
, _caption(this, QString(), st::boxDividerLabel)
, _listScroll(this, st::boxScroll)
, _detailsScroll(this, st::boxScroll) {
	setObjectName(u"theme_creator"_q);
	setProperty("responsiveBoxWidth", true);
	_search->setObjectName(u"theme_creator_search"_q);
	_search->customUpDown(true);
	_modified->setObjectName(u"theme_creator_modified"_q);
	_category->setObjectName(u"theme_creator_category"_q);
	_listScroll->setObjectName(u"theme_creator_list_scroll"_q);
	_detailsScroll->setObjectName(u"theme_creator_details_scroll"_q);
	_list = _listScroll->setOwnedWidget(object_ptr<EditorBlock>(this));
	_details = _detailsScroll->setOwnedWidget(object_ptr<EditorDetails>(this));
}

void Editor::prepare() {
	setTitle(tr::extras_ThemeCreatorTitle());
	setCloseByOutsideClick(false);
	setCloseByEscape(false);
	addCloseButton(st::boxTitleClose, [=] { closeWithConfirmation(); });
	_save = addButton(tr::lng_theme_editor_save_button(), [=] { save(); });
	_save->setObjectName(u"theme_creator_save"_q);
	_cancel = addButton(tr::lng_cancel(), [=] { closeWithConfirmation(); });
	_cancel->setObjectName(u"theme_creator_cancel"_q);
	_menuButton = addLeftButton(tr::extras_ThemeCreatorMore(), [=] { showMenu(); });
	_menuButton->setObjectName(u"theme_creator_more"_q);
	setDimensions(style::ConvertScale(880), style::ConvertScale(624));

	_search->changes() | rpl::on_next([=] { updateFilter(); }, lifetime());
	_search->submits() | rpl::on_next([=] { _list->chooseRow(); }, lifetime());
	_modified->checkedChanges() | rpl::on_next([=] { updateFilter(); }, lifetime());
	_category->setClickedCallback([=] {
		const auto menu = new Ui::PopupMenu(this);
		menu->deleteOnHide(true);
		for (auto index = 0; index != 5; ++index) {
			const auto category = EditorCategory(index);
			menu->addAction(EditorCategoryTitle(category), [=] {
				_currentCategory = category;
				_category->setText(rpl::single(EditorCategoryTitle(category)));
				updateFilter();
			});
		}
		menu->popup(_category->mapToGlobal(QPoint(0, _category->height())));
	});
	_list->chosen() | rpl::on_next([=](const QString &name) {
		selectColor(name);
	}, lifetime());
	_list->scrollRequests() | rpl::on_next([=](int top) {
		_listScroll->scrollToY(top, top + EditorBlock::rowHeight());
	}, lifetime());
	_listScroll->scrollTopChanges() | rpl::on_next([=] {
		const auto top = _listScroll->scrollTop();
		_list->setVisibleTopBottom(top, top + _listScroll->height());
	}, lifetime());
	_details->changes() | rpl::on_next([=](const EditorDetails::Change &change) {
		applyEditing(change.name, change.value);
	}, lifetime());
	_details->setCallbacks([=] { showColorList(); }, [=] { restoreSelected(); });
	boxClosing() | rpl::on_next([=] {
		// 页面切换或退出只关闭界面，放弃编辑由关闭确认负责。
		_closing = true;
	}, lifetime());
	Background()->updates() | rpl::on_next([=](const BackgroundUpdate &update) {
		if (_applyingUpdate || !Background()->editingTheme()) {
			return;
		}
		if (update.type == BackgroundUpdate::Type::TestingTheme) {
			Revert();
			base::call_delayed(st::slideDuration, this, [=] {
				getDelegate()->show(Ui::MakeInformBox(tr::lng_theme_editor_cant_change_theme()),
					Ui::LayerOption::KeepOther);
			});
		}
	}, lifetime());

	auto file = QFile(EditingPalettePath());
	if (!file.open(QIODevice::ReadOnly)) {
		showError();
		return;
	}
	_paletteContent = file.readAll();
	if (file.error() != QFileDevice::NoError || !readRows(true)) {
		showError();
		return;
	}
	_baselineContent = _paletteContent;
	for (auto &row : _rows) {
		_baselineValues.insert(row.name, row.value);
		if (row.existing) {
			_baselineExisting.insert(row.name);
		}
		row.modified = false;
	}
	_list->setRows(_rows);
	if (!_rows.empty()) {
		_selected = _rows.front().name;
		_list->setActive(_selected);
		updateDetails();
	}
	updateFilter();
	updateControlsGeometry();
}

bool Editor::readRows(bool captureBaseline) {
	const auto definitions = parsePaletteDefinitions(_paletteContent);
	if (!definitions) {
		return false;
	}
	auto rows = std::vector<EditorColor>();
	auto indices = QMap<QString, int>();
	auto baselineDefinitions = std::vector<BaselineDefinition>();
	auto baselineIndices = QMap<QString, int>();
	const auto feed = [&](QString name, QString value, bool existing) {
		auto color = QColor();
		if (value.startsWith('#')) {
			const auto bytes = value.toLatin1();
			const auto parsed = readColor(name, bytes.constData() + 1, bytes.size() - 1);
			if (parsed.error) {
				LOG(("Theme Warning: Skipping invalid palette value '%1: %2'.").arg(name, value));
				return;
			}
			color = parsed.color;
		} else {
			const auto source = indices.constFind(value);
			if (source == indices.cend()) {
				LOG(("Theme Warning: Skipping undefined palette reference '%1: %2'.").arg(name, value));
				return;
			}
			color = rows[source.value()].color;
		}
		if (captureBaseline) {
			const auto source = value.startsWith('#') ? -1 : baselineIndices.value(value, -1);
			baselineDefinitions.push_back({ .name = name, .value = value, .source = source });
			baselineIndices.insert(name, int(baselineDefinitions.size()) - 1);
		}
		if (const auto found = indices.constFind(name); found != indices.cend()) {
			auto &row = rows[found.value()];
			row.value = value;
			row.color = color;
			row.sources = indices.keys();
			row.sources.removeAll(name);
			return;
		}
		const auto sources = indices.keys();
		indices.insert(name, int(rows.size()));
		rows.push_back({
			.name = name,
			.title = EditorColorTitle(name),
			.description = EditorColorDescription(name),
			.value = value,
			.color = color,
			.category = EditorColorCategory(name),
			.existing = existing,
			.sources = sources,
		});
	};
	for (const auto &definition : *definitions) {
		feed(definition.name, definition.value, true);
	}
	for (const auto &row : style::main_palette::data()) {
		const auto name = bytesToUtf8(row.name);
		if (!indices.contains(name)) {
			feed(name, QString(row.value), false);
		}
	}
	for (auto &row : rows) {
		const auto bytes = row.name.toLatin1();
		const auto index = style::internal::GetPaletteIndex(QLatin1String(bytes));
		if (index >= 0) {
			row.color = style::main_palette::get()->colorAtIndex(index)->c;
		}
		row.modified = !_baselineContent.isEmpty()
			&& (!_baselineValues.contains(row.name)
				|| _baselineValues.value(row.name) != row.value);
	}
	_rows = std::move(rows);
	if (captureBaseline) {
		_baselineDefinitions = std::move(baselineDefinitions);
		_baselineIndices = std::move(baselineIndices);
	}
	_list->setRows(_rows);
	_list->setActive(_selected);
	return true;
}

QString Editor::selectedName() const {
	return _selected;
}

QString Editor::selectedValue() const {
	const auto i = ranges::find(_rows, _selected, &EditorColor::name);
	return (i != end(_rows)) ? i->value : QString();
}

QColor Editor::selectedColor() const {
	const auto i = ranges::find(_rows, _selected, &EditorColor::name);
	return (i != end(_rows)) ? i->color : QColor();
}

bool Editor::appendBaselineSource(
		int index,
		QSet<QString> &available,
		QByteArray &definitions) const {
	auto chain = std::vector<int>();
	while (index >= 0 && !available.contains(_baselineDefinitions[index].name)) {
		chain.push_back(index);
		index = _baselineDefinitions[index].source;
	}
	// 来源指向当时已解析的定义，逆序补写保留原有引用关系。
	for (auto i = chain.crbegin(); i != chain.crend(); ++i) {
		const auto &definition = _baselineDefinitions[*i];
		definitions += definition.name.toLatin1() + ": " + definition.value.toLatin1() + ";\n";
		available.insert(definition.name);
	}
	return !chain.empty() || index >= 0;
}

int Editor::visibleColorCount() const {
	return _list->visibleCount();
}

int Editor::modifiedColorCount() const {
	return ranges::count_if(_rows, &EditorColor::modified);
}

bool Editor::showingDetails() const {
	return _detailsPage && width() < style::ConvertScale(660);
}

QByteArray Editor::paletteContent() const {
	return _paletteContent;
}

void Editor::selectColor(const QString &name) {
	if (!_details->submitColor()) {
		return;
	}
	const auto i = ranges::find(_rows, name, &EditorColor::name);
	if (i == end(_rows)) {
		return;
	}
	_selected = name;
	_detailsPage = true;
	_list->setActive(name);
	updateDetails();
	_detailsScroll->scrollToY(0);
	updateControlsGeometry();
	if (showingDetails()) {
		_details->setInnerFocus();
	}
}

void Editor::updateDetails() {
	const auto i = ranges::find(_rows, _selected, &EditorColor::name);
	if (i == end(_rows)) {
		return;
	}
	auto sources = std::vector<EditorColor>();
	// 最后一次有效定义决定可引用的来源，兼容主题文件中的重复定义。
	for (const auto &source : _rows) {
		if (source.existing && source.name != i->name
			&& (!i->existing || i->sources.contains(source.name))) {
			sources.push_back(source);
		}
	}
	_details->showColor(*i, std::move(sources));
}

void Editor::updateFilter() {
	_list->filterRows(_search->getLastText(), _currentCategory, _modified->checked());
	updateCaption();
	_listScrollTop = 0;
	_listScroll->scrollToY(0);
	updateControlsGeometry();
}

void Editor::updateCaption() {
	_caption->setText((_search->getLastText().trimmed().isEmpty()
		? EditorCategoryTitle(_currentCategory)
		: tr::extras_ThemeCreatorSearchResults(tr::now))
		+ u" · "_q + QString::number(visibleColorCount())
		+ u" "_q + tr::extras_ThemeCreatorColorCount(tr::now));
}

bool Editor::applyEditing(const QString &name, const QString &value) {
	const auto row = ranges::find(_rows, name, &EditorColor::name);
	if (row == end(_rows)) {
		return false;
	}
	if (value.startsWith('#')) {
		const auto bytes = value.toLatin1();
		if (readColor(name, bytes.constData() + 1, bytes.size() - 1).error) {
			return false;
		}
	} else {
		const auto source = ranges::find(_rows, value, &EditorColor::name);
		if (source == end(_rows) || !source->existing
			|| name == value
			|| (row->existing && !row->sources.contains(value))) {
			return false;
		}
	}
	const auto content = ReplaceValueInPaletteContent(
		_paletteContent, name.toLatin1(), value.toLatin1());
	if (content == "error" || !applyNewPalette(content)) {
		showError();
		return false;
	}
	if (!readRows()) {
		showError();
		return false;
	}
	updateDetails();
	updateCaption();
	updateControlsGeometry();
	return true;
}

void Editor::restoreSelected() {
	const auto row = ranges::find(_rows, _selected, &EditorColor::name);
	Expects(row != end(_rows));
	const auto referenced = ranges::any_of(_rows, [&](const EditorColor &other) {
		return other.existing && other.value == _selected;
	});
	if (referenced && !_baselineValues.contains(_selected)) {
		showError();
		return;
	}
	auto content = QByteArray();
	if (!_baselineExisting.contains(_selected) && !referenced) {
		content = removeColorFromContent(_paletteContent, _selected.toLatin1());
	} else {
		const auto value = _baselineValues.value(_selected);
		auto definitions = QByteArray();
		auto available = QSet<QString>(row->sources.begin(), row->sources.end());
		if (!row->existing) {
			for (const auto &other : _rows) {
				if (other.existing) {
					available.insert(other.name);
				}
			}
		}
		// 导入后缺失的来源按初始配色补在目标之前，保留仍被引用的定义。
		if (!value.startsWith('#')) {
			const auto baseline = _baselineIndices.value(_selected, -1);
			if (baseline < 0 || !appendBaselineSource(
					_baselineDefinitions[baseline].source, available, definitions)) {
				showError();
				return;
			}
		}
		content = definitions.isEmpty() ? _paletteContent
			: insertDefinitionsBeforeColor(_paletteContent,
				_selected.toLatin1(), definitions);
		content = ReplaceValueInPaletteContent(content,
			_selected.toLatin1(), value.toLatin1());
	}
	if (content == "error" || !applyNewPalette(content) || !readRows()) {
		showError();
		return;
	}
	updateDetails();
	updateCaption();
	updateControlsGeometry();
}

bool Editor::applyNewPalette(const QByteArray &content) {
	auto file = QSaveFile(EditingPalettePath());
	if (!file.open(QIODevice::WriteOnly) || file.write(content) != content.size()) {
		LOG(("Theme Error: Could not write edited palette."));
		return false;
	}
	_applyingUpdate = true;
	const auto applied = ApplyEditedPalette(content);
	_applyingUpdate = false;
	if (!applied) {
		LOG(("Theme Error: Could not apply edited palette."));
		return false;
	}
	if (!file.commit()) {
		_applyingUpdate = true;
		ApplyEditedPalette(_paletteContent);
		_applyingUpdate = false;
		LOG(("Theme Error: Could not save edited palette."));
		return false;
	}
	_paletteContent = content;
	return true;
}

void Editor::showError() {
	getDelegate()->show(Ui::MakeInformBox(tr::lng_theme_editor_error()),
		Ui::LayerOption::KeepOther);
}

void Editor::showMenu() {
	const auto menu = new Ui::PopupMenu(this);
	menu->deleteOnHide(true);
	menu->addAction(tr::lng_theme_editor_menu_export(tr::now),
		[=] { exportTheme(); }, &st::menuIconExportTheme);
	menu->addAction(tr::lng_theme_editor_menu_import(tr::now),
		[=] { importTheme(); }, &st::menuIconImportTheme);
	menu->addAction(tr::lng_theme_editor_menu_show(tr::now), [=] {
		File::ShowInFolder(EditingPalettePath());
	}, &st::menuIconPalette);
	menu->popup(_menuButton->mapToGlobal(QPoint(0, -menu->height())));
}

void Editor::exportTheme() {
	if (!_details->submitColor()) {
		return;
	}
	FileDialog::GetWritePath(this,
		tr::lng_theme_editor_choose_name(tr::now),
		"Themes (*.tdesktop-theme)", "awesome.tdesktop-theme",
		crl::guard(this, [=](const QString &path) {
			if (path.isEmpty()) {
				return;
			}
			const auto result = CollectForExport(_paletteContent);
			auto file = QFile(path);
			if (!file.open(QIODevice::WriteOnly) || file.write(result) != result.size()) {
				LOG(("Theme Error: Could not export theme to '%1'.").arg(path));
				showError();
				return;
			}
			showToast(tr::lng_theme_editor_done(tr::now));
		}));
}

void Editor::importTheme() {
	auto filters = QStringList(u"Theme files (*.tdesktop-theme *.tdesktop-palette)"_q);
	filters.push_back(FileDialog::AllFilesFilter());
	FileDialog::GetOpenPath(this, tr::lng_theme_editor_menu_import(tr::now),
		filters.join(u";;"_q), crl::guard(this, [=](const FileDialog::OpenResult &result) {
			const auto path = result.paths.isEmpty() ? QString() : result.paths.front();
			if (!path.isEmpty()) {
				importThemeFile(path);
			}
		}));
}

bool Editor::importThemeFile(const QString &path) {
	auto file = QFile(path);
	if (!file.open(QIODevice::ReadOnly)) {
		showError();
		return false;
	}
	auto object = Object();
	object.pathAbsolute = QFileInfo(path).absoluteFilePath();
	object.pathRelative = QDir().relativeFilePath(path);
	object.content = file.readAll();
	const auto parsed = ParseTheme(object, false, false);
	if (parsed.palette.isEmpty() || !applyNewPalette(parsed.palette) || !readRows()) {
		showError();
		return false;
	}
	_search->setText(QString());
	if (ranges::find(_rows, _selected, &EditorColor::name) == end(_rows)) {
		_selected = _rows.empty() ? QString() : _rows.front().name;
	}
	updateDetails();
	updateFilter();
	auto image = Images::Read({
		.content = parsed.background,
		.forceOpaque = true,
	}).image;
	if (!image.isNull()) {
		Background()->set(Data::CustomWallPaper(), std::move(image));
		Background()->setTile(parsed.tiled);
		Ui::ForceFullRepaint(_window->widget());
	}
	return true;
}

QByteArray Editor::ColorizeInContent(
		QByteArray content,
		const style::colorizer &colorizer) {
	return Window::Theme::ColorizeInContent(std::move(content), colorizer);
}

void Editor::save() {
	if (Core::App().passcodeLocked()) {
		showToast(tr::lng_theme_editor_need_unlock(tr::now));
		return;
	}
	if (!_window->account().sessionExists()) {
		showToast(tr::lng_theme_editor_need_auth(tr::now));
		return;
	}
	if (_saving) {
		return;
	}
	if (!_details->submitColor()) {
		return;
	}
	_saving = true;
	const auto weak = QPointer<Editor>(this);
	SaveTheme(_window, _cloud, _paletteContent,
		[=] { return weak && !weak->_closing; },
		crl::guard(this, [=] { _saving = false; }),
		crl::guard(this, [=] { closeEditor(); }));
}

void Editor::resizeEvent(QResizeEvent *e) {
	Ui::BoxContent::resizeEvent(e);
	updateControlsGeometry();
}

void Editor::updateControlsGeometry() {
	const auto margin = st::boxRowPadding.left();
	const auto narrow = width() < style::ConvertScale(660);
	const auto detailWidth = narrow ? width() : style::ConvertScale(304);
	const auto listWidth = narrow ? width() : width() - detailWidth;
	const auto available = listWidth - margin - st::boxRowPadding.right();
	const auto detailsPage = narrow && _detailsPage;
	if (detailsPage && !_listPageHidden) {
		_listScrollTop = _listScroll->scrollTop();
	}
	const auto restoreScroll = !detailsPage && _listPageHidden;
	_listPageHidden = detailsPage;
	_search->setVisible(!detailsPage);
	_modified->setVisible(!detailsPage);
	_category->setVisible(!detailsPage);
	_caption->setVisible(!detailsPage);
	_listScroll->setVisible(!detailsPage);
	_detailsScroll->setVisible(!narrow || detailsPage);
	_details->setBackVisible(narrow);
	auto top = style::ConvertScale(10);
	const auto modifiedWidth = _modified->naturalWidth();
	const auto stacked = available < style::ConvertScale(440);
	_search->resizeToWidth(stacked ? available
		: available - modifiedWidth - style::ConvertScale(10));
	_search->moveToLeft(margin, top);
	_modified->resizeToWidth(modifiedWidth);
	if (stacked) {
		top += _search->height() + style::ConvertScale(8);
		_modified->moveToLeft(margin, top);
		top += _modified->height() + style::ConvertScale(6);
	} else {
		_modified->moveToLeft(listWidth - st::boxRowPadding.right() - modifiedWidth,
			top + (_search->height() - _modified->height()) / 2);
		top += _search->height() + style::ConvertScale(10);
	}
	_category->moveToLeft(margin, top);
	_caption->resizeToWidth(available - _category->width() - style::ConvertScale(10));
	_caption->moveToLeft(margin + _category->width() + style::ConvertScale(10),
		top + (_category->height() - _caption->height()) / 2);
	top += _category->height() + style::ConvertScale(8);
	if (detailsPage) {
		top = 0;
	}
	const auto contentHeight = std::max(height() - top, 0);
	_listScroll->setGeometry(0, top, listWidth, contentHeight);
	_list->resizeToWidth(listWidth);
	const auto detailTop = narrow ? top : 0;
	_detailsScroll->setGeometry(width() - detailWidth, detailTop,
		detailWidth, height() - detailTop);
	_details->resizeToWidth(detailWidth);
	if (restoreScroll) {
		_listScroll->scrollToY(_listScrollTop);
	}
	const auto scrollTop = _listScroll->scrollTop();
	_list->setVisibleTopBottom(scrollTop, scrollTop + contentHeight);
}

void Editor::showColorList() {
	if (!_details->submitColor()) {
		return;
	}
	_detailsPage = false;
	updateControlsGeometry();
	_search->setFocusFast();
}

void Editor::handleEscape() {
	if (showingDetails()) {
		showColorList();
		return;
	}
	if (!_search->getLastText().isEmpty()) {
		_search->setText(QString());
		return;
	}
	closeWithConfirmation();
}

void Editor::keyPressEvent(QKeyEvent *e) {
	if (e->key() == Qt::Key_Escape) {
		e->accept();
		handleEscape();
		return;
	}
	if (!showingDetails()
		&& (e->key() == Qt::Key_Down || e->key() == Qt::Key_Up)) {
		e->accept();
		_list->selectSkip(e->key() == Qt::Key_Down ? 1 : -1);
		return;
	}
	if (!showingDetails()
		&& (e->key() == Qt::Key_PageDown || e->key() == Qt::Key_PageUp)) {
		e->accept();
		const auto direction = e->key() == Qt::Key_PageDown ? 1 : -1;
		const auto count = std::max(_listScroll->height() / EditorBlock::rowHeight(), 1);
		for (auto i = 0; i != count; ++i) {
			_list->selectSkip(direction);
		}
		return;
	}
	Ui::BoxContent::keyPressEvent(e);
}

void Editor::setInnerFocus() {
	if (showingDetails()) {
		_details->setInnerFocus();
		return;
	}
	_search->setFocusFast();
}

void Editor::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.fillRect(e->rect(), st::boxBg);
	if (!showingDetails()) {
		p.fillRect(0, _listScroll->y(), _listScroll->width(),
			st::lineWidth, st::controlBorder);
	}
	if (width() >= style::ConvertScale(660)) {
		p.fillRect(_detailsScroll->x(), _detailsScroll->y(),
			st::lineWidth, _detailsScroll->height(), st::controlBorder);
	}
}

void Editor::closeWithConfirmation() {
	_details->submitColor();
	if (!PaletteChanged(_paletteContent, _cloud)) {
		Background()->clearEditingTheme(ClearEditing::KeepChanges);
		closeEditor();
		return;
	}
	getDelegate()->show(Ui::MakeConfirmBox({
		.text = tr::lng_theme_editor_sure_close(),
		.confirmed = crl::guard(this, [=](Fn<void()> &&close) {
			Background()->clearEditingTheme(ClearEditing::RevertChanges);
			closeEditor();
			close();
		}),
		.confirmText = tr::lng_close(),
	}), Ui::LayerOption::KeepOther);
}

void Editor::closeEditor() {
	_closing = true;
	closeBox();
}

} // namespace Theme
} // namespace Window
