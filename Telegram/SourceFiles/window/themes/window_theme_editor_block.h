#pragma once

#include "ui/rp_widget.h"

namespace Window::Theme {

enum class EditorCategory {
	Basic,
	Messages,
	Dialogs,
	Controls,
	Other,
};

struct EditorColor {
	QString name;
	QString title;
	QString description;
	QString value;
	QColor color;
	EditorCategory category = EditorCategory::Other;
	bool existing = false;
	bool modified = false;
	QStringList sources;
};

[[nodiscard]] QString EditorColorTitle(const QString &name);
[[nodiscard]] EditorCategory EditorColorCategory(const QString &name);
[[nodiscard]] QString EditorCategoryTitle(EditorCategory category);

class EditorBlock final : public Ui::RpWidget {
public:
	EditorBlock(QWidget *parent);

	void setRows(std::vector<EditorColor> rows);
	void filterRows(const QString &query, EditorCategory category, bool modified);
	void setActive(const QString &name);
	void chooseRow();
	void selectSkip(int direction);
	[[nodiscard]] int visibleCount() const;
	[[nodiscard]] rpl::producer<QString> chosen() const;
	[[nodiscard]] rpl::producer<int> scrollRequests() const;
	[[nodiscard]] static int rowHeight();

protected:
	void paintEvent(QPaintEvent *e) override;
	int resizeGetHeight(int newWidth) override;
	void mousePressEvent(QMouseEvent *e) override;
	void mouseReleaseEvent(QMouseEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void leaveEventHook(QEvent *e) override;

private:
	void refreshFilter();
	void setSelected(int index);
	[[nodiscard]] int indexAt(QPoint position) const;

	std::vector<EditorColor> _rows;
	std::vector<int> _visible;
	QString _query;
	QString _active;
	EditorCategory _category = EditorCategory::Basic;
	bool _onlyModified = false;
	int _selected = -1;
	int _pressed = -1;
	QBrush _transparent;
	rpl::event_stream<QString> _chosen;
	rpl::event_stream<int> _scrollRequests;
};

} // namespace Window::Theme
