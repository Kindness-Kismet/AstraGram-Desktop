#include "extras/ui/components/chat_surface_button.h"

#include "ui/chat/floating_bar.h"
#include "styles/style_widgets.h"
#include "styles/palette.h"

namespace ExtrasUi {

struct ChatSurfaceButton::Styles {
	explicit Styles(const style::FlatButton &st)
	: source(st)
	, foreground(st) {
		foreground.bgColor = st::transparent;
		foreground.overBgColor = st::transparent;
	}

	const style::FlatButton &source;
	style::FlatButton foreground;
};

ChatSurfaceButton::ChatSurfaceButton(
		QWidget *parent,
		const QString &text,
		const style::FlatButton &st)
: ChatSurfaceButton(parent, text, std::make_unique<Styles>(st)) {
}

ChatSurfaceButton::ChatSurfaceButton(
		QWidget *parent,
		const QString &text,
		std::unique_ptr<Styles> styles)
: FlatButton(parent, text, styles->foreground)
, _styles(std::move(styles)) {
}

ChatSurfaceButton::~ChatSurfaceButton() = default;

void ChatSurfaceButton::paintEvent(QPaintEvent *e) {
	{
		auto p = QPainter(this);
		const auto &st = _styles->source;
		Ui::PaintChatBar(p, this,
			QRect(0, height() - st.height, width(), st.height),
			st.bgColor->c,
			isOver() ? st.overBgColor->c : QColor());
	}
	// 保留原按钮的文字和涟漪绘制。
	FlatButton::paintEvent(e);
}

object_ptr<Ui::FlatButton> CreateChatSurfaceButton(
		QWidget *parent,
		const QString &text,
		const style::FlatButton &st) {
	return object_ptr<Ui::FlatButton>::fromRaw(
		new ChatSurfaceButton(parent, text, st));
}

} // namespace ExtrasUi
