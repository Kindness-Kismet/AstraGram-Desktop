#pragma once

#include "base/object_ptr.h"
#include "settings/settings_builder.h"
#include "ui/rp_widget.h"

namespace Ui {
class CrossButton;
class AbstractButton;
class InputField;
class ScrollArea;
class SearchFieldController;
class VerticalLayout;
} // namespace Ui

namespace Settings {

class WorkspaceSearch final : public Ui::RpWidget {
public:
	WorkspaceSearch(
		QWidget *parent,
		QWidget *popupParent,
		not_null<Main::Session*> session,
		Fn<void(Builder::SearchEntry)> activate);
	~WorkspaceSearch();
	void updateLayout();
	bool dismiss();
	bool containsGlobalPoint(QPoint point) const;

protected:
	void resizeEvent(QResizeEvent *e) override;
	void hideEvent(QHideEvent *e) override;

private:
	struct IndexedEntry {
		Builder::SearchEntry entry;
		QString path;
		QString titleTerms;
		QString keywordTerms;
		QString pathTerms;
	};
	void rebuildIndex();
	void refreshResults();
	void activateResult(int index);
	void selectResult(int index);
	void hideResults();
	bool handleKey(QKeyEvent *event);

	const not_null<Main::Session*> _session;
	const Fn<void(Builder::SearchEntry)> _activate;
	std::unique_ptr<Ui::SearchFieldController> _search;
	Ui::InputField *_field = nullptr;
	Ui::CrossButton *_clear = nullptr;
	object_ptr<Ui::RpWidget> _popup;
	Ui::ScrollArea *_scroll = nullptr;
	Ui::VerticalLayout *_list = nullptr;
	std::vector<IndexedEntry> _entries;
	std::vector<int> _results;
	std::vector<Ui::AbstractButton*> _buttons;
	std::vector<QString> _recent;
	int _selected = -1;
	bool _focused = false;
	bool _resultsShown = false;
};

} // namespace Settings
