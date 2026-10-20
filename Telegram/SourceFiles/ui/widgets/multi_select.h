/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#pragma once

#include "base/object_ptr.h"
#include "ui/rp_widget.h"
#include "styles/style_widgets.h"

namespace style {
struct MultiSelect;
} // namespace style

namespace Ui {

class ScrollArea;
class InputField;
class CrossButton;
class IconButton;
class BoxContent;

class MultiSelect : public RpWidget {
public:
	enum class Layout { Content, Inline };
	MultiSelect(
		QWidget *parent,
		const style::MultiSelect &st,
		rpl::producer<QString> placeholder = nullptr,
		const QString &query = QString(),
		Layout layout = Layout::Content);
	~MultiSelect();

	[[nodiscard]] QString getQuery() const;
	void setQuery(const QString &query);
	void setInnerFocus();
	void clearQuery();

	void setQueryChangedCallback(Fn<void(const QString &query)> callback);
	void setSubmittedCallback(Fn<void(Qt::KeyboardModifiers)> callback);
	void setCancelledCallback(Fn<void()> callback);
	void setFocusedChangedCallback(Fn<void(bool focused)> callback);
	void setResizedCallback(Fn<void()> callback);
	void setCancelButtonShown(bool shown);
	void expandSearch();

	enum class AddItemWay {
		Default,
		SkipAnimation,
	};
	using PaintRoundImage = Fn<void(Painter &p, int x, int y, int outerWidth, int size)>;
	void addItem(uint64 itemId, const QString &text, style::color color, PaintRoundImage paintRoundImage, AddItemWay way = AddItemWay::Default);
	bool addItemInBunch(uint64 itemId, const QString &text, style::color color, PaintRoundImage paintRoundImage);
	void finishItemsBunch();
	void setItemText(uint64 itemId, const QString &text);

	void setItemRemovedCallback(Fn<void(uint64 itemId)> callback);
	void removeItem(uint64 itemId);

	int getItemsCount() const;
	QVector<uint64> getItems() const;
	bool hasItem(uint64 itemId) const;

protected:
	void paintEvent(QPaintEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;
	int resizeGetHeight(int newWidth) override;
	bool eventFilter(QObject *o, QEvent *e) override;

private:
	void scrollTo(int activeTop, int activeBottom);

	void updateSearchGeometry(int width);
	void addTitleSearch(not_null<BoxContent*> box);
	void collapseSearch();

	const style::MultiSelect _st;
	const Layout _layout;
	InputField *_field = nullptr;
	CrossButton *_cancel = nullptr;
	QPointer<IconButton> _openSearch;
	bool _searchExpanded = false;
	bool _titleSearch = false;

	object_ptr<Ui::ScrollArea> _scroll;

	class Inner;
	QPointer<Inner> _inner;

	Fn<void()> _resizedCallback;
	Fn<void(const QString &query)> _queryChangedCallback;

};

} // namespace Ui
