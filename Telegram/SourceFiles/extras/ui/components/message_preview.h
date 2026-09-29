#pragma once

#include "ui/rp_widget.h"

namespace Window {
class SessionController;
} // namespace Window

class MessagePreview final : public Ui::RpWidget {
public:
	MessagePreview(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	void setBubbleRadius(int radius);

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	void updateWidgetSize(int width, bool animate = false);
	void refresh();

	const not_null<Window::SessionController*> _controller;

	class PreviewDelegate;
	struct State;
	const not_null<State*> _state;

};
