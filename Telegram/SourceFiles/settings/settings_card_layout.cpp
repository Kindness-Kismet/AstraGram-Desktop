#include "settings/settings_card_layout.h"
#include "ui/style/style_widget_color.h"
#include "ui/widgets/rp_window.h"
#include "extras/features/window_material/window_material.h"
#include "settings/settings_common.h"
#include "boxes/peer_list_box.h"

#include "ui/painter.h"
#include "ui/boxes/dialog_layout.h"
#include "ui/text/text_utilities.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "ui/wrap/padding_wrap.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"

#include <QtWidgets/QGraphicsEffect>

namespace Settings {
namespace {

class CardClipEffect final : public QGraphicsEffect {
public:
	explicit CardClipEffect(not_null<QWidget*> widget)
	: QGraphicsEffect(widget)
	, _widget(widget) {
	}

protected:
	void draw(QPainter *p) override {
		auto visible = _widget->rect();
		for (auto parent = _widget->parentWidget(); parent; parent = parent->parentWidget()) {
			visible &= QRect(_widget->mapFrom(parent, QPoint()), parent->size());
		}
		if (visible.isEmpty()) {
			return;
		}
		// 长列表只绘制父控件可见的部分，避免按全部会话分配图片。
		const auto offset = visible.topLeft();
		const auto ratio = p->device()->devicePixelRatioF();
		auto source = QPixmap(visible.size() * ratio);
		source.setDevicePixelRatio(ratio);
		source.fill(Qt::transparent);
		{
			auto painter = QPainter(&source);
			painter.translate(-offset);
			_widget->render(&painter, QPoint(), QRegion(), QWidget::DrawChildren);
		}
		const auto rect = QRect(-offset, _widget->size());
		if (_mask.size() != source.size()
			|| _mask.devicePixelRatio() != ratio
			|| _maskRect != rect) {
			_maskRect = rect;
			_mask = QPixmap(source.size());
			_mask.setDevicePixelRatio(ratio);
			_mask.fill(Qt::transparent);
			auto painter = QPainter(&_mask);
			painter.setRenderHint(QPainter::Antialiasing);
			painter.setPen(Qt::NoPen);
			painter.setBrush(Qt::white);
			painter.drawRoundedRect(QRectF(rect),
				st::settingsCardRadius, st::settingsCardRadius);
		}
		// 将整张卡片连同子控件按透明度裁切，避免整数区域造成锯齿。
		{
			auto painter = QPainter(&source);
			painter.setCompositionMode(QPainter::CompositionMode_DestinationIn);
			painter.drawPixmap(0, 0, _mask);
		}
		p->drawPixmap(offset, source);
	}

private:
	const not_null<QWidget*> _widget;
	QPixmap _mask;
	QRect _maskRect;
};

class CardGroup final : public Ui::RpWidget {
public:
	explicit CardGroup(QWidget *parent)
	: RpWidget(parent)
	, _dialog(Ui::IsDialogContent(this))
	, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
		setObjectName(u"settings.card"_q);
		if (!_dialog) {
			setGraphicsEffect(new CardClipEffect(this));
			_content->setProperty("settingsCardGroup", true);
		}
		_content->heightValue() | rpl::on_next([=] {
			if (!_resizing) {
				resize(width(), _content->height());
			}
		}, lifetime());
	}

	[[nodiscard]] not_null<Ui::VerticalLayout*> content() const {
		return _content;
	}

	// 紧挨在上方的组标题，组间细线画在标题之上。
	void setTitle(not_null<QWidget*> title) {
		_title = title.get();
	}
	[[nodiscard]] QWidget *title() const {
		return _title.data();
	}

protected:
	int resizeGetHeight(int newWidth) override {
		_resizing = true;
		_content->resizeToWidth(newWidth);
		_resizing = false;
		return _content->height();
	}

	void visibleTopBottomUpdated(int top, int bottom) override {
		setChildVisibleTopBottom(_content, top, bottom);
	}

private:
	const bool _dialog;
	const not_null<Ui::VerticalLayout*> _content;
	QPointer<QWidget> _title;
	bool _resizing = false;
};

} // namespace

CardPage::CardPage(QWidget *parent)
: RpWidget(parent)
, _dialog(Ui::IsDialogContent(this))
, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	if (!_dialog) {
		ExtrasFeatures::WindowMaterial::watchSurface(this);
		for (auto ancestor = parent; ancestor; ancestor = ancestor->parentWidget()) {
			const auto section = dynamic_cast<AbstractSection*>(ancestor);
			if (!section) {
				continue;
			}
			section->setProperty("settingsCardBackground", true);
			break;
		}
	}
	paintRequest() | rpl::on_next([=] {
		auto p = QPainter(this);
		if (_dialog) {
			p.fillRect(rect(), st::boxBg);
			return;
		}
		p.fillRect(rect(), ExtrasFeatures::WindowMaterial::surfaceColor(this, st::settingsPageBg->c));
		paintGroupDividers(p);
	}, lifetime());
	_content->heightValue() | rpl::on_next([=] {
		if (!_resizing) {
			resizeToWidth(width());
		}
		update();
	}, lifetime());
}

// 相邻分组之间画通栏细线，线位于组间留白正中。
void CardPage::paintGroupDividers(QPainter &p) {
	auto tops = std::vector<int>();
	for (const auto child : _content->findChildren<QWidget*>()) {
		if (child->objectName() != u"settings.card"_q
			|| !child->isVisibleTo(this)) {
			continue;
		}
		const auto group = static_cast<CardGroup*>(child);
		const auto title = group->title();
		const auto anchor = (title && title->isVisibleTo(this))
			? title
			: child;
		tops.push_back(anchor->mapTo(this, QPoint()).y());
	}
	ranges::sort(tops);
	const auto skip = st::settingsCardGroupSkip / 2;
	for (auto i = 1; i < int(tops.size()); ++i) {
		p.fillRect(
			_content->x(),
			tops[i] - skip,
			_content->width(),
			st::lineWidth,
			Ui::ResolveWidgetColor(this, Ui::WindowDividerColor(), Ui::WidgetColorRole::Border));
	}
}

not_null<Ui::VerticalLayout*> CardPage::content() const {
	return _content;
}

int CardPage::resizeGetHeight(int newWidth) {
	if (_dialog) {
		_resizing = true;
		_content->resizeToWidth(newWidth);
		_content->moveToLeft(0, st::boxPadding.top());
		_resizing = false;
		return st::boxPadding.top() + _content->height() + st::boxPadding.bottom();
	}
	const auto padding = (newWidth < st::settingsCardNarrowWidth)
		? st::settingsCardNarrowPadding
		: st::settingsCardPagePadding;
	const auto available = std::max(newWidth - 2 * padding, 1);
	_resizing = true;
	_content->resizeToWidth(available);
	_content->moveToLeft((newWidth - available) / 2, st::settingsCardPageTop);
	_resizing = false;
	return st::settingsCardPageTop
		+ _content->height()
		+ st::settingsCardPageBottom;
}

void CardPage::visibleTopBottomUpdated(int top, int bottom) {
	setChildVisibleTopBottom(_content, top, bottom);
}

not_null<Ui::VerticalLayout*> AddCardGroup(
		not_null<Ui::VerticalLayout*> container) {
	const auto count = container->count();
	const auto previous = count ? container->widgetAt(count - 1).get() : nullptr;
	const auto group = container->add(
		object_ptr<CardGroup>(container),
		QMargins(0, 0, 0, Ui::IsDialogContent(container)
			? st::boxMediumSkip
			: st::settingsCardGroupSkip));
	if (previous && previous->objectName() == u"settings.cardTitle"_q) {
		group->setTitle(previous);
	}
	return group->content();
}

not_null<PeerListContent*> AddCardList(
		not_null<Ui::VerticalLayout*> container,
		not_null<PeerListController*> controller) {
	const auto &padding = controller->computeListSt().padding;
	// 列表自带的上下留白属于旧页面，卡片内部从首行开始绘制。
	const auto content = container->add(object_ptr<Ui::PaddingWrap<PeerListContent>>(
		container,
		object_ptr<PeerListContent>(container, controller),
		QMargins(0, -padding.top(), 0, -padding.bottom())))->entity();
	content->setObjectName(u"settings.cardList"_q);
	content->setRowBackgroundRounding(QMargins(), st::settingsRowRadius);
	return content;
}

not_null<Ui::FlatLabel*> AddCardTitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title) {
	if (Ui::IsDialogContent(container)) {
		return Ui::AddSubsectionTitle(
			container,
			std::move(title),
			QMargins(st::boxRowPadding.left()
				- st::defaultSubsectionTitlePadding.left(), 0, 0, 0));
	}
	const auto label = container->add(
		object_ptr<Ui::FlatLabel>(container, st::settingsCardTitle),
		st::settingsCardTitlePadding);
	label->setObjectName(u"settings.cardTitle"_q);
	// 组标题用强调色加粗，随主题切换。
	rpl::combine(
		std::move(title),
		rpl::single(rpl::empty) | rpl::then(style::PaletteChanged())
	) | rpl::on_next([=](const QString &text, rpl::empty_value) {
		label->setMarkedText(Ui::Text::Bold(text));
		label->setTextColorOverride(st::windowActiveTextFg->c);
	}, label->lifetime());
	return label;
}

not_null<Ui::FlatLabel*> AddCardSubsectionTitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title) {
	if (!IsInCardGroup(container)) {
		return Ui::AddSubsectionTitle(container, std::move(title));
	}
	return container->add(
		object_ptr<Ui::FlatLabel>(
			container,
			std::move(title),
			st::settingsCardSubsectionTitle),
		st::settingsCardSubsectionTitlePadding);
}

void AddCardDescription(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text) {
	if (Ui::IsDialogContent(container)) {
		Ui::AddDialogText(container, std::move(text));
		return;
	}
	container->add(
		object_ptr<Ui::FlatLabel>(container, std::move(text), st::settingsCardHint),
		st::settingsCardHintPadding);
}

void AddSectionRowDetails(
		not_null<Ui::SettingsButton*> button,
		rpl::producer<QString> description) {
	if (Ui::IsDialogContent(button) && !description) {
		return;
	}
	if (!button->property("settingsRightLabelSkip").isValid()) {
		const auto arrow = Ui::CreateChild<Ui::RpWidget>(button.get());
		arrow->setObjectName(u"settings.row.arrow"_q);
		arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
		arrow->resize(st::settingsCardArrowSize, st::settingsCardArrowSize);
		const auto rightSkip = st::settingsCardRowInset
			+ arrow->width() + st::settingsCardTextGap;
		button->setProperty("settingsRightLabelSkip", rightSkip);
		auto padding = button->st().padding;
		padding.setRight(std::max(rightSkip,
			button->property("settingsContentRightSkip").toInt()));
		button->setPaddingOverride(padding);
		arrow->show();
		arrow->paintRequest() | rpl::on_next([=] {
			auto p = QPainter(arrow);
			auto hq = PainterHighQualityEnabler(p);
			p.setPen(QPen(Ui::ResolveWidgetColor(arrow, st::settingsCardSubTextFg->c),
				style::ConvertScale(1), Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
			const auto w = arrow->width();
			const auto top = style::rtlpoint(QPointF(w * .4, w * .25), w);
			const auto middle = style::rtlpoint(QPointF(w * .65, w * .5), w);
			const auto bottom = style::rtlpoint(QPointF(w * .4, w * .75), w);
			p.drawLine(top, middle);
			p.drawLine(middle, bottom);
		}, arrow->lifetime());
		button->sizeValue() | rpl::on_next([=](QSize size) {
			arrow->moveToRight(st::settingsCardRowInset,
				(size.height() - arrow->height()) / 2);
		}, arrow->lifetime());
	}
	if (!description) {
		return;
	}
	const auto label = Ui::CreateChild<Ui::FlatLabel>(button.get(),
		std::move(description), st::settingsCardRowDescription);
	label->setObjectName(u"settings.row.description"_q);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->show();
	const auto computeRightSkip = [=] {
		return std::max({
			button->st().padding.right(),
			button->property("settingsRightLabelSkip").toInt(),
			button->property("settingsContentRightSkip").toInt(),
		});
	};
	auto rightSkip = rpl::single(computeRightSkip()) | rpl::then(
		button->events() | rpl::filter([](not_null<QEvent*> event) {
			return event->type() == QEvent::DynamicPropertyChange;
		}) | rpl::map(computeRightSkip)
	) | rpl::distinct_until_changed();
	rpl::combine(
		button->widthValue(),
		rpl::duplicate(rightSkip)
	) | rpl::on_next([=](int width, int right) {
		const auto left = button->st().padding.left();
		label->resizeToWidth(std::max(width - left - right, 1));
		label->moveToLeft(left, st::settingsCardDescriptionTop);
	}, label->lifetime());
	rpl::combine(
		label->heightValue(),
		std::move(rightSkip)
	) | rpl::on_next([=](int height, int right) {
		const auto &st = button->st();
		auto padding = st.padding;
		padding.setRight(right);
		padding.setBottom(std::max(padding.bottom(),
			st::settingsCardDescriptionTop + height
				+ st::settingsCardRowInset - padding.top() - st.height));
		button->setProperty("settingsContentBottomSkip", padding.bottom());
		button->setPaddingOverride(padding);
	}, label->lifetime());
}

} // namespace Settings
