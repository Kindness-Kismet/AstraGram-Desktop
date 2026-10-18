/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/widgets/multi_select.h"

#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/scroll_area.h"
#include "ui/effects/animations.h"
#include "ui/effects/cross_animation.h"
#include "ui/text/text_options.h"
#include "ui/painter.h"
#include "ui/ui_utility.h"
#include "lang/lang_keys.h"
#include "ui/search_field_controller.h"
#include "base/invoke_queued.h"
#include "ui/layers/box_content.h"
#include "styles/style_td_common.h"
#include "styles/style_widgets.h"

#include <set>

// AyuGram includes
#include "extras/ui/extras_userpic.h"
#include "extras/features/window_material/window_material.h"


namespace Ui {
namespace {

constexpr int kWideScale = 3;

style::MultiSelect contentStyle(const style::MultiSelect &st, bool content) {
	auto result = st;
	if (!content) {
		return result;
	}
	result.field = st::contentSearchField;
	result.field.textFg = st.field.textFg;
	result.field.placeholderFg = st.field.placeholderFg;
	result.padding = QMargins(style::ConvertScale(20), 0,
		style::ConvertScale(20), style::ConvertScale(8));
	result.fieldCancel = st::contentSearchClose;
	result.fieldCancelSkip = st::contentSearchClose.width;
	result.item.height = style::ConvertScale(28);
	result.maxHeight = 2 * result.item.height + result.itemSkip
		+ result.padding.bottom();
	return result;
}

class Item {
public:
	Item(
		const style::MultiSelectItem &st,
		uint64 id,
		const QString &text,
		style::color color,
		MultiSelect::PaintRoundImage &&paintRoundImage);

	uint64 id() const {
		return _id;
	}
	int getWidth() const {
		return _width;
	}
	QRect rect() const {
		return QRect(_x, _y, _width, _st.height);
	}
	bool isOverDelete() const {
		return _overDelete;
	}
	void setActive(bool active) {
		_active = active;
	}
	void setPosition(int x, int y, int outerWidth, int maxVisiblePadding);
	QRect paintArea(int outerWidth) const;

	void setUpdateCallback(Fn<void()> updateCallback) {
		_updateCallback = updateCallback;
	}
	void setText(const QString &text);
	void paint(Painter &p, int outerWidth);

	void mouseMoveEvent(QPoint point);
	void leaveEvent();

	void showAnimated() {
		setVisibleAnimated(true);
	}
	void hideAnimated() {
		setVisibleAnimated(false);
	}
	bool hideFinished() const {
		return (_hiding && !_visibility.animating());
	}


private:
	void setOver(bool over);
	void paintOnce(Painter &p, int x, int y, int outerWidth);
	void paintDeleteButton(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		float64 overOpacity);
	bool paintCached(Painter &p, int x, int y, int outerWidth);
	void prepareCache();
	void setVisibleAnimated(bool visible);

	const style::MultiSelectItem &_st;

	uint64 _id;
	struct SlideAnimation {
		SlideAnimation(
				Fn<void()> updateCallback,
				int fromX,
				int toX,
				int y,
				float64 duration)
			: fromX(fromX)
			, toX(toX)
			, y(y) {
			x.start(updateCallback, fromX, toX, duration);
		}
		Ui::Animations::Simple x;
		int fromX, toX;
		int y;
	};
	std::vector<SlideAnimation> _copies;
	int _x = -1;
	int _y = -1;
	int _width = 0;
	Text::String _text;
	style::color _color;
	bool _over = false;
	QPixmap _cache;
	Ui::Animations::Simple _visibility;
	Ui::Animations::Simple _overOpacity;
	bool _overDelete = false;
	bool _active = false;
	MultiSelect::PaintRoundImage _paintRoundImage;
	Fn<void()> _updateCallback;
	bool _hiding = false;

};

Item::Item(
	const style::MultiSelectItem &st,
	uint64 id,
	const QString &text,
	style::color color,
	MultiSelect::PaintRoundImage &&paintRoundImage)
: _st(st)
, _id(id)
, _color(color)
, _paintRoundImage(std::move(paintRoundImage)) {
	setText(text);
}

void Item::setText(const QString &text) {
	_text.setText(_st.style, text, NameTextOptions());
	_width = _st.height
		+ _st.padding.left()
		+ _text.maxWidth()
		+ _st.padding.right();
	accumulate_min(_width, _st.maxWidth);
}

void Item::paint(Painter &p, int outerWidth) {
	if (!_cache.isNull() && !_visibility.animating()) {
		if (_hiding) {
			return;
		} else {
			_cache = QPixmap();
		}
	}
	if (_copies.empty()) {
		paintOnce(p, _x, _y, outerWidth);
	} else {
		for (auto i = _copies.begin(), e = _copies.end(); i != e;) {
			auto x = int(base::SafeRound(i->x.value(_x)));
			auto y = i->y;
			auto animating = i->x.animating();
			if (animating || (y == _y)) {
				paintOnce(p, x, y, outerWidth);
			}
			if (animating) {
				++i;
			} else {
				i = _copies.erase(i);
				e = _copies.end();
			}
		}
	}
}

void Item::paintOnce(Painter &p, int x, int y, int outerWidth) {
	if (!_cache.isNull()) {
		paintCached(p, x, y, outerWidth);
		return;
	}

	auto radius = std::min(
		ExtrasUserpic::ComputeRadius(_st.height),
		_st.height / 2);
	auto inner = style::rtlrect(
		x + radius,
		y,
		_width - radius,
		_st.height,
		outerWidth);

	auto clipEnabled = p.hasClipping();
	auto clip = clipEnabled ? p.clipRegion() : QRegion();
	p.setClipRect(inner);

	p.setPen(Qt::NoPen);
	p.setBrush(_active ? _st.textActiveBg : _st.textBg);
	{
		PainterHighQualityEnabler hq(p);
		p.drawRoundedRect(
			style::rtlrect(x, y, _width, _st.height, outerWidth),
			radius,
			radius);
	}

	if (clipEnabled) {
		p.setClipRegion(clip);
	} else {
		p.setClipping(false);
	}

	auto overOpacity = _overOpacity.value(_over ? 1. : 0.);
	if (overOpacity < 1.) {
		_paintRoundImage(p, x, y, outerWidth, _st.height);
	}
	if (overOpacity > 0.) {
		paintDeleteButton(p, x, y, outerWidth, overOpacity);
	}

	auto textLeft = _st.height + _st.padding.left();
	auto textWidth = _width - textLeft - _st.padding.right();
	p.setPen(_active ? _st.textActiveFg : _st.textFg);
	_text.drawLeftElided(
		p,
		x + textLeft,
		y + _st.padding.top(),
		textWidth,
		outerWidth);
}

void Item::paintDeleteButton(
		Painter &p,
		int x,
		int y,
		int outerWidth,
		float64 overOpacity) {
	p.setOpacity(overOpacity);

	p.setPen(Qt::NoPen);
	p.setBrush(_color);
	{
		PainterHighQualityEnabler hq(p);
		const auto rect = style::rtlrect(
			x, y, _st.height, _st.height, outerWidth);
		ExtrasUserpic::PaintShape(p, rect);
	}

	CrossAnimation::paint(
		p,
		_st.deleteCross,
		_st.deleteFg,
		x,
		y,
		outerWidth,
		overOpacity);

	p.setOpacity(1.);
}

bool Item::paintCached(Painter &p, int x, int y, int outerWidth) {
	PainterHighQualityEnabler hq(p);

	auto opacity = _visibility.value(_hiding ? 0. : 1.);
	auto height = opacity * _cache.height() / _cache.devicePixelRatio();
	auto width = opacity * _cache.width() / _cache.devicePixelRatio();

	p.setOpacity(opacity);
	p.drawPixmap(
		style::rtlrect(
			x + (_width - width) / 2.,
			y + (_st.height - height) / 2.,
			width,
			height,
			outerWidth),
		_cache);
	p.setOpacity(1.);
	return true;
}

void Item::mouseMoveEvent(QPoint point) {
	if (!_cache.isNull()) {
		return;
	}
	_overDelete = QRect(0, 0, _st.height, _st.height).contains(point);
	setOver(true);
}

void Item::leaveEvent() {
	_overDelete = false;
	setOver(false);
}

void Item::setPosition(int x, int y, int outerWidth, int maxVisiblePadding) {
	if (_x >= 0 && _y >= 0 && (_x != x || _y != y)) {
		// Make an animation if it is not the first setPosition().
		auto found = false;
		auto leftHidden = -_width - maxVisiblePadding;
		auto rightHidden = outerWidth + maxVisiblePadding;
		for (auto i = _copies.begin(), e = _copies.end(); i != e;) {
			if (i->x.animating()) {
				if (i->y == y) {
					i->x.start(_updateCallback, i->toX, x, _st.duration);
					found = true;
				} else {
					i->x.start(
						_updateCallback,
						i->fromX,
						(i->toX > i->fromX) ? rightHidden : leftHidden,
						_st.duration);
				}
				++i;
			} else {
				i = _copies.erase(i);
				e = _copies.end();
			}
		}
		if (_copies.empty()) {
			if (_y == y) {
				auto copy = SlideAnimation(
					_updateCallback,
					_x,
					x,
					_y,
					_st.duration);
				_copies.push_back(std::move(copy));
			} else {
				auto copyHiding = SlideAnimation(
					_updateCallback,
					_x,
					(y > _y) ? rightHidden : leftHidden,
					_y,
					_st.duration);
				_copies.push_back(std::move(copyHiding));
				auto copyShowing = SlideAnimation(
					_updateCallback,
					(y > _y) ? leftHidden : rightHidden,
					x,
					y,
					_st.duration);
				_copies.push_back(std::move(copyShowing));
			}
		} else if (!found) {
			auto copy = SlideAnimation(
				_updateCallback,
				(y > _y) ? leftHidden : rightHidden,
				x,
				y,
				_st.duration);
			_copies.push_back(std::move(copy));
		}
	}
	_x = x;
	_y = y;
}

QRect Item::paintArea(int outerWidth) const {
	if (_copies.empty()) {
		return rect();
	}
	auto yMin = 0, yMax = 0;
	for (const auto &copy : _copies) {
		accumulate_max(yMax, copy.y);
		if (yMin) {
			accumulate_min(yMin, copy.y);
		} else {
			yMin = copy.y;
		}
	}
	return QRect(0, yMin, outerWidth, yMax - yMin + _st.height);
}

void Item::prepareCache() {
	if (!_cache.isNull()) return;

	Assert(!_visibility.animating());
	auto cacheWidth = _width * kWideScale * style::DevicePixelRatio();
	auto cacheHeight = _st.height * kWideScale * style::DevicePixelRatio();
	auto data = QImage(
		cacheWidth,
		cacheHeight,
		QImage::Format_ARGB32_Premultiplied);
	data.fill(Qt::transparent);
	data.setDevicePixelRatio(style::DevicePixelRatio());
	{
		Painter p(&data);
		paintOnce(
			p,
			_width * (kWideScale - 1) / 2,
			_st.height * (kWideScale - 1) / 2,
			cacheWidth);
	}
	_cache = Ui::PixmapFromImage(std::move(data));
}

void Item::setVisibleAnimated(bool visible) {
	_hiding = !visible;
	prepareCache();
	auto from = visible ? 0. : 1.;
	auto to = visible ? 1. : 0.;
	auto transition = visible ? anim::bumpy(1.0625) : anim::linear;
	_visibility.start(_updateCallback, from, to, _st.duration, transition);
}

void Item::setOver(bool over) {
	if (over != _over) {
		_over = over;
		_overOpacity.start(
			_updateCallback,
			_over ? 0. : 1.,
			_over ? 1. : 0.,
			_st.duration);
	}
}

} // namespace

class MultiSelect::Inner : public RpWidget {
public:
	using ScrollCallback = Fn<void(int activeTop, int activeBottom)>;
	Inner(
		QWidget *parent,
		const style::MultiSelect &st,
		rpl::producer<QString> placeholder,
		const QString &query,
		ScrollCallback callback,
		bool content);
	InputField *field() const { return _field; }
	CrossButton *cancelButton() const { return _cancel; }
	void processKeyPress(QKeyEvent *e) { keyPressEvent(e); }

	[[nodiscard]] QString getQuery() const;
	void setQuery(const QString &query);
	bool setInnerFocus();
	void clearQuery();

	void setQueryChangedCallback(Fn<void(const QString &query)> callback);
	void setSubmittedCallback(Fn<void(Qt::KeyboardModifiers)> callback);
	void setCancelledCallback(Fn<void()> callback);
	void setFocusedChangedCallback(Fn<void(bool focused)> callback);
	void setCancelButtonShown(bool shown);

	bool addItemInBunch(std::unique_ptr<Item> item);
	void finishItemsBunch(AddItemWay way);
	void setItemText(uint64 itemId, const QString &text);

	void setItemRemovedCallback(Fn<void(uint64 itemId)> callback);
	void removeItem(uint64 itemId);

	int getItemsCount() const;
	QVector<uint64> getItems() const;
	bool hasItem(uint64 itemId) const;

	void setResizedCallback(Fn<void(int heightDelta)> callback);

	~Inner();

protected:
	int resizeGetHeight(int newWidth) override;

	void paintEvent(QPaintEvent *e) override;
	void enterEventHook(QEnterEvent *e) override;
	void leaveEventHook(QEvent *e) override;
	void mouseMoveEvent(QMouseEvent *e) override;
	void mousePressEvent(QMouseEvent *e) override;
	void keyPressEvent(QKeyEvent *e) override;

private:
	void cancelled();
	void queryChanged();
	void fieldFocused();
	void computeItemsGeometry(int newWidth);
	void updateItemsGeometry();
	void updateFieldGeometry();
	void updateHasAnyItems(bool hasAnyItems);
	void updateSelection(QPoint mousePosition);
	void clearSelection() {
		updateSelection(QPoint(-1, -1));
	}
	void updateCursor();
	void updateHeightStep();
	void finishHeightAnimation();
	enum class ChangeActiveWay {
		Default,
		SkipSetFocus,
	};
	void setActiveItem(
		int active,
		ChangeActiveWay skipSetFocus = ChangeActiveWay::Default);
	void setActiveItemPrevious();
	void setActiveItemNext();

	QMargins itemPaintMargins() const;

	const style::MultiSelect &_st;
	const bool _content;
	Ui::Animations::Simple _iconOpacity;

	ScrollCallback _scrollCallback;

	std::set<uint64> _idsMap;
	std::vector<std::unique_ptr<Item>> _items;
	std::set<std::unique_ptr<Item>> _removingItems;

	int _selected = -1;
	int _active = -1;
	bool _overDelete = false;

	int _fieldLeft = 0;
	int _fieldTop = 0;
	int _fieldWidth = 0;
	const style::InputField _fieldStyle;
	object_ptr<Ui::InputField> _field;
	object_ptr<Ui::CrossButton> _cancel;
	bool _cancelShown = true;

	int _newHeight = 0;
	Ui::Animations::Simple _height;

	Fn<void(const QString &query)> _queryChangedCallback;
	Fn<void(Qt::KeyboardModifiers)> _submittedCallback;
	Fn<void()> _cancelledCallback;
	Fn<void(bool focused)> _focusedChangedCallback;
	Fn<void(uint64 itemId)> _itemRemovedCallback;
	Fn<void(int heightDelta)> _resizedCallback;

};

MultiSelect::MultiSelect(
	QWidget *parent,
	const style::MultiSelect &st,
	rpl::producer<QString> placeholder,
	const QString &query,
	Layout layout)
: RpWidget(parent)
, _st(contentStyle(st, layout == Layout::Content))
, _layout(layout)
, _scroll(this, _st.scroll) {
	const auto scrollCallback = [=](int activeTop, int activeBottom) {
		scrollTo(activeTop, activeBottom);
	};
	_inner = _scroll->setOwnedWidget(object_ptr<Inner>(
		this,
		_st,
		std::move(placeholder),
		query,
		scrollCallback,
		layout == Layout::Content));
	if (_layout == Layout::Content) {
		_field = _inner->field();
		_cancel = _inner->cancelButton();
		_field->setParent(this);
		_cancel->setParent(this);
		_cancel->show(anim::type::instant);
		_field->setObjectName(u"contentSearch.field"_q);
		_cancel->setObjectName(u"contentSearch.close"_q);
		setupContentSearchField(_field);
		_searchExpanded = !query.isEmpty();
		_openSearch = CreateChild<IconButton>(this, st::contentSearchOpen);
		_openSearch->setObjectName(u"contentSearch.open"_q);
		_openSearch->setClickedCallback([=] { expandSearch(); });
		_cancel->setClickedCallback([=] {
			if (!_field->empty()) {
				clearQuery();
				_field->setFocusFast();
			} else {
				collapseSearch();
			}
		});
		_field->cancelled() | rpl::on_next([=] {
			if (_field->empty()) {
				collapseSearch();
			} else {
				clearQuery();
			}
		}, lifetime());
		InvokeQueued(this, [=] {
			for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
				const auto box = dynamic_cast<BoxContent*>(parent);
				if (!box || !box->hasDelegate()) {
					continue;
				}
				_openSearch->deleteLater();
				_openSearch = box->addTopButton(st::contentSearchOpen,
					[=] { expandSearch(); });
				_openSearch->setObjectName(u"contentSearch.open"_q);
				_titleSearch = true;
				resizeToWidth(width());
				box->updateButtonsGeometry();
				break;
			}
		});
	}
	_scroll->installEventFilter(this);
	_inner->setResizedCallback([this](int innerHeightDelta) {
		auto newHeight = resizeGetHeight(width());
		if (innerHeightDelta > 0) {
			_scroll->scrollToY(_scroll->scrollTop() + innerHeightDelta);
		}
		if (newHeight != height()) {
			resize(width(), newHeight);
			if (_resizedCallback) {
				_resizedCallback();
			}
		}
	});
	_inner->setQueryChangedCallback([this](const QString &query) {
		if (_layout == Layout::Inline) {
			_scroll->scrollToY(_scroll->scrollTopMax());
		} else {
			_cancel->setAccessibleName(query.isEmpty()
				? tr::lng_sr_cancel_search(tr::now)
				: tr::lng_sr_clear_search(tr::now));
		}
		if (_queryChangedCallback) {
			_queryChangedCallback(query);
		}
	});

	setAttribute(Qt::WA_OpaquePaintEvent, _st.bg->c.alpha() == 255);
	ExtrasFeatures::WindowMaterial::watchSurface(this);
	ExtrasFeatures::WindowMaterial::watchSurface(_inner);
	auto defaultWidth = _st.item.maxWidth + _st.fieldMinWidth + _st.fieldCancelSkip;
	resizeToWidth(_st.padding.left() + defaultWidth + _st.padding.right());
}

MultiSelect::~MultiSelect() {
	// 输入控件先于其样式和回调所属的标签区销毁。
	if (_layout == Layout::Content) {
		delete _openSearch.data();
		delete _field;
		delete _cancel;
	}
	_scroll.destroy();
}

bool MultiSelect::eventFilter(QObject *o, QEvent *e) {
	if (o == _scroll && e->type() == QEvent::KeyPress) {
		e->ignore();
		return true;
	}
	return false;
}

void MultiSelect::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.fillRect(e->rect(), ExtrasFeatures::WindowMaterial::surfaceColor(this, _st.bg->c));
}

void MultiSelect::keyPressEvent(QKeyEvent *e) {
	if (_layout == Layout::Content && _field->hasFocus()) {
		_inner->processKeyPress(e);
		return;
	}
	RpWidget::keyPressEvent(e);
}

void MultiSelect::scrollTo(int activeTop, int activeBottom) {
	auto scrollTop = _scroll->scrollTop();
	auto scrollHeight = _scroll->height();
	auto scrollBottom = scrollTop + scrollHeight;
	if (scrollTop > activeTop) {
		_scroll->scrollToY(activeTop);
	} else if (scrollBottom < activeBottom) {
		_scroll->scrollToY(activeBottom - scrollHeight);
	}
}

void MultiSelect::setQueryChangedCallback(Fn<void(const QString &query)> callback) {
	_queryChangedCallback = std::move(callback);
}

void MultiSelect::setSubmittedCallback(Fn<void(Qt::KeyboardModifiers)> callback) {
	_inner->setSubmittedCallback(std::move(callback));
}

void MultiSelect::setCancelledCallback(Fn<void()> callback) {
	_inner->setCancelledCallback(std::move(callback));
}

void MultiSelect::setFocusedChangedCallback(Fn<void(bool focused)> callback) {
	_inner->setFocusedChangedCallback(std::move(callback));
}

void MultiSelect::setResizedCallback(Fn<void()> callback) {
	_resizedCallback = std::move(callback);
}

void MultiSelect::setCancelButtonShown(bool shown) {
	_inner->setCancelButtonShown(shown);
}

void MultiSelect::setInnerFocus() {
	if (_layout == Layout::Content) {
		if (_searchExpanded) {
			_field->setFocusFast();
		} else {
			setFocus();
		}
		return;
	}
	if (_inner->setInnerFocus()) {
		_scroll->scrollToY(_scroll->scrollTopMax());
	}
}

void MultiSelect::clearQuery() {
	_inner->clearQuery();
}

QString MultiSelect::getQuery() const {
	return _inner->getQuery();
}

void MultiSelect::setQuery(const QString &query) {
	if (_layout == Layout::Content && !query.isEmpty()) {
		expandSearch();
	}
	_inner->setQuery(query);
}

void MultiSelect::expandSearch() {
	const auto oldHeight = height();
	_searchExpanded = true;
	resizeToWidth(width());
	if (height() != oldHeight && _resizedCallback) {
		_resizedCallback();
	}
	_field->setFocusFast();
}

void MultiSelect::collapseSearch() {
	const auto oldHeight = height();
	_searchExpanded = false;
	setFocus();
	resizeToWidth(width());
	if (height() != oldHeight && _resizedCallback) {
		_resizedCallback();
	}
}

void MultiSelect::updateSearchGeometry(int newWidth) {
	const auto padding = _st.padding.left();
	_field->setGeometryToLeft(padding, style::ConvertScale(4),
		newWidth - padding - _st.padding.right(), _field->height(), newWidth);
	_field->setVisible(_searchExpanded);
	_cancel->moveToRight(_st.padding.right(), style::ConvertScale(4), newWidth);
	_cancel->toggle(_searchExpanded, anim::type::instant);
	if (_openSearch) {
		_openSearch->setVisible(!_searchExpanded);
		if (!_titleSearch) {
			_openSearch->moveToRight(_st.padding.right(), 0, newWidth);
		}
	}
}

void MultiSelect::addItem(uint64 itemId, const QString &text, style::color color, PaintRoundImage paintRoundImage, AddItemWay way) {
	if (addItemInBunch(itemId, text, color, std::move(paintRoundImage))) {
		_inner->finishItemsBunch(way);
	}
}

bool MultiSelect::addItemInBunch(uint64 itemId, const QString &text, style::color color, PaintRoundImage paintRoundImage) {
	return _inner->addItemInBunch(
		std::make_unique<Item>(_st.item, itemId, text, color, std::move(paintRoundImage)));
}

void MultiSelect::finishItemsBunch() {
	_inner->finishItemsBunch(AddItemWay::SkipAnimation);
}

void MultiSelect::setItemRemovedCallback(Fn<void(uint64 itemId)> callback) {
	_inner->setItemRemovedCallback(std::move(callback));
}

void MultiSelect::removeItem(uint64 itemId) {
	_inner->removeItem(itemId);
}

int MultiSelect::getItemsCount() const {
	return _inner->getItemsCount();
}

QVector<uint64> MultiSelect::getItems() const {
	return _inner->getItems();
}

bool MultiSelect::hasItem(uint64 itemId) const {
	return _inner->hasItem(itemId);
}

int MultiSelect::resizeGetHeight(int newWidth) {
	if (newWidth != _inner->width()) {
		_inner->resizeToWidth(newWidth);
	}
	auto newHeight = std::min(_inner->height(), _st.maxHeight);
	const auto top = _layout == Layout::Content
		? (_searchExpanded ? st::contentSearchHeight
			: (_titleSearch ? 0 : st::contentSearchOpen.height))
		: 0;
	_scroll->setGeometryToLeft(0, top, newWidth, newHeight);
	if (_layout == Layout::Content) {
		updateSearchGeometry(newWidth);
	}
	return top + newHeight;
}

MultiSelect::Inner::Inner(
	QWidget *parent,
	const style::MultiSelect &st,
	rpl::producer<QString> placeholder,
	const QString &query,
	ScrollCallback callback,
	bool content)
: RpWidget(parent)
, _st(st)
, _content(content)
, _scrollCallback(std::move(callback))
, _fieldStyle([&] {
	auto result = st.field;
	// 容器统一绘制搜索背景，输入控件只负责文字与光标。
	result.search = false;
	result.textBg = result.textBgActive = st::transparent;
	if (!content) {
		result.border = result.borderActive = result.borderRadius = 0;
	}
	return result;
}())
, _field(this, _fieldStyle, std::move(placeholder), query)
, _cancel(this, _st.fieldCancel) {
	_field->setObjectName(u"multiSearchField"_q);
	_field->customUpDown(true);
	_field->focusedChanges(
	) | rpl::filter(rpl::mappers::_1) | rpl::on_next([=] {
		fieldFocused();
	}, _field->lifetime());
	_field->focusedChanges(
	) | rpl::on_next([=](bool focused) {
		update();
		if (_focusedChangedCallback) {
			_focusedChangedCallback(focused);
		}
	}, _field->lifetime());
	_field->changes(
	) | rpl::on_next([=] {
		queryChanged();
	}, _field->lifetime());
	_field->submits(
	) | rpl::on_next([=](Qt::KeyboardModifiers m) {
		if (_submittedCallback) {
			_submittedCallback(m);
		}
	}, _field->lifetime());
	_field->cancelled(
	) | rpl::on_next([=] {
		cancelled();
	}, _field->lifetime());
	_cancel->setClickedCallback([=] {
		clearQuery();
		_field->setFocus();
	});
	setMouseTracking(true);
}

void MultiSelect::Inner::queryChanged() {
	auto query = getQuery();
	if (!_content) {
		_cancel->toggle(_cancelShown && !query.isEmpty(), anim::type::normal);
	}
	updateFieldGeometry();
	if (_queryChangedCallback) {
		_queryChangedCallback(query);
	}
}

QString MultiSelect::Inner::getQuery() const {
	return _field->getLastText().trimmed();
}

void MultiSelect::Inner::setQuery(const QString &query) {
	_field->setText(query);
	if (const auto last = _field->getLastText(); !last.isEmpty()) {
		_field->setCursorPosition(last.size());
	}
}

bool MultiSelect::Inner::setInnerFocus() {
	if (_active >= 0 || (_content && !_field->isVisible())) {
		setFocus();
	} else if (!_field->hasFocus()) {
		_field->setFocusFast();
		return true;
	}
	return false;
}

void MultiSelect::Inner::clearQuery() {
	_field->setText(QString());
}

void MultiSelect::Inner::setQueryChangedCallback(Fn<void(const QString &query)> callback) {
	_queryChangedCallback = std::move(callback);
}

void MultiSelect::Inner::setSubmittedCallback(
		Fn<void(Qt::KeyboardModifiers)> callback) {
	_submittedCallback = std::move(callback);
}

void MultiSelect::Inner::setCancelledCallback(Fn<void()> callback) {
	_cancelledCallback = std::move(callback);
}

void MultiSelect::Inner::setFocusedChangedCallback(
		Fn<void(bool focused)> callback) {
	_focusedChangedCallback = std::move(callback);
}

void MultiSelect::Inner::setCancelButtonShown(bool shown) {
	if (_cancelShown == shown) {
		return;
	}
	_cancelShown = shown;
	const auto toggled = _cancelShown && !getQuery().isEmpty();
	if (_cancel->toggled() != toggled) {
		_cancel->toggle(toggled, anim::type::instant);
		updateFieldGeometry();
	}
}

void MultiSelect::Inner::updateFieldGeometry() {
	if (_content) {
		return;
	}
	auto fieldFinalWidth = _fieldWidth;
	if (_cancel->toggled()) {
		fieldFinalWidth -= _st.fieldCancelSkip;
	}
	_field->resizeToWidth(fieldFinalWidth);
	_field->moveToLeft(_st.padding.left() + _fieldLeft, _st.padding.top() + _fieldTop);
}

void MultiSelect::Inner::updateHasAnyItems(bool hasAnyItems) {
	_field->setPlaceholderHidden(!_content && hasAnyItems);
	updateCursor();
	_iconOpacity.start([this] {
		rtlupdate(
			_st.padding.left(),
			_st.padding.top() + (_st.field.heightMin - _st.fieldIcon.height()) / 2,
			_st.fieldIcon.width(),
			_st.fieldIcon.height());
	}, hasAnyItems ? 1. : 0., hasAnyItems ? 0. : 1., _st.item.duration);
}

void MultiSelect::Inner::updateCursor() {
	setCursor(_items.empty() ? style::cur_text : (_overDelete ? style::cur_pointer : style::cur_default));
}

void MultiSelect::Inner::setActiveItem(int active, ChangeActiveWay skipSetFocus) {
	if (_active == active) return;

	if (_active >= 0) {
		Assert(_active < _items.size());
		_items[_active]->setActive(false);
	}
	_active = active;
	if (_active >= 0) {
		Assert(_active < _items.size());
		_items[_active]->setActive(true);
	}
	if (skipSetFocus != ChangeActiveWay::SkipSetFocus) {
		setInnerFocus();
	}
	if (_scrollCallback && (!_content || _active >= 0)) {
		auto rect = (_active >= 0) ? _items[_active]->rect() : _field->geometry().translated(-_st.padding.left(), -_st.padding.top());
		_scrollCallback(rect.y(), rect.y() + rect.height() + _st.padding.top() + _st.padding.bottom());
	}
	update();
}

void MultiSelect::Inner::setActiveItemPrevious() {
	if (_active > 0) {
		setActiveItem(_active - 1);
	} else if (_active < 0 && !_items.empty()) {
		setActiveItem(_items.size() - 1);
	}
}

void MultiSelect::Inner::setActiveItemNext() {
	if (_active >= 0 && _active + 1 < _items.size()) {
		setActiveItem(_active + 1);
	} else {
		setActiveItem(-1);
	}
}

int MultiSelect::Inner::resizeGetHeight(int newWidth) {
	if (newWidth <= 0) {
		return height();
	}
	computeItemsGeometry(newWidth);
	if (_content) {
		return _items.empty() ? 0
			: _items.back()->rect().bottom() + 1 + _st.padding.bottom();
	}
	updateFieldGeometry();

	auto cancelLeft = _fieldLeft + _fieldWidth + _st.padding.right() - _cancel->width();
	auto cancelTop = _fieldTop + (_field->height() - _cancel->height()) / 2;
	_cancel->moveToLeft(_st.padding.left() + cancelLeft, _st.padding.top() + cancelTop);

	return _field->y() + _field->height() + _st.padding.bottom();
}

void MultiSelect::Inner::paintEvent(QPaintEvent *e) {
	Painter p(this);

	auto paintRect = e->rect();
	p.fillRect(paintRect, ExtrasFeatures::WindowMaterial::surfaceColor(
		this, _st.bg->c));
	const auto focused = _field->hasFocus();
	const auto capsule = rect().marginsRemoved(QMargins(
		std::max(0, _st.padding.left() - st::searchFieldPadding),
		_st.padding.top(),
		std::max(0, _st.padding.right() - st::searchFieldPadding),
		_st.padding.bottom()));
	if (!_content) {
		InputField::paintSearchBackground(p, capsule, _st.field, focused, underMouse());
	}

	auto offset = QPoint(
		style::RightToLeft() ? _st.padding.right() : _st.padding.left(),
		_st.padding.top());
	p.translate(offset);
	paintRect.translate(-offset);

	auto outerWidth = width() - _st.padding.left() - _st.padding.right();
	auto iconOpacity = _iconOpacity.value(_items.empty() ? 1. : 0.);
	if (!_content && iconOpacity > 0.) {
		p.setOpacity(iconOpacity);
		_st.fieldIcon.paint(p,
			0,
			(_st.field.heightMin - _st.fieldIcon.height()) / 2,
			outerWidth,
			focused ? st::windowActiveTextFg->c : _st.field.placeholderFg->c);
		p.setOpacity(1.);
	}

	auto checkRect = myrtlrect(paintRect);
	auto paintMargins = itemPaintMargins();
	for (auto i = _removingItems.begin(), e = _removingItems.end(); i != e;) {
		auto &item = *i;
		auto itemRect = item->paintArea(outerWidth);
		itemRect = itemRect.marginsAdded(paintMargins);
		if (checkRect.intersects(itemRect)) {
			item->paint(p, outerWidth);
		}
		if (item->hideFinished()) {
			i = _removingItems.erase(i);
			e = _removingItems.end();
		} else {
			++i;
		}
	}
	for (const auto &item : _items) {
		auto itemRect = item->paintArea(outerWidth);
		itemRect = itemRect.marginsAdded(paintMargins);
		if (checkRect.y() + checkRect.height() <= itemRect.y()) {
			break;
		} else if (checkRect.intersects(itemRect)) {
			item->paint(p, outerWidth);
		}
	}
}

QMargins MultiSelect::Inner::itemPaintMargins() const {
	return {
		std::max(_st.itemSkip, _st.padding.left()),
		_st.itemSkip,
		std::max(_st.itemSkip, _st.padding.right()),
		_st.itemSkip,
	};
}

void MultiSelect::Inner::enterEventHook(QEnterEvent *e) {
	RpWidget::enterEventHook(e);
	update();
}

void MultiSelect::Inner::leaveEventHook(QEvent *e) {
	update();
	clearSelection();
}

void MultiSelect::Inner::mouseMoveEvent(QMouseEvent *e) {
	updateSelection(e->pos());
}

void MultiSelect::Inner::keyPressEvent(QKeyEvent *e) {
	if (_active >= 0) {
		Expects(_active < _items.size());
		if (e->key() == Qt::Key_Delete || e->key() == Qt::Key_Backspace) {
			auto itemId = _items[_active]->id();
			setActiveItemNext();
			removeItem(itemId);
		} else if (e->key() == Qt::Key_Left) {
			setActiveItemPrevious();
		} else if (e->key() == Qt::Key_Right) {
			setActiveItemNext();
		} else if (e->key() == Qt::Key_Escape) {
			setActiveItem(-1);
		} else {
			e->ignore();
		}
	} else if (e->key() == Qt::Key_Left || e->key() == Qt::Key_Backspace) {
		setActiveItemPrevious();
	} else {
		e->ignore();
	}
}

void MultiSelect::Inner::cancelled() {
	if (!_content && _cancelledCallback) {
		_cancelledCallback();
	}
}

void MultiSelect::Inner::fieldFocused() {
	setActiveItem(-1, ChangeActiveWay::SkipSetFocus);
}

void MultiSelect::Inner::updateSelection(QPoint mousePosition) {
	auto point = myrtlpoint(mousePosition) - QPoint(_st.padding.left(), _st.padding.top());
	auto selected = -1;
	for (auto i = 0, count = int(_items.size()); i != count; ++i) {
		auto itemRect = _items[i]->rect();
		if (itemRect.y() > point.y()) {
			break;
		} else if (itemRect.contains(point)) {
			point -= itemRect.topLeft();
			selected = i;
			break;
		}
	}
	if (_selected != selected) {
		if (_selected >= 0) {
			Assert(_selected < _items.size());
			_items[_selected]->leaveEvent();
		}
		_selected = selected;
		update();
	}
	auto overDelete = false;
	if (_selected >= 0) {
		_items[_selected]->mouseMoveEvent(point);
		overDelete = _items[_selected]->isOverDelete();
	}
	if (_overDelete != overDelete) {
		_overDelete = overDelete;
		updateCursor();
	}
}

void MultiSelect::Inner::mousePressEvent(QMouseEvent *e) {
	if (_overDelete) {
		Expects(_selected >= 0);
		Expects(_selected < _items.size());
		removeItem(_items[_selected]->id());
	} else if (_selected >= 0) {
		setActiveItem(_selected);
	} else {
		setInnerFocus();
	}
}

bool MultiSelect::Inner::addItemInBunch(std::unique_ptr<Item> item) {
	if (_idsMap.find(item->id()) != _idsMap.cend()) {
		return false;
	}
	auto wasEmpty = _items.empty();
	item->setUpdateCallback([this, item = item.get()] {
		auto itemRect = item->paintArea(width() - _st.padding.left() - _st.padding.top());
		itemRect = itemRect.translated(_st.padding.left(), _st.padding.top());
		itemRect = itemRect.marginsAdded(itemPaintMargins());
		rtlupdate(itemRect);
	});
	_idsMap.insert(item->id());
	_items.push_back(std::move(item));
	if (wasEmpty) {
		updateHasAnyItems(true);
	}
	return true;
}

void MultiSelect::Inner::finishItemsBunch(AddItemWay way) {
	updateItemsGeometry();
	if (way != AddItemWay::SkipAnimation) {
		_items.back()->showAnimated();
	} else {
		_field->finishAnimating();
		finishHeightAnimation();
	}
}

void MultiSelect::Inner::computeItemsGeometry(int newWidth) {
	newWidth -= _st.padding.left() + _st.padding.right();

	auto itemLeft = 0;
	auto itemTop = 0;
	auto widthLeft = newWidth;
	auto maxVisiblePadding = std::max(
		_st.padding.left(),
		_st.padding.right());
	for (const auto &item : _items) {
		auto itemWidth = item->getWidth();
		Assert(itemWidth <= newWidth);
		if (itemWidth > widthLeft) {
			itemLeft = 0;
			itemTop += _st.item.height + _st.itemSkip;
			widthLeft = newWidth;
		}
		item->setPosition(itemLeft, itemTop, newWidth, maxVisiblePadding);
		itemLeft += itemWidth + _st.itemSkip;
		widthLeft -= itemWidth + _st.itemSkip;
	}
	if (_content) {
		return;
	}

	auto fieldMinWidth = _st.fieldMinWidth + _st.fieldCancelSkip;
	Assert(fieldMinWidth <= newWidth);
	if (fieldMinWidth > widthLeft) {
		_fieldLeft = 0;
		_fieldTop = itemTop + _st.item.height + _st.itemSkip;
	} else {
		_fieldLeft = itemLeft + (_items.empty() ? _st.fieldIconSkip : 0);
		_fieldTop = itemTop;
	}
	_fieldWidth = newWidth - _fieldLeft;
}

void MultiSelect::Inner::updateItemsGeometry() {
	auto newHeight = resizeGetHeight(width());
	if (newHeight == _newHeight) return;

	_newHeight = newHeight;
	_height.start([this] { updateHeightStep(); }, height(), _newHeight, _st.item.duration);
}

void MultiSelect::Inner::updateHeightStep() {
	auto newHeight = int(base::SafeRound(_height.value(_newHeight)));
	if (auto heightDelta = newHeight - height()) {
		resize(width(), newHeight);
		if (_resizedCallback) {
			_resizedCallback(heightDelta);
		}
		update();
	}
}

void MultiSelect::Inner::finishHeightAnimation() {
	_height.stop();
	updateHeightStep();
}

void MultiSelect::Inner::setItemText(uint64 itemId, const QString &text) {
	for (const auto &item : _items) {
		if (item->id() == itemId) {
			item->setText(text);
			updateItemsGeometry();
			return;
		}
	}
}

void MultiSelect::Inner::setItemRemovedCallback(Fn<void(uint64 itemId)> callback) {
	_itemRemovedCallback = std::move(callback);
}

void MultiSelect::Inner::setResizedCallback(Fn<void(int heightDelta)> callback) {
	_resizedCallback = std::move(callback);
}

void MultiSelect::Inner::removeItem(uint64 itemId) {
	auto found = false;
	for (auto i = 0, count = int(_items.size()); i != count; ++i) {
		auto &item = _items[i];
		if (item->id() == itemId) {
			found = true;
			clearSelection();

			item->hideAnimated();
			_idsMap.erase(item->id());
			_removingItems.insert(std::move(item));
			_items.erase(_items.begin() + i);

			if (_active == i) {
				_active = -1;
			} else if (_active > i) {
				--_active;
			}

			updateItemsGeometry();
			if (_items.empty()) {
				updateHasAnyItems(false);
			}
			auto point = QCursor::pos();
			if (auto parent = parentWidget()) {
				if (parent->rect().contains(parent->mapFromGlobal(point))) {
					updateSelection(mapFromGlobal(point));
				}
			}
			break;
		}
	}
	if (found && _itemRemovedCallback) {
		_itemRemovedCallback(itemId);
	}
	setInnerFocus();
}

int MultiSelect::Inner::getItemsCount() const {
	return _items.size();
}

QVector<uint64> MultiSelect::Inner::getItems() const {
	auto result = QVector<uint64>();
	result.reserve(_items.size());
	for (const auto &item : _items) {
		result.push_back(item->id());
	}
	return result;
}

bool MultiSelect::Inner::hasItem(uint64 itemId) const {
	return _idsMap.find(itemId) != _idsMap.cend();
}

MultiSelect::Inner::~Inner() = default;

} // namespace Ui
