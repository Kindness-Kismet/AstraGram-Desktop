#pragma once

#include "ui/widgets/buttons.h"
#include "base/object_ptr.h"

namespace ExtrasUi {

class ChatSurfaceButton final : public Ui::FlatButton {
public:
	ChatSurfaceButton(
		QWidget *parent,
		const QString &text,
		const style::FlatButton &st);
	~ChatSurfaceButton();

protected:
	void paintEvent(QPaintEvent *e) override;

private:
	struct Styles;
	ChatSurfaceButton(
		QWidget *parent,
		const QString &text,
		std::unique_ptr<Styles> styles);

	std::unique_ptr<Styles> _styles;
};

[[nodiscard]] object_ptr<Ui::FlatButton> CreateChatSurfaceButton(
	QWidget *parent,
	const QString &text,
	const style::FlatButton &st);

} // namespace ExtrasUi
