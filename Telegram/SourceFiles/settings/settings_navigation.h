#pragma once

#include "settings/settings_type.h"
#include "ui/rp_widget.h"
#include "base/object_ptr.h"
#include "base/unique_qptr.h"

namespace Window {
class SessionController;
} // namespace Window

namespace Ui {
class PopupMenu;
class ScrollArea;
class VerticalLayout;
} // namespace Ui

namespace Settings {

class Navigation final : public Ui::RpWidget {
public:
	Navigation(
		QWidget *parent,
		not_null<Window::SessionController*> controller,
		Fn<void(Type)> navigate,
		Fn<void()> close);
	~Navigation();

	void setActive(Type type);
	void setNarrow(bool narrow);

protected:
	void resizeEvent(QResizeEvent *e) override;
	void paintEvent(QPaintEvent *e) override;

private:
	class Item;
	void addItem(
		rpl::producer<QString> title,
		const style::icon &icon,
		const QString &name,
		Type type,
		Fn<void()> callback = nullptr,
		rpl::producer<bool> shown = nullptr);

	Fn<void(Type)> _navigate;
	object_ptr<Ui::RpWidget> _header;
	object_ptr<Ui::ScrollArea> _scroll;
	Ui::VerticalLayout *_list = nullptr;
	std::vector<std::pair<Type, Item*>> _items;
	base::unique_qptr<Ui::PopupMenu> _accountMenu;
};

} // namespace Settings
