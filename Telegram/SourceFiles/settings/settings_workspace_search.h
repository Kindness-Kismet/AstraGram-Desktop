#pragma once

#include "base/timer.h"
#include "settings/settings_builder.h"
#include "ui/layers/layer_widget.h"

namespace Ui {
class CrossButton;
class AbstractButton;
class InputField;
class ScrollArea;
class SearchFieldController;
class VerticalLayout;
} // namespace Ui

namespace Settings {

// 设置搜索浮层：铺满窗口并模糊底层界面，输入框浮在上方居中，结果卡片向下展开。
class WorkspaceSearch final : public Ui::LayerWidget {
public:
	WorkspaceSearch(
		not_null<Main::Session*> session,
		std::shared_ptr<std::vector<QString>> recent,
		Fn<void(Builder::SearchEntry)> activate);
	~WorkspaceSearch();

	void parentResized() override;

protected:
	int resizeGetHeight(int newWidth) override;
	void paintEvent(QPaintEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void doSetInnerFocus() override;

private:
	struct IndexedEntry {
		Builder::SearchEntry entry;
		QString path;
		QString titleTerms;
		QString keywordTerms;
		QString pathTerms;
	};
	void refreshBackdrop();
	void rebuildIndex();
	void refreshResults();
	void activateResult(int index);
	void selectResult(int index);
	bool handleKey(QKeyEvent *event);

	const not_null<Main::Session*> _session;
	const std::shared_ptr<std::vector<QString>> _recent;
	const Fn<void(Builder::SearchEntry)> _activate;
	std::unique_ptr<Ui::SearchFieldController> _search;
	Ui::InputField *_field = nullptr;
	Ui::CrossButton *_clear = nullptr;
	Ui::ScrollArea *_scroll = nullptr;
	Ui::VerticalLayout *_list = nullptr;
	std::vector<IndexedEntry> _entries;
	std::vector<int> _results;
	std::vector<Ui::AbstractButton*> _buttons;
	QImage _backdrop;
	base::Timer _backdropTimer;
	QRect _card;
	int _selected = -1;
};

} // namespace Settings
