#include "settings/settings_card_layout.h"
#include "extras/features/window_material/window_material.h"
#include "settings/settings_common.h"

#include "ui/painter.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_settings.h"

#include <QPainterPath>

namespace Settings {
namespace {

class CardGroup final : public Ui::RpWidget {
public:
	explicit CardGroup(QWidget *parent)
	: RpWidget(parent)
	, _content(Ui::CreateChild<Ui::VerticalLayout>(this))
	, _frame(Ui::CreateChild<Ui::RpWidget>(this)) {
		_content->setProperty("settingsCardGroup", true);
		ExtrasFeatures::WindowMaterial::changes(this) | rpl::on_next([=] {
			updateMaterialClip();
			_frame->update();
		}, lifetime());
		_frame->setAttribute(Qt::WA_TransparentForMouseEvents);
		_frame->paintRequest() | rpl::on_next([=] {
			paintFrame();
		}, _frame->lifetime());
		paintRequest() | rpl::on_next([=] {
			auto p = QPainter(this);
			p.fillRect(rect(), ExtrasFeatures::WindowMaterial::cardColor(this, st::cardBg->c));
		}, lifetime());
		_content->heightValue() | rpl::on_next([=] {
			if (!_resizing) {
				resize(width(), _content->height());
			}
		}, lifetime());
		sizeValue() | rpl::on_next([=](QSize size) {
			updateMaterialClip();
			_frame->resize(size);
			_frame->raise();
		}, lifetime());
	}

	[[nodiscard]] not_null<Ui::VerticalLayout*> content() const {
		return _content;
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
	void updateMaterialClip() {
		if (!ExtrasFeatures::WindowMaterial::isActive(this)) {
			clearMask();
			return;
		}
		// 透明底层无法覆盖子控件的方角，直接裁切整张卡片。
		auto shape = QPainterPath();
		shape.addRoundedRect(QRectF(rect()), st::settingsCardRadius, st::settingsCardRadius);
		setMask(QRegion(shape.toFillPolygon().toPolygon()));
	}

	void paintFrame() {
		auto p = QPainter(_frame);
		auto hq = PainterHighQualityEnabler(p);
		const auto radius = st::settingsCardRadius;
		const auto area = QRectF(rect()).adjusted(.5, .5, -.5, -.5);
		auto shape = QPainterPath();
		shape.addRoundedRect(area, radius, radius);
		auto corners = QPainterPath();
		corners.addRect(rect());
		corners.addPath(shape);
		if (!ExtrasFeatures::WindowMaterial::isActive(this)) {
			p.fillPath(corners, st::dialogsBg);
		}
		p.setPen(st::strokeFg);
		p.setBrush(Qt::NoBrush);
		p.drawPath(shape);

		// 分隔线只连接相邻的操作行，说明和自绘内容保留自己的间距。
		Ui::SettingsButton *previous = nullptr;
		for (auto i = 0; i != _content->count(); ++i) {
			const auto row = dynamic_cast<Ui::SettingsButton*>(
				_content->widgetAt(i).get());
			if (row && previous && row->y() == previous->geometry().bottom() + 1) {
				p.drawLine(
					row->property("settingsSeparatorInset").toInt(),
					row->y(),
					width() - st::lineWidth,
					row->y());
			}
			previous = row;
		}
	}

	const not_null<Ui::VerticalLayout*> _content;
	const not_null<Ui::RpWidget*> _frame;
	bool _resizing = false;
};

} // namespace

CardPage::CardPage(QWidget *parent)
: RpWidget(parent)
, _content(Ui::CreateChild<Ui::VerticalLayout>(this)) {
	ExtrasFeatures::WindowMaterial::watchSurface(this);
	for (auto ancestor = parent; ancestor; ancestor = ancestor->parentWidget()) {
		const auto section = dynamic_cast<AbstractSection*>(ancestor);
		if (!section) {
			continue;
		}
		section->setProperty("settingsCardBackground", true);
		break;
	}
	paintRequest() | rpl::on_next([=] {
		auto p = QPainter(this);
		p.fillRect(rect(), ExtrasFeatures::WindowMaterial::surfaceColor(this, st::dialogsBg->c));
	}, lifetime());
	_content->heightValue() | rpl::on_next([=] {
		if (!_resizing) {
			resizeToWidth(width());
		}
	}, lifetime());
}

not_null<Ui::VerticalLayout*> CardPage::content() const {
	return _content;
}

int CardPage::resizeGetHeight(int newWidth) {
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
	return container->add(
		object_ptr<CardGroup>(container),
		QMargins(0, 0, 0, st::settingsCardGroupSkip))->content();
}

not_null<Ui::FlatLabel*> AddCardTitle(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<QString> title) {
	return container->add(
		object_ptr<Ui::FlatLabel>(container, std::move(title), st::settingsCardTitle),
		st::settingsCardTitlePadding);
}

void AddCardDescription(
	not_null<Ui::VerticalLayout*> container,
	rpl::producer<QString> text) {
	container->add(
		object_ptr<Ui::FlatLabel>(container, std::move(text), st::settingsCardHint),
		st::settingsCardHintPadding);
}

void AddSectionRowDetails(
		not_null<Ui::SettingsButton*> button,
		rpl::producer<QString> description) {
	const auto arrow = Ui::CreateChild<Ui::RpWidget>(button.get());
	arrow->setAttribute(Qt::WA_TransparentForMouseEvents);
	arrow->resize(style::ConvertScale(18), style::ConvertScale(18));
	button->setProperty(
		"settingsRightLabelSkip",
		2 * st::settingsCardRowInset + arrow->width());
	arrow->show();
	arrow->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(arrow);
		auto hq = PainterHighQualityEnabler(p);
		p.setOpacity(.7);
		p.setPen(QPen(st::windowSubTextFg->c, 1.2 * style::ConvertScale(1),
			Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		const auto w = arrow->width();
		p.drawLine(QPointF(w * .4, w * .25), QPointF(w * .65, w * .5));
		p.drawLine(QPointF(w * .65, w * .5), QPointF(w * .4, w * .75));
	}, arrow->lifetime());
	button->sizeValue() | rpl::on_next([=](QSize size) {
		arrow->moveToRight(
			st::settingsCardRowInset,
			(size.height() - arrow->height()) / 2);
	}, arrow->lifetime());
	if (!description) {
		return;
	}
	const auto label = Ui::CreateChild<Ui::FlatLabel>(
		button.get(),
		std::move(description),
		st::settingsCardRowDescription);
	label->setAttribute(Qt::WA_TransparentForMouseEvents);
	label->show();
	button->widthValue() | rpl::on_next([=](int width) {
		label->resizeToWidth(std::max(
			width - st::settingsCardDetailedButton.padding.left()
				- st::settingsCardDetailedButton.padding.right(),
			1));
		label->moveToLeft(
			st::settingsCardDetailedButton.padding.left(),
			st::settingsCardDescriptionTop);
	}, label->lifetime());
}

} // namespace Settings
