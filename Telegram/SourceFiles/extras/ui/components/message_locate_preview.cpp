#include "extras/ui/components/message_locate_preview.h"

#include "extras/extras_settings.h"
#include "data/data_session.h"
#include "history/admin_log/history_admin_log_item.h"
#include "history/history.h"
#include "history/history_item.h"
#include "history/history_view_highlight_manager.h"
#include "history/view/history_view_element.h"
#include "history/view/history_view_fake_items.h"
#include "lang_auto.h"
#include "main/main_session.h"
#include "ui/chat/chat_style.h"
#include "ui/chat/chat_theme.h"
#include "ui/painter.h"
#include "window/section_widget.h"
#include "window/themes/window_theme.h"
#include "window/window_session_controller.h"

namespace {

class PreviewDelegate final : public HistoryView::SimpleElementDelegate {
public:
	using SimpleElementDelegate::SimpleElementDelegate;
private:
	HistoryView::Context elementContext() override {
		return HistoryView::Context::ContactPreview;
	}
};

} // namespace

struct MessageLocatePreview::State {
	std::unique_ptr<PreviewDelegate> delegate;
	std::unique_ptr<Ui::ChatStyle> style;
	std::unique_ptr<Ui::ChatTheme> theme;
	AdminLog::OwnedItem item;
	std::unique_ptr<HistoryView::ElementHighlighter> highlighter;
};

MessageLocatePreview::MessageLocatePreview(
		QWidget *parent,
		not_null<Window::SessionController*> controller)
: RpWidget(parent)
, _controller(controller)
, _state(lifetime().make_state<State>()) {
	setObjectName(u"messageLocate/preview"_q);
	_state->delegate = std::make_unique<PreviewDelegate>(
		controller, crl::guard(this, [=] { update(); }));
	_state->style = std::make_unique<Ui::ChatStyle>(
		controller->session().colorIndicesValue());
	_state->style->apply(controller->defaultChatTheme().get());
	_state->theme = Window::Theme::DefaultChatThemeOn(lifetime());
	const auto history = controller->session().data().history(
		PeerData::kServiceNotificationsId);
	_state->item = HistoryView::GenerateItem(
		_state->delegate.get(),
		history,
		HistoryView::GenerateUser(history, tr::extras_MessageLocatePreviewName(tr::now)),
		{},
		tr::extras_MessageLocatePreviewText(tr::now));
	_state->highlighter = std::make_unique<HistoryView::ElementHighlighter>(
		&controller->session().data(),
		[=](const HistoryItem *item) {
			return (item == _state->item->data()) ? _state->item.get() : nullptr;
		},
		[=](const HistoryView::Element*) { update(); });
	widthValue() | rpl::filter([](int width) {
		return width > 0;
	}) | rpl::on_next([=](int width) {
		resize(width, _state->item->resizeGetHeight(width) + style::ConvertScale(32));
	}, lifetime());
	const auto &settings = ExtrasSettings::getInstance();
	rpl::merge(
		settings.messageLocateStyleValue() | rpl::to_empty,
		settings.messageLocateColorValue() | rpl::to_empty,
		settings.messageLocateDurationValue() | rpl::to_empty,
		settings.messageLocateStrokeValue() | rpl::to_empty,
		settings.messageLocateCornerValue() | rpl::to_empty,
		settings.messageLocatePinSizeValue() | rpl::to_empty,
		settings.messageLocateBounceValue() | rpl::to_empty
	) | rpl::on_next([=] {
		replay();
	}, lifetime());
}

void MessageLocatePreview::replay() {
	_state->highlighter->highlight(HistoryView::SelectedQuote{
		.item = _state->item->data().get(),
	});
}

void MessageLocatePreview::paintEvent(QPaintEvent *event) {
	auto p = Painter(this);
	p.setClipRect(event->rect());
	Window::SectionWidget::PaintBackground(
		p, _state->theme.get(), size(), event->rect());
	const auto offset = QPoint(0, style::ConvertScale(16));
	const auto area = rect().translated(-offset);
	auto context = _controller->defaultChatTheme()->preparePaintContext(
		_state->style.get(), area, area, event->rect().translated(-offset), false);
	context.outbg = _state->item->hasOutLayout();
	context.highlight = _state->highlighter->state(_state->item->data());
	p.translate(offset);
	_state->item->draw(p, context);
}
