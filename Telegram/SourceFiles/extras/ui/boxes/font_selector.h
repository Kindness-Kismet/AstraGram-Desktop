#pragma once

#include "boxes/abstract_box.h"
#include "base/binary_guard.h"

struct LanguageId;
struct Font;

namespace Ui {

class MultiSelect;
struct ScrollToRequest;
class VerticalLayout;

} // namespace Ui

namespace Window {

class SessionController;

} // namespace Window

namespace ExtrasUi {

class FontSelectorBox : public Ui::BoxContent
{
public:
	FontSelectorBox(QWidget *, Window::SessionController *controller, Fn<void(QString font)> hook);

	void setInnerFocus() override;
	static base::binary_guard Show(Window::SessionController *controller, const Fn<void(QString font)> hook);

private:
	QString _selectedFont;

protected:
	void prepare() override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void setupTop(not_null<Ui::VerticalLayout*> container);
	[[nodiscard]] int rowsInPage() const;

	Window::SessionController *_controller = nullptr;
	rpl::event_stream<bool> _translateChatTurnOff;
	Fn<void()> _setInnerFocus;
	Fn<Ui::ScrollToRequest(int rows)> _jump;
	Fn<void(QString font)> _hook;

};

}
