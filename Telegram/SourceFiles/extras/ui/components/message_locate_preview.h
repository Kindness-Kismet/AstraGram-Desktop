#pragma once

#include "ui/rp_widget.h"

namespace Window {
class SessionController;
} // namespace Window

class MessageLocatePreview final : public Ui::RpWidget {
public:
	MessageLocatePreview(
		QWidget *parent,
		not_null<Window::SessionController*> controller);
	void replay();

protected:
	void paintEvent(QPaintEvent *event) override;

private:
	struct State;
	const not_null<Window::SessionController*> _controller;
	const not_null<State*> _state;
};
