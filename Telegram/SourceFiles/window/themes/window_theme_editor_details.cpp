#include "window/themes/window_theme_editor_details.h"

#include "window/themes/window_theme_editor.h"
#include "boxes/abstract_box.h"
#include "lang/lang_keys.h"
#include "ui/layers/generic_box.h"
#include "ui/abstract_button.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/color_editor.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_widgets.h"
#include "styles/style_window.h"

#include <QtCore/QRegularExpression>

namespace Window::Theme {
namespace {

class ColorSwatch final : public Ui::AbstractButton {
public:
	ColorSwatch(QWidget *parent, QString name, Fn<QColor()> color)
	: AbstractButton(parent)
	, _name(std::move(name))
	, _color(std::move(color))
	, _transparent(style::TransparentPlaceholder()) {
		setCursor(style::cur_pointer);
		setFocusPolicy(Qt::StrongFocus);
		setToolTip(EditorColorTitle(_name));
	}

	QString accessibilityName() override {
		return EditorColorTitle(_name);
	}

protected:
	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		const auto hq = PainterHighQualityEnabler(p);
		auto path = QPainterPath();
		path.addRoundedRect(rect().adjusted(1, 1, -1, -1),
			style::ConvertScale(5), style::ConvertScale(5));
		p.setClipPath(path);
		p.fillRect(rect(), _transparent);
		p.fillRect(rect(), _color());
		p.setClipping(false);
		p.setPen(hasFocus() ? st::windowActiveTextFg : st::controlBorder);
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	}

private:
	QString _name;
	Fn<QColor()> _color;
	QBrush _transparent;
};

} // namespace

EditorDetails::EditorDetails(QWidget *parent)
: RpWidget(parent)
, _back(this, tr::extras_ThemeCreatorBack(), st::defaultBoxButton)
, _title(this, QString(), st::boxTitle)
, _key(this, QString(), st::boxDividerLabel)
, _description(this, QString(), st::boxDividerLabel)
, _colorLabel(this, tr::extras_ThemeCreatorColor(), st::boxDividerLabel)
, _hex(this, st::defaultInputField)
, _picker(this, tr::extras_ThemeCreatorPick(), st::defaultBoxButton)
, _opacityLabel(this, QString(), st::boxDividerLabel)
, _opacity(this, st::defaultContinuousSlider)
, _reference(this, tr::extras_ThemeCreatorReference(), st::defaultBoxButton)
, _referenceNote(this, QString(), st::boxDividerLabel)
, _previewLabel(this, tr::extras_ThemeCreatorPreview(), st::boxDividerLabel)
, _previewNote(this, tr::extras_ThemeCreatorPreviewNote(), st::boxDividerLabel)
, _restore(this, tr::extras_ThemeCreatorRestore(), st::defaultBoxButton) {
	setObjectName(u"theme_creator_details"_q);
	_back->setObjectName(u"theme_creator_back"_q);
	_hex->setObjectName(u"theme_creator_hex"_q);
	_hex->setMaxLength(9);
	_opacity->setObjectName(u"theme_creator_opacity"_q);
	_picker->setObjectName(u"theme_creator_picker"_q);
	_reference->setObjectName(u"theme_creator_reference"_q);
	_restore->setObjectName(u"theme_creator_restore"_q);
	_back->hide();
	_hex->changes() | rpl::on_next([=] {
		if (_updating) {
			return;
		}
		static const auto validHex = QRegularExpression(
			u"^#[0-9a-fA-F]{6}([0-9a-fA-F]{2})?$"_q);
		_valid = validHex.match(_hex->getLastText()).hasMatch();
		if (!_valid) {
			_hex->showErrorNoFocus();
			return;
		}
		_hex->hideError();
	}, lifetime());
	_hex->submits() | rpl::on_next([=] { submitColor(); }, lifetime());
	_hex->focusedChanges() | rpl::on_next([=](bool focused) {
		if (!focused && !_updating && _valid) {
			submitColor();
		}
	}, lifetime());
	_picker->setClickedCallback([=] { openPicker(); });
	_reference->setClickedCallback([=] { chooseReference(); });
	const auto addQuickColor = [&](QString name, const style::color &color) {
		const auto swatch = Ui::CreateChild<ColorSwatch>(
			this, name, [=] { return color->c; });
		swatch->setObjectName(u"theme_creator_quick_"_q + name);
		swatch->setClickedCallback([=] { updateColor(color->c); });
		_quickColors.push_back(swatch);
	};
	addQuickColor(u"windowBg"_q, st::windowBg);
	addQuickColor(u"windowFg"_q, st::windowFg);
	addQuickColor(u"windowBgActive"_q, st::windowBgActive);
	addQuickColor(u"msgInBg"_q, st::msgInBg);
	addQuickColor(u"msgOutBg"_q, st::msgOutBg);
	addQuickColor(u"controlBorder"_q, st::controlBorder);
	_opacity->setAlwaysDisplayMarker(true);
	_opacity->setAdjustCallback([](float64 value) {
		return std::round(value * 255.) / 255.;
	});
	_opacity->setChangeProgressCallback([=](float64 value) {
		auto color = _row.color;
		color.setAlpha(int(std::round(value * 255.)));
		_updating = true;
		_hex->setText(QString::fromLatin1(ColorHexString(color)));
		_opacityLabel->setText(tr::extras_ThemeCreatorOpacity(tr::now)
			+ u"  "_q + QString::number(int(std::round(value * 100.))) + '%');
		_updating = false;
		_previewColor = color;
		update();
	});
	_opacity->setChangeFinishedCallback([=](float64 value) {
		auto color = _row.color;
		color.setAlpha(int(std::round(value * 255.)));
		updateColor(color);
	});
}

void EditorDetails::setCallbacks(Fn<void()> back, Fn<void()> restore) {
	_back->setClickedCallback(std::move(back));
	_restore->setClickedCallback(std::move(restore));
}

void EditorDetails::setBackVisible(bool visible) {
	if (_backVisible == visible) {
		return;
	}
	_backVisible = visible;
	_back->setVisible(visible);
	resizeToWidth(width());
}

rpl::producer<EditorDetails::Change> EditorDetails::changes() const {
	return _changes.events();
}

void EditorDetails::showColor(
		const EditorColor &row,
		std::vector<EditorColor> sources) {
	_row = row;
	_previewColor = row.color;
	_sources = std::move(sources);
	refreshControls();
	resizeToWidth(width());
	update();
}

void EditorDetails::refreshControls() {
	_updating = true;
	_title->setText(_row.title);
	_key->setText(_row.name);
	_key->setBreakEverywhere(true);
	_description->setText(_row.description);
	_hex->setText(QString::fromLatin1(ColorHexString(_row.color)));
	_opacity->setValue(_row.color.alphaF());
	_opacityLabel->setText(tr::extras_ThemeCreatorOpacity(tr::now)
		+ u"  "_q + QString::number(int(std::round(_row.color.alphaF() * 100.))) + '%');
	const auto linked = !_row.value.startsWith('#');
	_hex->setDisabled(linked);
	_opacity->setDisabled(linked);
	_picker->setDisabled(linked);
	for (const auto color : _quickColors) {
		color->setDisabled(linked);
	}
	_referenceNote->setText(linked
		? tr::extras_ThemeCreatorFollowing(tr::now) + u" "_q + _row.value
		: tr::extras_ThemeCreatorIndependent(tr::now));
	_referenceNote->setBreakEverywhere(true);
	_restore->setDisabled(!_row.modified);
	_valid = true;
	_hex->hideError();
	_updating = false;
}

bool EditorDetails::submitColor() {
	if (_updating || _row.name.isEmpty()
		|| !_row.value.startsWith('#')) {
		return true;
	}
	if (!_valid) {
		_hex->showError();
		return false;
	}
	const auto value = _hex->getLastText().toLower();
	if (value == QString::fromLatin1(ColorHexString(_row.color))) {
		return true;
	}
	_changes.fire({ _row.name, value });
	return true;
}

void EditorDetails::setInnerFocus() {
	if (_row.value.startsWith('#')) {
		_hex->setFocusFast();
		return;
	}
	_reference->setFocus();
}

void EditorDetails::updateColor(QColor color) {
	_changes.fire({ _row.name, QString::fromLatin1(ColorHexString(color)) });
}

void EditorDetails::chooseReference() {
	if (!submitColor()) {
		return;
	}
	const auto name = _row.name;
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(tr::extras_ThemeCreatorReference());
		box->setProperty("responsiveBoxWidth", true);
		box->setWidth(st::boxWideWidth);
		box->addRow(object_ptr<Ui::FlatLabel>(box,
			tr::extras_ThemeCreatorReferenceNote(), st::boxDividerLabel));
		const auto add = [=](QString title, QString value, QString objectName) {
			const auto button = box->addRow(object_ptr<Ui::RoundButton>(
				box, rpl::single(std::move(title)), st::defaultBoxButton));
			button->setObjectName(std::move(objectName));
			button->setClickedCallback(crl::guard(this, [=] {
				box->closeBox();
				_changes.fire({ name, value });
			}));
		};
		add(tr::extras_ThemeCreatorIndependent(tr::now),
			QString::fromLatin1(ColorHexString(_row.color)),
			u"theme_creator_reference_independent"_q);
		for (const auto &source : _sources) {
			add(source.title + u" · "_q + source.name,
				source.name, u"theme_creator_source_"_q + source.name);
		}
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}), Ui::LayerOption::KeepOther);
}

void EditorDetails::openPicker() {
	if (!submitColor()) {
		return;
	}
	const auto name = _row.name;
	Ui::show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto picker = box->addRow(object_ptr<ColorEditor>(
			box, ColorEditor::Mode::RGBA, _row.color));
		const auto apply = crl::guard(this, [=] {
			const auto color = picker->color();
			box->closeBox();
			_changes.fire({ name, QString::fromLatin1(ColorHexString(color)) });
		});
		box->setTitle(tr::extras_ThemeCreatorPick());
		box->setWidth(picker->width());
		box->setFocusCallback([=] { picker->setInnerFocus(); });
		picker->submitRequests() | rpl::on_next(apply, picker->lifetime());
		box->addButton(tr::lng_settings_save(), apply);
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}), Ui::LayerOption::KeepOther);
}

int EditorDetails::resizeGetHeight(int newWidth) {
	const auto margin = st::boxRowPadding.left();
	const auto available = newWidth - margin - st::boxRowPadding.right();
	auto top = style::ConvertScale(14);
	const auto place = [&](not_null<Ui::RpWidget*> widget, int skip) {
		widget->resizeToWidth(available);
		widget->moveToLeft(margin, top);
		top += widget->height() + skip;
	};
	if (_backVisible) {
		_back->moveToLeft(margin, top);
		top += _back->height() + style::ConvertScale(8);
	}
	place(_title.data(), style::ConvertScale(2));
	place(_key.data(), style::ConvertScale(4));
	place(_description.data(), style::ConvertScale(8));
	place(_colorLabel.data(), style::ConvertScale(4));
	const auto colorSize = style::ConvertScale(32);
	const auto colorSkip = style::ConvertScale(8);
	const auto pickerWidth = _picker->naturalWidth();
	_hex->resizeToWidth(available - colorSize - colorSkip);
	_hex->moveToLeft(margin + colorSize + colorSkip, top);
	_colorRect = QRect(margin, top + (_hex->height() - colorSize) / 2,
		colorSize, colorSize);
	top += _hex->height() + style::ConvertScale(6);
	place(_opacityLabel.data(), style::ConvertScale(4));
	_opacity->resize(available, style::ConvertScale(18));
	_opacity->moveToLeft(margin, top);
	top += _opacity->height() + style::ConvertScale(8);
	const auto swatchSize = std::min(style::ConvertScale(26),
		(available - pickerWidth - colorSkip) / int(_quickColors.size()));
	for (auto i = 0; i != _quickColors.size(); ++i) {
		_quickColors[i]->setGeometry(margin + i * swatchSize, top,
			swatchSize - style::ConvertScale(3), swatchSize);
	}
	_picker->moveToRight(st::boxRowPadding.right(),
		top + (swatchSize - _picker->height()) / 2);
	top += std::max(swatchSize, _picker->height()) + style::ConvertScale(8);
	_referenceNote->resizeToWidth(available - _reference->width() - colorSkip);
	const auto referenceHeight = std::max(_reference->height(), _referenceNote->height());
	_reference->moveToLeft(margin, top + (referenceHeight - _reference->height()) / 2);
	_referenceNote->moveToLeft(margin + _reference->width() + colorSkip,
		top + (referenceHeight - _referenceNote->height()) / 2);
	top += referenceHeight + style::ConvertScale(10);
	place(_previewLabel.data(), style::ConvertScale(6));
	_previewRect = QRect(margin, top, available, style::ConvertScale(142));
	top += _previewRect.height() + style::ConvertScale(6);
	place(_previewNote.data(), style::ConvertScale(6));
	_restore->moveToLeft(margin, top);
	return top + _restore->height() + style::ConvertScale(14);
}

void EditorDetails::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	p.fillRect(e->rect(), st::boxBg);
	{
		const auto hq = PainterHighQualityEnabler(p);
		auto path = QPainterPath();
		path.addRoundedRect(_colorRect,
			style::ConvertScale(6), style::ConvertScale(6));
		p.save();
		p.setClipPath(path);
		p.fillRect(_colorRect, style::TransparentPlaceholder());
		p.fillRect(_colorRect, _previewColor);
		p.restore();
		p.setPen(st::controlBorder);
		p.setBrush(Qt::NoBrush);
		p.drawPath(path);
	}
	paintPreview(p, _previewRect);
}

void EditorDetails::paintPreview(Painter &p, QRect rect) {
	const auto hq = PainterHighQualityEnabler(p);
	p.save();
	auto clip = QPainterPath();
	clip.addRoundedRect(rect, style::ConvertScale(8), style::ConvertScale(8));
	p.setClipPath(clip);
	p.fillRect(rect, st::windowBg);
	const auto lineHeight = style::ConvertScale(32);
	p.fillRect(rect.x(), rect.y() + lineHeight, rect.width(),
		rect.height() - 2 * lineHeight, st::historyComposeAreaBg);
	p.setFont(st::boxTextFont);
	p.setPen(st::windowFg);
	p.drawText(rect.adjusted(style::ConvertScale(10), 0, 0, 0),
		Qt::AlignTop | Qt::AlignLeft, tr::extras_ThemeCreatorPreviewTitle(tr::now));
	const auto bubble = [&](QRect area, const style::color &bg,
			const style::color &fg, QString text) {
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(area, style::ConvertScale(7), style::ConvertScale(7));
		p.setPen(fg);
		p.drawText(area.adjusted(style::ConvertScale(7), 0, -style::ConvertScale(7), 0),
			Qt::AlignVCenter, st::boxTextFont->elided(text,
				area.width() - style::ConvertScale(14)));
	};
	const auto bubbleWidth = rect.width() - style::ConvertScale(28);
	bubble(QRect(rect.x() + style::ConvertScale(8), rect.y() + lineHeight + style::ConvertScale(6),
		bubbleWidth, style::ConvertScale(28)), st::msgInBg, st::historyTextInFg,
		tr::extras_ThemeCreatorPreviewIncoming(tr::now));
	bubble(QRect(rect.x() + style::ConvertScale(20), rect.y() + lineHeight + style::ConvertScale(40),
		bubbleWidth, style::ConvertScale(28)), st::msgOutBg, st::historyTextOutFg,
		tr::extras_ThemeCreatorPreviewOutgoing(tr::now));
	const auto compose = QRect(rect.x(), rect.bottom() - lineHeight + 1,
		rect.width(), lineHeight);
	p.fillRect(compose, st::historyComposeAreaBg);
	p.setPen(st::windowSubTextFg);
	p.drawText(compose.adjusted(style::ConvertScale(10), 0, 0, 0),
		Qt::AlignVCenter, tr::extras_ThemeCreatorPreviewCompose(tr::now));
	p.restore();
	p.setBrush(Qt::NoBrush);
	p.setPen(st::controlBorder);
	p.drawPath(clip);
}

} // namespace Window::Theme
