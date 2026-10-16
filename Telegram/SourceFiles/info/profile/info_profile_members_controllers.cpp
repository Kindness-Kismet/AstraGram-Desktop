/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "info/profile/info_profile_members_controllers.h"

#include "boxes/peers/edit_participants_box.h"
#include "info/profile/info_profile_values.h"
#include "data/data_chat.h"
#include "data/data_user.h"
#include "ui/unread_badge.h"
#include "lang/lang_keys.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "styles/style_info.h"
#include "styles/style_boxes.h"
#include "styles/style_widgets.h"

namespace Info {
namespace Profile {
namespace {

// 头衔最多占名字一行的这个比例，超出时名字先截断。
constexpr auto kTagMaxShare = 0.46;

} // namespace

MemberListRow::MemberListRow(
	not_null<PeerData*> peer,
	Type type)
: PeerListRow(peer)
, _type(type) {
	setType(type);
}

MemberListRow::~MemberListRow() = default;

void MemberListRow::setType(Type type) {
	_type = type;
	_tagRipple = nullptr;
	_removeRipple = nullptr;
	if (_type.canAddTag) {
		_tagMode = TagMode::AddTag;
		_tagText = tr::lng_context_add_my_tag(tr::now);
	} else if (!_type.rank.isEmpty()) {
		_tagMode = (_type.rights == Rights::Admin
			|| _type.rights == Rights::Creator)
			? TagMode::AdminPill
			: TagMode::NormalText;
		_tagText = _type.rank;
	} else if (_type.rights == Rights::Creator) {
		_tagMode = TagMode::AdminPill;
		_tagText = tr::lng_owner_badge(tr::now);
	} else if (_type.rights == Rights::Admin) {
		_tagMode = TagMode::AdminPill;
		_tagText = tr::lng_admin_badge(tr::now);
	} else {
		_tagMode = TagMode::None;
		_tagText = QString();
	}
	_tagTextWidth = _tagText.isEmpty()
		? 0
		: st::memberTagFont->width(_tagText);
	if (_type.canRemove) {
		_removeText = _type.removeText.isEmpty()
			? tr::lng_profile_kick(tr::now)
			: _type.removeText;
		_removeTextWidth = st::normalFont->width(_removeText);
	} else {
		_removeText = QString();
		_removeTextWidth = 0;
	}
}

MemberListRow::Type MemberListRow::type() const {
	return _type;
}

UserData *MemberListRow::user() const {
	return peer()->asUser();
}

void MemberListRow::setRefreshCallback(Fn<void()> callback) {
	_refreshCallback = std::move(callback);
}

bool MemberListRow::tagInteractive() const {
	return _type.canEditTag || _type.canAddTag;
}

QSize MemberListRow::tagSize() const {
	if (_tagTextWidth == 0) {
		return QSize();
	}
	const auto &pad = st::memberTagPadding;
	return QSize(
		pad.left() + _tagTextWidth + pad.right(),
		pad.top() + st::memberTagFont->height + pad.bottom());
}

QSize MemberListRow::rightActionSize() const {
	// 头衔跟在名字后面，右侧只给悬停出现的移除按钮留位置。
	const auto remove = removeSize();
	return remove.isEmpty()
		? QSize()
		: QSize(remove.width(), st::defaultPeerListItem.height);
}

QMargins MemberListRow::rightActionMargins() const {
	const auto skip = st::contactsCheckPosition.x();
	return QMargins(
		skip,
		0,
		st::defaultPeerListItem.photoPosition.x() + skip,
		0);
}

QSize MemberListRow::removeSize() const {
	if (_removeTextWidth == 0) {
		return QSize();
	}
	const auto &p = st::memberTagPillPadding;
	const auto h = p.top() + st::normalFont->height + p.bottom();
	const auto w = p.left() + _removeTextWidth + p.right();
	return QSize(std::max(w, h), h);
}

int MemberListRow::elementsCount() const {
	return _type.canRemove ? 2 : 1;
}

QRect MemberListRow::elementGeometry(int element, int outerWidth) const {
	if (element == kTagElement) {
		return _tagGeometry;
	}
	if (element != kRemoveElement) {
		return QRect();
	}
	const auto size = removeSize();
	if (size.isEmpty()) {
		return QRect();
	}
	const auto right = st::defaultPeerListItem.photoPosition.x()
		+ st::contactsCheckPosition.x();
	return QRect(
		QPoint(
			outerWidth - right - size.width(),
			_nameTop + (st::semiboldFont->height - size.height()) / 2),
		size);
}

bool MemberListRow::elementDisabled(int element) const {
	if (element == kTagElement) {
		return !tagInteractive();
	}
	if (element == kRemoveElement) {
		const auto progress = _hoverAnimation.value(
			_wasHovered ? 1. : 0.);
		return (progress == 0.);
	}
	return false;
}

bool MemberListRow::elementOnlySelect(int element) const {
	return false;
}

void MemberListRow::elementAddRipple(
		int element,
		QPoint point,
		Fn<void()> updateCallback) {
	if (!_refreshCallback) {
		_refreshCallback = updateCallback;
	}
	if (element == kTagElement) {
		if (!tagInteractive()) {
			return;
		}
		if (!_tagRipple) {
			auto mask = Ui::RippleAnimation::RoundRectMask(
				_tagGeometry.size(),
				st::memberTagRadius);
			_tagRipple = std::make_unique<Ui::RippleAnimation>(
				st::defaultLightButton.ripple,
				std::move(mask),
				updateCallback);
		}
		_tagRipple->add(point);
	} else if (element == kRemoveElement) {
		if (!_removeRipple) {
			const auto size = removeSize();
			const auto radius = size.height() / 2;
			auto mask = Ui::RippleAnimation::RoundRectMask(
				size, radius);
			_removeRipple = std::make_unique<Ui::RippleAnimation>(
				st::defaultLightButton.ripple,
				std::move(mask),
				updateCallback);
		}
		_removeRipple->add(point);
	}
}

void MemberListRow::elementsStopLastRipple() {
	if (_tagRipple) {
		_tagRipple->lastStop();
	}
	if (_removeRipple) {
		_removeRipple->lastStop();
	}
}

void MemberListRow::refreshStatus() {
	const auto u = user();
	if (u && u->isBot()) {
		const auto seesAllMessages = (u->botInfo->readsAllHistory
			|| _type.rights != Rights::Normal);
		setCustomStatus(seesAllMessages
			? tr::lng_status_bot_reads_all(tr::now)
			: tr::lng_status_bot_not_reads_all(tr::now));
	} else {
		PeerListRow::refreshStatus();
	}
}

void MemberListRow::checkHoverChanged(bool hovered) {
	if (!_type.canRemove || _wasHovered == hovered) {
		return;
	}
	_wasHovered = hovered;
	_hoverAnimation.start(
		_refreshCallback ? _refreshCallback : Fn<void()>([] {}),
		hovered ? 0. : 1.,
		hovered ? 1. : 0.,
		st::universalDuration);
}

int MemberListRow::pillHeight() const {
	const auto &p = st::memberTagPillPadding;
	return p.top() + st::normalFont->height + p.bottom();
}

const QImage &MemberListRow::ensurePillCircle(const QColor &color) const {
	auto &cache = *_type.circleCache;
	const auto rgba = color.rgba();
	const auto it = cache.find(rgba);
	if (it != end(cache)) {
		return it->second;
	}
	const auto h = pillHeight();
	const auto ratio = style::DevicePixelRatio();
	auto image = QImage(
		QSize(h, h) * ratio,
		QImage::Format_ARGB32_Premultiplied);
	image.setDevicePixelRatio(ratio);
	image.fill(Qt::transparent);
	{
		auto p = QPainter(&image);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(color);
		p.drawEllipse(0, 0, h, h);
	}
	return cache.emplace(rgba, std::move(image)).first->second;
}

void MemberListRow::paintPill(
		Painter &p,
		int x,
		int y,
		int width,
		const QColor &bgColor) const {
	const auto h = pillHeight();
	const auto &circle = ensurePillCircle(bgColor);
	const auto ratio = style::DevicePixelRatio();
	const auto half = h / 2;
	const auto otherHalf = h - half;
	p.drawImage(
		QRect(x, y, half, h),
		circle,
		QRect(0, 0, half * ratio, h * ratio));
	if (width > h) {
		p.fillRect(
			x + half,
			y,
			width - h,
			h,
			bgColor);
	}
	p.drawImage(
		QRect(x + width - otherHalf, y, otherHalf, h),
		circle,
		QRect(half * ratio, 0, otherHalf * ratio, h * ratio));
}

void MemberListRow::paintTag(
		Painter &p,
		QRect geometry,
		int outerWidth,
		bool over) {
	const auto active = over && tagInteractive();
	auto fg = QColor();
	auto bg = QColor(0, 0, 0, 0);
	switch (_tagMode) {
	case TagMode::AdminPill:
		fg = (_type.rights == Rights::Creator)
			? st::rankOwnerFg->c
			: st::rankAdminFg->c;
		bg = fg;
		bg.setAlphaF(active ? 0.16 : 0.12);
		break;
	case TagMode::NormalText:
		fg = st::rankUserFg->c;
		bg = active ? st::controlActiveBg->c : st::controlBg->c;
		break;
	case TagMode::AddTag:
		fg = active ? st::lightButtonFgOver->c : st::lightButtonFg->c;
		if (active) {
			bg = st::lightButtonBgOver->c;
		}
		break;
	case TagMode::None:
		return;
	}
	const auto radius = st::memberTagRadius;
	if (bg.alpha() > 0) {
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(bg);
		p.drawRoundedRect(geometry, radius, radius);
	}
	if (_tagRipple) {
		auto rippleColor = fg;
		rippleColor.setAlphaF(0.12);
		_tagRipple->paint(
			p,
			geometry.x(),
			geometry.y(),
			outerWidth,
			&rippleColor);
		if (_tagRipple->empty()) {
			_tagRipple.reset();
		}
	}
	const auto &pad = st::memberTagPadding;
	const auto &font = st::memberTagFont;
	const auto available = geometry.width() - pad.left() - pad.right();
	const auto text = (_tagTextWidth > available)
		? font->elided(_tagText, available)
		: _tagText;
	p.setFont(font);
	p.setPen(fg);
	p.drawTextLeft(
		geometry.x() + pad.left(),
		geometry.y() + pad.top(),
		outerWidth,
		text);
}

void MemberListRow::paintRemove(
		Painter &p,
		QRect geometry,
		int outerWidth,
		bool over) {
	if (_removeTextWidth == 0) {
		return;
	}
	const auto progress = _hoverAnimation.value(
		_wasHovered ? 1. : 0.);
	if (progress == 0.) {
		return;
	}
	const auto x = geometry.x();
	const auto y = geometry.y();
	const auto &pad = st::memberTagPillPadding;
	const auto h = pillHeight();
	const auto cw = pad.left() + _removeTextWidth + pad.right();
	const auto w = std::max(cw, h);

	auto o = p.opacity();
	p.setOpacity(o * progress);

	if (over) {
		paintPill(p, x, y, w, st::lightButtonBgOver->c);
	}
	if (_removeRipple) {
		const auto color = st::lightButtonBgRipple->c;
		_removeRipple->paint(p, x, y, outerWidth, &color);
		if (_removeRipple->empty()) {
			_removeRipple.reset();
		}
	}
	p.setFont(st::normalFont);
	p.setPen(over ? st::lightButtonFgOver : st::lightButtonFg);
	p.drawTextLeft(
		x + (w - _removeTextWidth) / 2,
		y + pad.top(),
		outerWidth,
		_removeText,
		_removeTextWidth);

	p.setOpacity(o);
}

int MemberListRow::paintNameIconGetWidth(
		Painter &p,
		Fn<void()> repaint,
		crl::time now,
		int nameLeft,
		int nameTop,
		int nameWidth,
		int availableWidth,
		int outerWidth,
		bool selected) {
	_nameTop = nameTop;
	const auto tag = tagSize();
	const auto reserve = tag.isEmpty()
		? 0
		: (st::memberTagSkip
			+ std::min(tag.width(), int(availableWidth * kTagMaxShare)));
	const auto badge = PeerListRow::paintNameIconGetWidth(
		p,
		std::move(repaint),
		now,
		nameLeft,
		nameTop,
		nameWidth,
		availableWidth - reserve,
		outerWidth,
		selected);
	if (tag.isEmpty()) {
		_tagGeometry = QRect();
		return badge;
	}
	// 头衔紧跟名字与徽章，名字放不下时由调用方截断。
	const auto shown = std::clamp(
		nameWidth,
		0,
		std::max(availableWidth - reserve - badge, 0));
	_tagGeometry = QRect(
		nameLeft + shown + badge + st::memberTagSkip,
		nameTop + (st::semiboldFont->height - tag.height()) / 2,
		reserve - st::memberTagSkip,
		tag.height());
	return badge + reserve;
}

int MemberListRow::paintNameIconGetLeadingWidth(
		Painter &p,
		Fn<void()> repaint,
		crl::time now,
		int nameLeft,
		int nameTop,
		int outerWidth,
		bool selected) {
	if (!_refreshCallback) {
		_refreshCallback = repaint;
	}
	return PeerListRow::paintNameIconGetLeadingWidth(
		p,
		std::move(repaint),
		now,
		nameLeft,
		nameTop,
		outerWidth,
		selected);
}

void MemberListRow::elementsPaint(
		Painter &p,
		int outerWidth,
		bool selected,
		int selectedElement) {
	checkHoverChanged(selected || (selectedElement > 0));
	if (_type.canRemove) {
		const auto removeGeometry = elementGeometry(
			kRemoveElement, outerWidth);
		if (!removeGeometry.isEmpty()) {
			paintRemove(
				p,
				removeGeometry,
				outerWidth,
				(selectedElement == kRemoveElement));
		}
	}
	const auto tagGeometry = elementGeometry(
		kTagElement, outerWidth);
	if (!tagGeometry.isEmpty()) {
		paintTag(
			p,
			tagGeometry,
			outerWidth,
			(selectedElement == kTagElement));
	}
}

bool MemberListRow::elementsAnimating() const {
	return _wasHovered || _hoverAnimation.animating();
}

std::unique_ptr<ParticipantsBoxController> CreateMembersController(
		not_null<Window::SessionNavigation*> navigation,
		not_null<PeerData*> peer) {
	return std::make_unique<ParticipantsBoxController>(
		navigation,
		peer,
		ParticipantsBoxController::Role::Profile);
}

} // namespace Profile
} // namespace Info
