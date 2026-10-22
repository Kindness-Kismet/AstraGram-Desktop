/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "data/data_cloud_themes.h"
#include "window/themes/window_theme_editor_block.h"
#include "ui/layers/box_content.h"
#include "base/object_ptr.h"

#include <QtCore/QSet>

namespace style {
struct colorizer;
} // namespace style

namespace Ui {
class InputField;
class ScrollArea;
class RoundButton;
class FlatLabel;
class Checkbox;
} // namespace Ui

namespace Window {

class Controller;

namespace Theme {

class EditorDetails;

struct ParsedTheme {
	QByteArray palette;
	QByteArray background;
	bool isPng = false;
	bool tiled = false;
};

[[nodiscard]] QByteArray ColorHexString(const QColor &color);
[[nodiscard]] QByteArray ReplaceValueInPaletteContent(
	const QByteArray &content,
	const QByteArray &name,
	const QByteArray &value);
[[nodiscard]] QByteArray WriteCloudToText(const Data::CloudTheme &cloud);
[[nodiscard]] Data::CloudTheme ReadCloudFromText(const QByteArray &text);
[[nodiscard]] QByteArray StripCloudTextFields(const QByteArray &text);
[[nodiscard]] QString EditorColorDescription(const QString &name);

class Editor final : public Ui::BoxContent {
public:
	Editor(QWidget*, not_null<Window::Controller*> window,
		const Data::CloudTheme &cloud);

	[[nodiscard]] static QByteArray ColorizeInContent(
		QByteArray content,
		const style::colorizer &colorizer);
	[[nodiscard]] QString selectedName() const;
	[[nodiscard]] QString selectedValue() const;
	[[nodiscard]] QColor selectedColor() const;
	[[nodiscard]] int visibleColorCount() const;
	[[nodiscard]] int modifiedColorCount() const;
	[[nodiscard]] bool showingDetails() const;
	[[nodiscard]] QByteArray paletteContent() const;
	void selectColor(const QString &name);
	bool applyEditing(const QString &name, const QString &value);
	bool importThemeFile(const QString &path);

protected:
	void prepare() override;
	void paintEvent(QPaintEvent *e) override;
	void resizeEvent(QResizeEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	void setInnerFocus() override;

private:
	void save();
	void showMenu();
	void exportTheme();
	void importTheme();
	void closeEditor();
	void closeWithConfirmation();
	void updateControlsGeometry();
	bool readRows(bool captureBaseline = false);
	bool appendBaselineSource(int index, QSet<QString> &available,
		QByteArray &definitions) const;
	bool applyNewPalette(const QByteArray &content);
	void restoreSelected();
	void updateFilter();
	void updateCaption();
	void updateDetails();
	void showColorList();
	void handleEscape();
	void showError();

	const not_null<Window::Controller*> _window;
	const Data::CloudTheme _cloud;
	QByteArray _paletteContent;
	QByteArray _baselineContent;
	QMap<QString, QString> _baselineValues;
	struct BaselineDefinition {
		QString name;
		QString value;
		int source = -1;
	};
	std::vector<BaselineDefinition> _baselineDefinitions;
	QMap<QString, int> _baselineIndices;
	QSet<QString> _baselineExisting;
	std::vector<EditorColor> _rows;
	QString _selected;
	object_ptr<Ui::InputField> _search;
	object_ptr<Ui::Checkbox> _modified;
	object_ptr<Ui::RoundButton> _category;
	object_ptr<Ui::FlatLabel> _caption;
	object_ptr<Ui::ScrollArea> _listScroll;
	object_ptr<Ui::ScrollArea> _detailsScroll;
	QPointer<EditorBlock> _list;
	QPointer<EditorDetails> _details;
	QPointer<Ui::RoundButton> _save;
	QPointer<Ui::RoundButton> _cancel;
	QPointer<Ui::RoundButton> _menuButton;
	bool _saving = false;
	bool _applyingUpdate = false;
	bool _detailsPage = false;
	bool _closing = false;
	bool _listPageHidden = false;
	int _listScrollTop = 0;
	EditorCategory _currentCategory = EditorCategory::Basic;
};

} // namespace Theme
} // namespace Window
