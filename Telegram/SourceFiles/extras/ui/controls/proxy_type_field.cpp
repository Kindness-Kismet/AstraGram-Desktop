#include "extras/ui/controls/proxy_type_field.h"

#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/widgets/menu/menu.h"
#include "ui/widgets/menu/menu_item_base.h"
#include "ui/widgets/popup_menu.h"
#include "styles/style_connection_box.h"
#include "styles/style_media_player.h" // mediaPlayerMenuCheck
#include "styles/style_menu_icons.h"
#include "styles/style_widgets.h"

#include <QtGui/QtEvents>

namespace ExtrasUi {
namespace {

using Type = MTP::ProxyData::Type;

struct TypeInfo {
	Type type = Type::None;
	QString name;
	QString about;
	not_null<const style::icon*> icon;
};

[[nodiscard]] std::vector<TypeInfo> TypeInfos() {
	return {
		{
			Type::Socks5,
			u"SOCKS5"_q,
			tr::extras_ProxyTypeSocks5About(tr::now),
			&st::menuIconNetwork,
		},
		{
			Type::Http,
			u"HTTP"_q,
			tr::extras_ProxyTypeHttpAbout(tr::now),
			&st::menuIconLanguage,
		},
		{
			Type::Mtproto,
			u"MTProto"_q,
			tr::extras_ProxyTypeMtprotoAbout(tr::now),
			&st::menuIconSend,
		},
		{
			Type::Web,
			u"Web"_q,
			tr::extras_ProxyTypeWebAbout(tr::now),
			&st::menuIconNewWindow,
		},
	};
}

[[nodiscard]] TypeInfo LookupTypeInfo(Type type) {
	auto infos = TypeInfos();
	const auto i = ranges::find(infos, type, &TypeInfo::type);
	Assert(i != end(infos));
	return std::move(*i);
}

// 类型菜单项：图标、名称与一句说明，当前类型在右侧打勾。
class TypeItem final : public Ui::Menu::ItemBase {
public:
	TypeItem(
		not_null<Ui::Menu::Menu*> parent,
		const style::Menu &st,
		TypeInfo info,
		bool checked,
		Fn<void()> callback);

	not_null<QAction*> action() const override;
	bool isEnabled() const override;
	void handleKeyPress(not_null<QKeyEvent*> e) override;

protected:
	QPoint prepareRippleStartPosition() const override;
	QImage prepareRippleMask() const override;
	int contentHeight() const override;

private:
	void paint(Painter &p);

	const not_null<QAction*> _dummyAction;
	const style::Menu &_st;
	const TypeInfo _info;
	const bool _checked = false;
	const int _height = 0;

};

TypeItem::TypeItem(
	not_null<Ui::Menu::Menu*> parent,
	const style::Menu &st,
	TypeInfo info,
	bool checked,
	Fn<void()> callback)
: ItemBase(parent, st)
, _dummyAction(new QAction(parent))
, _st(st)
, _info(std::move(info))
, _checked(checked)
, _height(st::proxyTypeMenuItemSkip
	+ st.itemStyle.font->height
	+ st::proxyTypeFieldAboutFont->height
	+ st::proxyTypeMenuItemSkip) {
	setAcceptBoth(true);
	fitToMenuWidth();
	setActionTriggered(std::move(callback));

	paintRequest(
	) | rpl::on_next([=] {
		auto p = Painter(this);
		paint(p);
	}, lifetime());

	const auto textWidth = std::max(
		_st.itemStyle.font->width(_info.name),
		st::proxyTypeFieldAboutFont->width(_info.about));
	setMinWidth(std::clamp(
		(_st.itemPadding.left()
			+ textWidth
			+ _st.itemPadding.right()
			+ st::mediaPlayerMenuCheck.width()),
		_st.widthMin,
		_st.widthMax));

	enableMouseSelecting();
}

void TypeItem::paint(Painter &p) {
	const auto selected = isSelected();
	Ui::Menu::PaintItemBackground(
		p,
		_st,
		QRect(0, 0, width(), _height),
		selected);
	paintRipple(p, 0, 0);

	const auto &icon = *_info.icon;
	icon.paint(
		p,
		_st.itemIconPosition.x(),
		(_height - icon.height()) / 2,
		width());

	auto right = _st.itemPadding.right();
	if (_checked) {
		const auto &check = st::mediaPlayerMenuCheck;
		check.paint(
			p,
			width() - right - check.width(),
			(_height - check.height()) / 2,
			width());
		right += check.width();
	}
	const auto left = _st.itemPadding.left();
	const auto available = width() - left - right;
	auto top = st::proxyTypeMenuItemSkip;
	p.setPen(selected ? _st.itemFgOver : _st.itemFg);
	p.setFont(_st.itemStyle.font);
	p.drawTextLeft(
		left,
		top,
		width(),
		_st.itemStyle.font->elided(_info.name, available));
	top += _st.itemStyle.font->height;

	p.setPen(selected ? _st.itemFgShortcutOver : _st.itemFgShortcut);
	p.setFont(st::proxyTypeFieldAboutFont);
	p.drawTextLeft(
		left,
		top,
		width(),
		st::proxyTypeFieldAboutFont->elided(_info.about, available));
}

not_null<QAction*> TypeItem::action() const {
	return _dummyAction;
}

bool TypeItem::isEnabled() const {
	return true;
}

void TypeItem::handleKeyPress(not_null<QKeyEvent*> e) {
	if (!isSelected()) {
		return;
	}
	const auto key = e->key();
	if (key == Qt::Key_Enter || key == Qt::Key_Return) {
		setClicked(Ui::Menu::TriggeredSource::Keyboard);
	}
}

QPoint TypeItem::prepareRippleStartPosition() const {
	return mapFromGlobal(QCursor::pos());
}

QImage TypeItem::prepareRippleMask() const {
	return Ui::Menu::ItemRippleMask(_st, size());
}

int TypeItem::contentHeight() const {
	return _height;
}

} // namespace

ProxyTypeField::ProxyTypeField(
	QWidget *parent,
	rpl::producer<Type> type,
	Fn<void(Type)> chosen)
: RippleButton(parent, st::proxyTypeFieldRipple)
, _chosen(std::move(chosen)) {
	std::move(type) | rpl::on_next([=](Type value) {
		_type = value;
		update();
	}, lifetime());

	setClickedCallback([=] { showMenu(); });
}

int ProxyTypeField::resizeGetHeight(int newWidth) {
	const auto &padding = st::proxyTypeFieldPadding;
	return padding.top()
		+ st::proxyTypeFieldNameFont->height
		+ st::proxyTypeFieldAboutFont->height
		+ padding.bottom();
}

void ProxyTypeField::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	// 展开菜单时与聚焦的输入框同色，悬停时略深。
	const auto opened = (_menu.get() != nullptr);
	const auto &bg = opened
		? st::proxyFormBgActive
		: isOver()
		? st::proxyTypeFieldBgOver
		: st::proxyFormBg;
	const auto radius = st::proxyFormInput.borderRadius;
	{
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(rect(), radius, radius);
	}
	paintRipple(p, 0, 0);

	const auto info = LookupTypeInfo(_type);
	const auto &padding = st::proxyTypeFieldPadding;
	info.icon->paint(
		p,
		padding.left(),
		(height() - info.icon->height()) / 2,
		width());

	const auto &arrow = opened
		? st::proxyTypeFieldArrowOpened
		: st::proxyTypeFieldArrow;
	arrow.paint(
		p,
		width() - padding.right() - arrow.width(),
		(height() - arrow.height()) / 2,
		width());

	const auto left = st::proxyTypeFieldTextLeft;
	const auto available = width()
		- left
		- arrow.width()
		- 2 * padding.right();
	auto top = padding.top();
	p.setPen(st::windowFg);
	p.setFont(st::proxyTypeFieldNameFont);
	p.drawTextLeft(
		left,
		top,
		width(),
		st::proxyTypeFieldNameFont->elided(info.name, available));
	top += st::proxyTypeFieldNameFont->height;

	p.setPen(st::windowSubTextFg);
	p.setFont(st::proxyTypeFieldAboutFont);
	p.drawTextLeft(
		left,
		top,
		width(),
		st::proxyTypeFieldAboutFont->elided(info.about, available));
}

void ProxyTypeField::onStateChanged(State was, StateChangeSource source) {
	RippleButton::onStateChanged(was, source);
	update();
}

QImage ProxyTypeField::prepareRippleMask() const {
	return Ui::RippleAnimation::RoundRectMask(
		size(),
		st::proxyFormInput.borderRadius);
}

void ProxyTypeField::showMenu() {
	_menu = base::make_unique_q<Ui::PopupMenu>(
		this,
		st::popupMenuWithIcons);
	_menu->setDestroyedCallback(crl::guard(this, [=] { update(); }));

	const auto menu = _menu->menu();
	for (auto &info : TypeInfos()) {
		const auto type = info.type;
		const auto checked = (type == _type);
		_menu->addAction(base::make_unique_q<TypeItem>(
			menu,
			menu->st(),
			std::move(info),
			checked,
			[=] { _chosen(type); }));
	}
	_menu->setForceWidth(width());
	_menu->setForcedOrigin(Ui::PanelAnimation::Origin::TopLeft);

	// 菜单可见区域从阴影内侧开始，先算出阴影宽度，再左移与字段对齐。
	const auto position = mapToGlobal(
		QPoint(0, height() + st::proxyTypeMenuSkip));
	if (!_menu->prepareGeometryFor(position)) {
		_menu = nullptr;
		return;
	}
	const auto shadow = _menu->preparedPadding().left();
	_menu->prepareGeometryFor(position - QPoint(shadow, 0));
	_menu->popupPrepared();
	update();
}

} // namespace ExtrasUi
