/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "chat_helpers/stickers_list_footer.h"
#include "extras/features/window_material/window_material.h"

#include "chat_helpers/emoji_keywords.h"
#include "chat_helpers/stickers_emoji_pack.h"
#include "chat_helpers/stickers_lottie.h"
#include "core/application.h"
#include "data/stickers/data_stickers_set.h"
#include "data/stickers/data_stickers.h"
#include "data/stickers/data_custom_emoji.h"
#include "data/data_file_origin.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_document.h"
#include "data/data_document_media.h"
#include "main/main_app_config.h"
#include "main/main_session.h"
#include "lang/lang_keys.h"
#include "lang/lang_instance.h"
#include "lottie/lottie_single_player.h"
#include "ui/dpr/dpr_icon.h"
#include "ui/dpr/dpr_image.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/buttons.h"
#include "ui/painter.h"
#include "ui/rect_part.h"
#include "styles/style_chat_helpers.h"

#include <QtWidgets/QApplication>

namespace ChatHelpers {
namespace {

constexpr auto kEmojiSectionSetIdBase = uint64(0x77FF'FFFF'FFFF'FFF0ULL);
constexpr auto kEmojiSearchLimit = 32;

using EmojiSection = Ui::Emoji::Section;

QColor CategorySelectionColor(const style::EmojiPan &st) {
	const auto color = st.categoriesBgOver->c;
	const auto gray = qGray(color.rgb());
	return QColor(gray, gray, gray, color.alpha());
}

QString CategoryLabel(EmojiSection section) {
	switch (section) {
	case EmojiSection::Recent: return tr::extras_EmojiCategoryRecent(tr::now);
	case EmojiSection::People: return tr::extras_EmojiCategoryPeople(tr::now);
	case EmojiSection::Nature: return tr::extras_EmojiCategoryNature(tr::now);
	case EmojiSection::Food: return tr::extras_EmojiCategoryFood(tr::now);
	case EmojiSection::Activity: return tr::extras_EmojiCategoryActivity(tr::now);
	case EmojiSection::Travel: return tr::extras_EmojiCategoryTravel(tr::now);
	case EmojiSection::Objects: return tr::extras_EmojiCategoryObjects(tr::now);
	case EmojiSection::Symbols: return tr::extras_EmojiCategorySymbols(tr::now);
	}
	Unexpected("Invalid emoji section.");
}

void UpdateAnimated(anim::value &value, int to) {
	if (int(base::SafeRound(value.to())) == to) {
		return;
	}
	value = anim::value(
		(value.from() != value.to()) ? value.from() : to,
		to);
}

void UpdateAnimated(
		anim::value &value,
		int to,
		ValidateIconAnimations animations) {
	if (animations == ValidateIconAnimations::Full) {
		value.start(to);
	} else {
		value = anim::value(to, to);
	}
}

} // namespace

bool MatchAllPreparedSearchWords(
		const QStringList &titleWords,
		const QStringList &searchWords) {
	for (const auto &searchWord : searchWords) {
		auto found = false;
		for (const auto &titleWord : titleWords) {
			if (titleWord.startsWith(searchWord)) {
				found = true;
				break;
			}
		}
		if (!found) {
			return false;
		}
	}
	return true;
}

uint64 EmojiSectionSetId(EmojiSection section) {
	Expects(section >= EmojiSection::Recent
		&& section <= EmojiSection::Symbols);

	return kEmojiSectionSetIdBase + static_cast<uint64>(section) + 1;
}

uint64 RecentEmojiSectionSetId() {
	return EmojiSectionSetId(EmojiSection::Recent);
}

uint64 AllEmojiSectionSetId() {
	return kEmojiSectionSetIdBase;
}

uint64 SearchEmojiSectionSetId() {
	return kEmojiSectionSetIdBase
		+ static_cast<uint64>(EmojiSection::Symbols)
		+ 2;
}

std::optional<EmojiSection> SetIdEmojiSection(uint64 id) {
	const auto base = RecentEmojiSectionSetId();
	if (id < base) {
		return {};
	}
	const auto index = id - base;
	return (index <= uint64(EmojiSection::Symbols))
		? static_cast<EmojiSection>(index)
		: std::optional<EmojiSection>();
}

[[nodiscard]] std::vector<QString> GifSearchEmojiFallback() {
	return {
		u"\xf0\x9f\x91\x8d"_q,
		u"\xf0\x9f\x98\x98"_q,
		u"\xf0\x9f\x98\x8d"_q,
		u"\xf0\x9f\x98\xa1"_q,
		u"\xf0\x9f\xa5\xb3"_q,
		u"\xf0\x9f\x98\x82"_q,
		u"\xf0\x9f\x98\xae"_q,
		u"\xf0\x9f\x99\x84"_q,
		u"\xf0\x9f\x98\x8e"_q,
		u"\xf0\x9f\x91\x8e"_q,
	};
}

rpl::producer<std::vector<GifSection>> GifSectionsValue(
		not_null<Main::Session*> session) {
	const auto config = &session->appConfig();
	return config->value(
	) | rpl::map([=] {
		return config->get<std::vector<QString>>(
			u"gif_search_emojies"_q,
			GifSearchEmojiFallback());
	}) | rpl::distinct_until_changed(
	) | rpl::map([=](const std::vector<QString> &emoji) {
		const auto list = ranges::views::all(
			emoji
		) | ranges::views::transform([](const QString &val) {
			return Ui::Emoji::Find(val);
		}) | ranges::views::filter([](EmojiPtr emoji) {
			return emoji != nullptr;
		}) | ranges::to_vector;

		const auto pack = &session->emojiStickersPack();
		return rpl::single(
			rpl::empty_value()
		) | rpl::then(
			pack->refreshed()
		) | rpl::map([=, list = std::move(list)] {
			 return list | ranges::views::transform([&](EmojiPtr emoji) {
				const auto document = pack->stickerForEmoji(emoji).document;
				return GifSection{ document, emoji };
			}) | ranges::views::filter([](GifSection section) {
				return (section.document != nullptr);
			}) | ranges::to_vector;
		}) | rpl::distinct_until_changed();
	}) | rpl::flatten_latest();
}

[[nodiscard]] std::vector<EmojiPtr> SearchEmoji(
		const std::vector<QString> &query,
		base::flat_set<EmojiPtr> &outResultSet,
		bool exact) {
	auto result = std::vector<EmojiPtr>();
	const auto pushPlain = [&](EmojiPtr emoji) {
		if (result.size() < kEmojiSearchLimit
			&& outResultSet.emplace(emoji).second) {
			result.push_back(emoji);
		}
		if (const auto original = emoji->original(); original != emoji) {
			outResultSet.emplace(original);
		}
	};
	auto refreshed = false;
	auto &keywords = Core::App().emojiKeywords();
	for (const auto &entry : query) {
		if (const auto emoji = Ui::Emoji::Find(entry)) {
			pushPlain(emoji);
			if (result.size() >= kEmojiSearchLimit) {
				return result;
			}
		} else if (!entry.isEmpty()) {
			if (!refreshed) {
				refreshed = true;
				keywords.refresh();
			}
			const auto list = keywords.queryMine(entry, exact);
			for (const auto &entry : list) {
				pushPlain(entry.emoji);
				if (result.size() >= kEmojiSearchLimit) {
					return result;
				}
			}
		}
	}
	return result;
}

StickerIcon::StickerIcon(uint64 setId) : setId(setId) {
}

StickerIcon::StickerIcon(
	not_null<Data::StickersSet*> set,
	DocumentData *sticker,
	int pixw,
	int pixh)
: setId(set->id)
, set(set)
, sticker(sticker)
, pixw(std::max(pixw, 1))
, pixh(std::max(pixh, 1)) {
}

StickerIcon::StickerIcon(StickerIcon&&) = default;

StickerIcon &StickerIcon::operator=(StickerIcon&&) = default;

StickerIcon::~StickerIcon() = default;

void StickerIcon::ensureMediaCreated() const {
	if (!sticker) {
		return;
	} else if (set->hasThumbnail()) {
		if (!thumbnailMedia) {
			thumbnailMedia = set->createThumbnailView();
			set->loadThumbnail();
		}
	} else if (!stickerMedia) {
		stickerMedia = sticker->createMediaView();
		stickerMedia->thumbnailWanted(sticker->stickerSetOrigin());
	}
}

template <typename UpdateCallback>
StickersListFooter::ScrollState::ScrollState(UpdateCallback &&callback)
: animation([=](crl::time now) {
	callback();
	return animationCallback(now);
}) {
}

bool StickersListFooter::ScrollState::animationCallback(crl::time now) {
	if (anim::Disabled()) {
		now += st::stickerIconMove;
	}
	if (!animationStart) {
		return false;
	}
	const auto dt = (now - animationStart) / float64(st::stickerIconMove);
	if (dt >= 1.) {
		animationStart = 0;
		x.finish();
		selectionX.finish();
		selectionWidth.finish();
		return false;
	}
	x.update(dt, anim::linear);
	selectionX.update(dt, anim::easeOutCubic);
	selectionWidth.update(dt, anim::easeOutCubic);
	return true;
}

GradientPremiumStar::GradientPremiumStar() {
	style::PaletteChanged(
	) | rpl::on_next([=] {
		_image = QImage();
	}, _lifetime);
}

QImage GradientPremiumStar::image() const {
	if (_image.isNull()) {
		renderOnDemand();
	}
	return _image;
}

void GradientPremiumStar::renderOnDemand() const {
	const auto size = st::emojiStatusDefault.size();
	const auto mask = st::emojiStatusDefault.instance(Qt::white);
	const auto factor = style::DevicePixelRatio();
	_image = QImage(
		size * factor,
		QImage::Format_ARGB32_Premultiplied);
	_image.setDevicePixelRatio(factor);

	QPainter p(&_image);
	auto gradient = QLinearGradient(
		QPoint(0, size.height()),
		QPoint(size.width(), 0));
	gradient.setStops({
		{ 0., st::stickerPanPremium1->c },
		{ 1., st::stickerPanPremium2->c },
	});
	p.fillRect(QRect(QPoint(), size), gradient);
	p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
	p.drawImage(QRect(QPoint(), size), mask);
}

StickersListFooter::StickersListFooter(Descriptor &&descriptor)
: InnerFooter(
	descriptor.parent,
	descriptor.st ? *descriptor.st : st::defaultEmojiPan)
, _session(descriptor.session)
, _customTextColor(std::move(descriptor.customTextColor))
, _paused(std::move(descriptor.paused))
, _features(descriptor.features)
, _iconState([=] { update(); })
, _subiconState([=] { update(); })
, _forceFirstFrame(descriptor.forceFirstFrame) {
	setMouseTracking(true);
	setObjectName(u"emoji.categories"_q);

	_iconsLeft = st().iconSkip
		+ (_features.stickersSettings ? st().iconWidth : 0);
	_iconsRight = st().iconSkip;
	Lang::GetInstance().updated(
	) | rpl::on_next([=] {
		refreshIconsGeometry(_activeByScrollId, ValidateIconAnimations::None);
	}, lifetime());

	_session->downloaderTaskFinished(
	) | rpl::on_next([=] {
		update();
	}, lifetime());
}

void StickersListFooter::clearHeavyData() {
	enumerateIcons([&](const IconInfo &info) {
		auto &icon = _icons[info.index];
		icon.webm = nullptr;
		icon.lottie = nullptr;
		icon.lifetime.destroy();
		icon.stickerMedia = nullptr;
		if (!info.visible) {
			icon.savedFrame = QImage();
		}
		return true;
	});
}

void StickersListFooter::paintExpanding(
		Painter &p,
		QRect clip,
		float64 radius,
		RectPart origin) {
	const auto delta = ((origin | RectPart::None) & RectPart::FullBottom)
		? (height() - clip.height())
		: 0;
	const auto shift = QPoint(clip.x(), clip.y() - delta);
	p.translate(shift);
	const auto context = ExpandingContext{
		.clip = clip.translated(-shift),
		.progress = clip.height() / float64(height()),
		.radius = int(std::ceil(radius)),
		.expanding = true,
	};
	paint(p, context);
	p.translate(-shift);
	p.setClipping(false);
}

int StickersListFooter::IconFrameSize() {
	return Data::FrameSizeFromTag(
		Data::CustomEmojiManager::SizeTag::SetIcon
	) / style::DevicePixelRatio();
}

#ifdef _DEBUG
std::vector<StickersListFooter::DebugItem> StickersListFooter::debugItems() const {
	auto result = std::vector<DebugItem>();
	const auto physical = [&](QRect rect) {
		if (rtl()) rect.moveLeft(width() - rect.x() - rect.width());
		return rect;
	};
	const auto viewport = physical(QRect(_iconsLeft, _iconsTop,
		width() - _iconsLeft - _iconsRight, st().footer));
	const auto add = [&](uint64 id, int left, int itemWidth,
			QRect clip, bool active) {
		const auto rect = physical(QRect(left, _iconsTop, itemWidth, st().footer));
		const auto section = SetIdEmojiSection(id);
		result.push_back({ id, rect, rect.intersected(clip),
			(section && categoryActiveWeight(*section) > 0.)
				? _categoryLabels[int(*section)] : QString(), active });
	};
	enumerateIcons([&](const IconInfo &info) {
		const auto id = _icons[info.index].setId;
		if (id == AllEmojiSectionSetId() && info.width > _singleWidth) {
			const auto clip = physical(QRect(info.adjustedLeft, _iconsTop,
				info.width, st().footer).marginsRemoved(
					{ st::emojiIconSelectSkip, 0, st::emojiIconSelectSkip, 0 }
				)).intersected(viewport);
			enumerateSubicons([&](const IconInfo &sub) {
				add(EmojiSectionSetId(EmojiSection(sub.index + 1)),
					info.adjustedLeft + sub.adjustedLeft, sub.width, clip,
					_subiconsExpanded && _subiconState.selected == sub.index);
				return true;
			});
		} else {
			add(id, info.adjustedLeft, info.width, viewport,
				info.index == _iconState.selected);
		}
		return true;
	});
	return result;
}
#endif

void StickersListFooter::enumerateVisibleIcons(
		Fn<void(const IconInfo &)> callback) const {
	enumerateIcons([&](const IconInfo &info) {
		if (info.visible) {
			callback(info);
		} else if (info.adjustedLeft > 0) {
			return false;
		}
		return true;
	});
}

void StickersListFooter::enumerateIcons(
		Fn<bool(const IconInfo &)> callback) const {
	auto left = 0;
	const auto iconsX = int(base::SafeRound(_iconState.x.current()));
	const auto shift = _iconsLeft - iconsX;
	const auto emojiId = AllEmojiSectionSetId();
	const auto right = width();
	for (auto i = 0, count = int(_icons.size()); i != count; ++i) {
		const auto &icon = _icons[i];
		const auto section = SetIdEmojiSection(icon.setId);
		const auto width = int(base::SafeRound((icon.setId == emojiId)
			? _subiconsWidthAnimation.value(_subiconsExpanded
				? _subiconsWidth
				: _singleWidth)
			: section
			? categoryItemWidth(*section)
			: _singleWidth));
		const auto shifted = shift + left;
		const auto visible = (shifted + width > 0 && shifted < right);
		const auto result = callback({
			.index = i,
			.left = left,
			.adjustedLeft = shifted,
			.width = width,
			.visible = visible,
		});
		if (!result) {
			break;
		}
		left += width;
	}
}

void StickersListFooter::enumerateSubicons(
		Fn<bool(const IconInfo &)> callback) const {
	auto left = 0;
	const auto iconsX = int(base::SafeRound(_subiconState.x.current()));
	const auto shift = -iconsX;
	const auto right = _subiconsWidth;
	using Section = Ui::Emoji::Section;
	for (auto i = int(Section::People); i <= int(Section::Symbols); ++i) {
		const auto itemWidth = categoryItemWidth(Section(i));
		const auto shifted = shift + left;
		const auto visible = (shifted + itemWidth > 0 && shifted < right);
		const auto result = callback({
			.index = i - int(Section::People),
			.left = left,
			.adjustedLeft = shifted,
			.width = itemWidth,
			.visible = visible,
		});
		if (!result) {
			break;
		}
		left += itemWidth;
	}
}

auto StickersListFooter::iconInfo(int index) const -> IconInfo {
	if (index < 0) {
		const auto iconsX = int(base::SafeRound(_iconState.x.current()));
		return {
			.index = -1,
			.left = -_singleWidth - _iconsLeft,
			.adjustedLeft = -_singleWidth - _iconsLeft - iconsX,
			.width = _singleWidth,
			.visible = false,
		};
	}
	auto result = IconInfo();
	enumerateIcons([&](const IconInfo &info) {
		if (info.index == index) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

auto StickersListFooter::subiconInfo(int index) const -> IconInfo {
	auto result = IconInfo();
	enumerateSubicons([&](const IconInfo &info) {
		if (info.index == index) {
			result = info;
			return false;
		}
		return true;
	});
	return result;
}

void StickersListFooter::preloadImages() {
	enumerateVisibleIcons([&](const IconInfo &info) {
		const auto &icon = _icons[info.index];
		if (const auto sticker = icon.sticker) {
			Assert(icon.set != nullptr);
			if (icon.set->hasThumbnail()) {
				icon.set->loadThumbnail();
			} else {
				sticker->loadThumbnail(sticker->stickerSetOrigin());
			}
		}
	});
}

void StickersListFooter::validateSelectedIcon(
		uint64 setId,
		ValidateIconAnimations animations) {
	_activeByScrollId = setId;
	refreshCategoryLabel(setId, animations);

	using EmojiSection = Ui::Emoji::Section;
	auto favedIconIndex = -1;
	auto newSelected = -1;
	auto newSubSelected = -1;
	const auto emojiSection = SetIdEmojiSection(setId);
	const auto isEmojiSection = emojiSection.has_value()
		&& (emojiSection != EmojiSection::Recent);
	const auto allEmojiSetId = AllEmojiSectionSetId();
	for (auto i = 0, l = int(_icons.size()); i != l; ++i) {
		if (_icons[i].setId == setId
			|| (_icons[i].setId == Data::Stickers::FavedSetId
				&& setId == Data::Stickers::RecentSetId)) {
			newSelected = i;
			break;
		} else if (_icons[i].setId == Data::Stickers::FavedSetId
			&& setId != SearchEmojiSectionSetId()) {
			favedIconIndex = i;
		} else if (isEmojiSection && _icons[i].setId == allEmojiSetId) {
			newSelected = i;
			newSubSelected = setId - EmojiSectionSetId(EmojiSection::People);
		}
	}
	setSelectedIcon(
		(newSelected >= 0
			? newSelected
			: (favedIconIndex >= 0)
			? favedIconIndex
			: -1),
		animations);
	setSelectedSubicon(
		(newSubSelected >= 0 ? newSubSelected : 0),
		animations);
}

void StickersListFooter::updateEmojiSectionWidth() {
	const auto expanded = (_iconState.selected >= 0)
		&& (_iconState.selected < _icons.size())
		&& (_icons[_iconState.selected].setId == AllEmojiSectionSetId());
	if (_subiconsExpanded == expanded) {
		return;
	}
	const auto from = _subiconsWidthAnimation.value(
		_subiconsExpanded ? _subiconsWidth : _singleWidth);
	_subiconsExpanded = expanded;
	_subiconsWidthAnimation.start(
		[=] { updateEmojiWidthCallback(); },
		from,
		expanded ? _subiconsWidth : _singleWidth,
		st::slideDuration);
}

void StickersListFooter::updateEmojiWidthCallback() {
	refreshScrollableDimensions();
	const auto info = iconInfo(_iconState.selected);
	UpdateAnimated(_iconState.selectionX, info.left);
	UpdateAnimated(_iconState.selectionWidth, info.width);
	if (_iconState.selected >= 0 && !_iconState.dragging) {
		const auto target = std::clamp(
			info.left + (info.width - width() + _iconsLeft + _iconsRight) / 2,
			0, _iconState.max);
		if (_iconState.animation.animating()) {
			UpdateAnimated(_iconState.x, target);
		} else {
			_iconState.x = anim::value(target, target);
		}
	}
	if (_iconState.animation.animating()) {
		_iconState.animationCallback(crl::now());
	}
	update();
}

void StickersListFooter::setSelectedIcon(
		int newSelected,
		ValidateIconAnimations animations) {
	if (_iconState.selected == newSelected) {
		return;
	}
	if ((_iconState.selected < 0) != (newSelected < 0)) {
		animations = ValidateIconAnimations::None;
	}
	_iconState.selected = newSelected;
	updateEmojiSectionWidth();
	if (animations == ValidateIconAnimations::None) {
		_subiconsWidthAnimation.stop();
	}
	refreshScrollableDimensions();
	const auto info = iconInfo(_iconState.selected);
	UpdateAnimated(_iconState.selectionX, info.left, animations);
	UpdateAnimated(_iconState.selectionWidth, info.width, animations);
	const auto iconsXFinal = std::clamp(
		info.left + (info.width - width() + _iconsLeft + _iconsRight) / 2,
		0,
		_iconState.max);
	if (animations == ValidateIconAnimations::None) {
		_iconState.x = anim::value(iconsXFinal, iconsXFinal);
		_iconState.animation.stop();
	} else {
		_iconState.x.start(iconsXFinal);
		_iconState.animationStart = crl::now();
		_iconState.animation.start();
	}
	updateSelected();
	update();
}

void StickersListFooter::setSelectedSubicon(
		int newSelected,
		ValidateIconAnimations animations) {
	if (_subiconState.selected == newSelected) {
		return;
	}
	_subiconState.selected = newSelected;
	const auto info = subiconInfo(_subiconState.selected);
	const auto relativeLeft = info.left;
	const auto subiconsWidthForCentering = 2 * relativeLeft + info.width;
	const auto subiconsXFinal = std::clamp(
		(subiconsWidthForCentering - _subiconsWidth) / 2,
		0,
		_subiconState.max);
	if (animations == ValidateIconAnimations::None) {
		_subiconState.x = anim::value(subiconsXFinal, subiconsXFinal);
		_subiconState.animation.stop();
	} else {
		_subiconState.x.start(subiconsXFinal);
		_subiconState.animationStart = crl::now();
		_subiconState.animation.start();
	}
	updateSelected();
	update();
}

void StickersListFooter::processHideFinished() {
	_categoryAnimation.stop();
	refreshCategoryGeometry();
	_selected = _pressed = SpecialOver::None;
	_iconState.animation.stop();
	_iconState.animationStart = 0;
	_iconState.x.finish();
	_iconState.selectionX.finish();
	_iconState.selectionWidth.finish();
	_subiconState.animation.stop();
	_subiconState.animationStart = 0;
	_subiconState.x.finish();
}

void StickersListFooter::leaveToChildEvent(QEvent *e, QWidget *child) {
	_iconsMousePos = QCursor::pos();
	updateSelected();
}

void StickersListFooter::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);

	_repaintScheduled = false;
	paint(p, {});
}

void StickersListFooter::paint(
		Painter &p,
		const ExpandingContext &context) const {
	if (_icons.empty()) {
		return;
	}

	if (_features.stickersSettings) {
		paintStickerSettingsIcon(p);
	}

	auto clip = QRect(
		_iconsLeft,
		_iconsTop,
		width() - _iconsLeft - _iconsRight,
		st().footer);
	if (rtl()) {
		clip.moveLeft(width() - _iconsLeft - clip.width());
	}
	if (context.expanding) {
		const auto both = clip.intersected(
			context.clip.marginsRemoved(
				{ 0/*context.radius*/, 0, context.radius, 0 }));
		if (both.isEmpty()) {
			return;
		}
		p.setClipRect(both);
	} else {
		p.setClipRect(clip);
	}
	paintSelectionBg(p, context);

	const auto iconCacheSize = QSize(_singleWidth, st().footer);
	const auto full = iconCacheSize * style::DevicePixelRatio();
	if (_setIconCache.size() != full) {
		_setIconCache = QImage(full, QImage::Format_ARGB32_Premultiplied);
		_setIconCache.setDevicePixelRatio(style::DevicePixelRatio());
	}

	const auto now = crl::now();
	const auto paused = _paused();
	p.setPen(st::windowFg);
	enumerateVisibleIcons([&](const IconInfo &info) {
		paintSetIcon(p, context, info, now, paused);
	});
	paintLeftRightFading(p, context);
}

void StickersListFooter::paintSelectionBg(
		QPainter &p,
		const ExpandingContext &context) const {
	if (_categorySection && !_subiconsExpanded) {
		return;
	}
	const auto selectionX
		= int(base::SafeRound(_iconState.selectionX.current()));
	auto selxrel = _iconsLeft + selectionX;
	auto selx = selxrel - int(base::SafeRound(_iconState.x.current()));
	const auto selw
		= int(base::SafeRound(_iconState.selectionWidth.current()));
	if (rtl()) {
		selx = width() - selx - selw;
	}
	const auto sely = _iconsTop;
	const auto area = st().iconArea;
	auto rect = QRect(
		QPoint(selx, sely) + _areaPosition,
		QSize(selw - 2 * _areaPosition.x(), area));
	if (context.expanding) {
		const auto recthalf = rect.height() / 2;
		const auto myhalf = height() / 2;
		const auto sub = anim::interpolate(recthalf, 0, context.progress);
		const auto shift = anim::interpolate(myhalf, 0, context.progress);
		rect = rect.marginsRemoved(
			{ sub, sub, sub, sub }
		).translated(0, shift);
	}
	const auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(CategorySelectionColor(st()));
	p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
}

void StickersListFooter::paintLeftRightFading(
		QPainter &p,
		const ExpandingContext &context) const {
	if (ExtrasFeatures::WindowMaterial::isActive(this)) {
		return;
	}
	p.save();
	if (rtl()) {
		p.translate(width(), 0);
		p.scale(-1., 1.);
	}
	const auto o_left_normal = std::clamp(
		_iconState.x.current() / st().fadeLeft.width(),
		0.,
		1.);
	const auto o_left = context.expanding
		? (1. - context.progress * (1. - o_left_normal))
		: o_left_normal;
	const auto radiusSkip = context.expanding
		? std::max(context.radius - st::emojiPanRadius, 0)
		: 0;
	if (o_left > 0) {
		p.setOpacity(o_left);
		const auto left = std::max(_iconsLeft, radiusSkip);
		const auto top = _iconsTop;
		if (left >= st::emojiPanRadius) {
			st().fadeLeft.fill(
				p,
				QRect(left, top, st().fadeLeft.width(), st().footer));
		} else {
			validateFadeLeft(left + st().fadeLeft.width());
			p.drawImage(0, _iconsTop, _fadeLeftCache);
		}
		p.setOpacity(1.);
	}
	const auto o_right_normal = std::clamp(
		(_iconState.max - _iconState.x.current()) / st().fadeRight.width(),
		0.,
		1.);
	const auto o_right = context.expanding
		? (1. - context.progress * (1. - o_right_normal))
		: o_right_normal;
	if (o_right > 0) {
		p.setOpacity(o_right);
		const auto right = std::max(_iconsRight, radiusSkip);
		const auto rightWidth = right + st().fadeRight.width();
		if (right >= st::emojiPanRadius) {
			st().fadeRight.fill(
				p,
				QRect(
					width() - rightWidth,
					_iconsTop,
					st().fadeRight.width(),
					st().footer));
		} else {
			validateFadeRight(rightWidth);
			p.drawImage(width() - rightWidth, _iconsTop, _fadeRightCache);
		}
		p.setOpacity(1.);
	}
	p.restore();
}

void StickersListFooter::validateFadeLeft(int leftWidth) const {
	validateFadeMask();

	const auto ratio = devicePixelRatioF();
	const auto &color = st().categoriesBg->c;
	dpr::Validate(_fadeLeftCache, ratio, { leftWidth, st().footer }, [&](
			QPainter &p,
			QSize size) {
		_fadeLeftColor = color;
		const auto frame = dpr::IconFrame(st().fadeLeft, color, ratio);
		p.drawImage(
			QRect(
				size.width() - frame.width(),
				0,
				frame.width(),
				size.height()),
			frame);
		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		p.drawImage(0, 0, _fadeMask);
	}, (_fadeLeftColor != color), Qt::transparent);
}

void StickersListFooter::validateFadeRight(int rightWidth) const {
	validateFadeMask();

	const auto ratio = devicePixelRatioF();
	const auto &color = st().categoriesBg->c;
	dpr::Validate(_fadeRightCache, ratio, { rightWidth, st().footer }, [&](
			QPainter &p,
			QSize size) {
		_fadeRightColor = color;
		const auto frame = dpr::IconFrame(st().fadeRight, color, ratio);
		p.drawImage(QRect(0, 0, frame.width(), size.height()), frame);
		p.setCompositionMode(QPainter::CompositionMode_DestinationIn);
		p.drawImage(size.width() - _fadeMask.width(), 0, _fadeMask);
	}, (_fadeRightColor != color), Qt::transparent);
}

void StickersListFooter::validateFadeMask() const {
	const auto ratio = devicePixelRatioF();
	const auto width = st().fadeLeft.width()
		+ st().fadeRight.width()
		+ 2 * st::emojiPanRadius;
	dpr::Validate(_fadeMask, ratio, { width, st().footer }, [&](
			QPainter &p,
			QSize size) {
		const auto radius = st::emojiPanRadius * ratio;
		p.setBrush(Qt::white);
		p.setPen(Qt::NoPen);
		auto hq = PainterHighQualityEnabler(p);
		p.drawRoundedRect(QRect(QPoint(), size), radius, radius);
	}, false, Qt::transparent, false);
}

void StickersListFooter::resizeEvent(QResizeEvent *e) {
	refreshIconsGeometry(_activeByScrollId, ValidateIconAnimations::None);
}

rpl::producer<uint64> StickersListFooter::setChosen() const {
	return _setChosen.events();
}

rpl::producer<> StickersListFooter::openSettingsRequests() const {
	return _openSettingsRequests.events();
}

void StickersListFooter::mousePressEvent(QMouseEvent *e) {
	if (e->button() != Qt::LeftButton) {
		return;
	}
	_iconsMousePos = e->globalPos();
	updateSelected();

	if (_selected == SpecialOver::Settings) {
		_openSettingsRequests.fire({});
	} else {
		_pressed = _selected;
		_iconsMouseDown = _iconsMousePos;
		const auto iconX = int(base::SafeRound(_iconState.x.current()));
		const auto subiconX = int(base::SafeRound(_subiconState.x.current()));
		_iconState.draggingStartX = iconX;
		_subiconState.draggingStartX = subiconX;
	}
}

void StickersListFooter::mouseMoveEvent(QMouseEvent *e) {
	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	updateSelected();

	if (!_iconState.dragging
		&& !_icons.empty()
		&& v::is<IconId>(_pressed)) {
		if ((_iconsMousePos - _iconsMouseDown).manhattanLength() >= QApplication::startDragDistance()) {
			const auto &icon = _icons[v::get<IconId>(_pressed).index];
			(icon.setId == AllEmojiSectionSetId()
				? _subiconState
				: _iconState).dragging = true;
		}
	}
	checkDragging(_iconState);
	checkDragging(_subiconState);
}

void StickersListFooter::checkDragging(ScrollState &state) {
	if (state.dragging) {
		const auto newX = std::clamp(
			(rtl() ? -1 : 1) * (_iconsMouseDown.x() - _iconsMousePos.x())
				+ state.draggingStartX,
			0,
			state.max);
		if (newX != int(base::SafeRound(state.x.current()))) {
			state.x = anim::value(newX, newX);
			state.animationStart = 0;
			state.animation.stop();
			update();
		}
	}
}

void StickersListFooter::mouseReleaseEvent(QMouseEvent *e) {
	if (_icons.empty()) {
		return;
	}

	const auto wasDown = std::exchange(_pressed, SpecialOver::None);

	_iconsMousePos = e ? e->globalPos() : QCursor::pos();
	if (finishDragging()) {
		return;
	}

	updateSelected();
	if (wasDown == _selected) {
		if (const auto icon = std::get_if<IconId>(&_selected)) {
			const auto info = iconInfo(icon->index);
			_iconState.selectionX = anim::value(info.left, info.left);
			_iconState.selectionWidth = anim::value(info.width, info.width);
			const auto setId = _icons[icon->index].setId;
			_setChosen.fire_copy((setId == AllEmojiSectionSetId())
				? EmojiSectionSetId(
					EmojiSection(int(EmojiSection::People) + icon->subindex))
				: setId);
		}
	}
}

bool StickersListFooter::finishDragging() {
	const auto icon = finishDragging(_iconState);
	const auto subicon = finishDragging(_subiconState);
	return icon || subicon;
}

bool StickersListFooter::finishDragging(ScrollState &state) {
	if (!state.dragging) {
		return false;
	}
	const auto newX = std::clamp(
		state.draggingStartX
			+ (rtl() ? -1 : 1) * (_iconsMouseDown.x() - _iconsMousePos.x()),
		0,
		state.max);
	if (newX != int(base::SafeRound(state.x.current()))) {
		state.x = anim::value(newX, newX);
		state.animationStart = 0;
		state.animation.stop();
		update();
	}
	state.dragging = false;
	updateSelected();
	return true;
}

bool StickersListFooter::eventHook(QEvent *e) {
	if (e->type() == QEvent::TouchBegin) {
	} else if (e->type() == QEvent::Wheel) {
		if (!_icons.empty()
			&& v::is<IconId>(_selected)
			&& (_pressed == SpecialOver::None)) {
			scrollByWheelEvent(static_cast<QWheelEvent*>(e));
		}
	}
	return InnerFooter::eventHook(e);
}

void StickersListFooter::scrollByWheelEvent(
		not_null<QWheelEvent*> e) {
	auto horizontal = (e->angleDelta().x() != 0);
	auto vertical = (e->angleDelta().y() != 0);
	if (!horizontal && !vertical) {
		return;
	}
	auto delta = horizontal
		? ((rtl() ? -1 : 1) * (e->pixelDelta().x()
			? e->pixelDelta().x()
			: e->angleDelta().x()))
		: (e->pixelDelta().y()
			? e->pixelDelta().y()
			: e->angleDelta().y());
	const auto use = [&](ScrollState &state) {
		const auto now = int(base::SafeRound(state.x.current()));
		const auto used = now - delta;
		const auto next = std::clamp(used, 0, state.max);
		delta = next - used;
		if (next != now) {
			state.x = anim::value(next, next);
			state.animationStart = 0;
			state.animation.stop();
			updateSelected();
			update();
		}
	};
	const auto index = v::get<IconId>(_selected).index;
	if (_subiconsExpanded
		&& _icons[index].setId == AllEmojiSectionSetId()) {
		use(_subiconState);
	} else {
		use(_iconState);
	}
}

void StickersListFooter::clipCallback(
		Media::Clip::Notification notification,
		uint64 setId) {
	using namespace Media::Clip;
	switch (notification) {
	case Notification::Reinit: {
		enumerateIcons([&](const IconInfo &info) {
			auto &icon = _icons[info.index];
			if (icon.setId != setId || !icon.webm) {
				return true;
			} else if (icon.webm->state() == State::Error) {
				icon.webm.setBad();
			} else if (!info.visible) {
				icon.webm = nullptr;
			} else if (icon.webm->ready() && !icon.webm->started()) {
				icon.webm->start({
					.frame = { icon.pixw, icon.pixh },
					.keepAlpha = true,
				});
			}
			updateSetIconAt(info.adjustedLeft);
			return true;
		});
	} break;

	case Notification::Repaint:
		updateSetIcon(setId);
		break;
	}
}

void StickersListFooter::updateSelected() {
	if (_pressed != SpecialOver::None) {
		return;
	}

	auto p = mapFromGlobal(_iconsMousePos);
	auto x = p.x(), y = p.y();
	if (rtl()) x = width() - 1 - x;
	const auto settingsLeft = _iconsLeft - _singleWidth;
	auto newOver = OverState(SpecialOver::None);
	if (_features.stickersSettings
		&& x >= settingsLeft
		&& x < settingsLeft + _singleWidth
		&& y >= _iconsTop
		&& y < _iconsTop + st().footer) {
		if (!_icons.empty()) {
			newOver = SpecialOver::Settings;
		}
	} else if (!_icons.empty()) {
		if (y >= _iconsTop
			&& y < _iconsTop + st().footer
			&& x >= _iconsLeft
			&& x < width() - _iconsRight) {
			enumerateIcons([&](const IconInfo &info) {
				if (x >= info.adjustedLeft
					&& x < info.adjustedLeft + info.width) {
					newOver = IconId{ .index = info.index };
					if (_icons[info.index].setId == AllEmojiSectionSetId()) {
						const auto subx = (x - info.adjustedLeft);
						enumerateSubicons([&](const IconInfo &info) {
							if (subx >= info.adjustedLeft
								&& subx < info.adjustedLeft + info.width) {
								v::get<IconId>(newOver).subindex = info.index;
								return false;
							}
							return true;
						});
					}
					return false;
				}
				return true;
			});
		}
	}
	if (newOver != _selected) {
		if (newOver == SpecialOver::None) {
			setCursor(style::cur_default);
		} else if (_selected == SpecialOver::None) {
			setCursor(style::cur_pointer);
		}
		_selected = newOver;
	}
}

auto StickersListFooter::getLottieRenderer()
-> std::shared_ptr<Lottie::FrameRenderer> {
	if (auto result = _lottieRenderer.lock()) {
		return result;
	}
	auto result = Lottie::MakeFrameRenderer();
	_lottieRenderer = result;
	return result;
}

void StickersListFooter::refreshIcons(
		std::vector<StickerIcon> icons,
		uint64 activeSetId,
		Fn<std::shared_ptr<Lottie::FrameRenderer>()> renderer,
		ValidateIconAnimations animations) {
	_renderer = renderer
		? std::move(renderer)
		: [=] { return getLottieRenderer(); };

	auto indices = base::flat_map<uint64, int>();
	indices.reserve(_icons.size());
	auto index = 0;
	for (const auto &entry : _icons) {
		indices.emplace(entry.setId, index++);
	}

	for (auto &now : icons) {
		if (const auto i = indices.find(now.setId); i != end(indices)) {
			auto &was = _icons[i->second];
			if (now.sticker == was.sticker) {
				now.webm = std::move(was.webm);
				now.lottie = std::move(was.lottie);
				now.custom = std::move(was.custom);
				now.lifetime = std::move(was.lifetime);
				now.savedFrame = std::move(was.savedFrame);
			}
		}
	}

	_icons = std::move(icons);
	refreshIconsGeometry(activeSetId, animations);
}

void StickersListFooter::refreshScrollableDimensions() {
	const auto &last = iconInfo(_icons.size() - 1);
	_iconState.max = std::max(
		last.left + last.width + _iconsLeft + _iconsRight - width(),
		0);
	if (_iconState.x.current() > _iconState.max) {
		_iconState.x = anim::value(_iconState.max, _iconState.max);
	}
}

void StickersListFooter::refreshIconsGeometry(
		uint64 activeSetId,
		ValidateIconAnimations animations) {
	_selected = _pressed = SpecialOver::None;
	_iconState.x.finish();
	_iconState.selectionX.finish();
	_iconState.selectionWidth.finish();
	_iconState.animationStart = 0;
	_iconState.animation.stop();
	_subiconsWidthAnimation.stop();
	_categoryAnimation.stop();
	if (!refreshCategoryLabel(activeSetId, ValidateIconAnimations::None)) {
		refreshCategoryGeometry();
	}
	_iconState.selected = _subiconState.selected = -2;
	validateSelectedIcon(activeSetId, animations);
	update();
}

bool StickersListFooter::refreshCategoryLabel(
		uint64 activeSetId,
		ValidateIconAnimations animations) {
	const auto section = SetIdEmojiSection(activeSetId);
	const auto label = section
		? st::emojiCategoryLabelFont->elided(
			CategoryLabel(*section), st::emojiCategoryLabelMaxWidth)
		: QString();
	if (_categorySection == section && _categoryLabel == label) {
		return false;
	}
	for (auto i = 0; i != kVisibleIconsCount; ++i) {
		_categoryWidthsFrom[i] = categoryItemWidth(EmojiSection(i));
		_categoryWeightsFrom[i] = categoryActiveWeight(EmojiSection(i));
		_categoryLabels[i] = st::emojiCategoryLabelFont->elided(
			CategoryLabel(EmojiSection(i)), st::emojiCategoryLabelMaxWidth);
	}
	_categorySection = section;
	_categoryLabel = label;
	_categoryWidth = section
		? st().icons.people.width()
			+ st::emojiCategoryLabelGap
			+ st::emojiCategoryLabelFont->width(label)
			+ 2 * st::emojiCategoryLabelPadding
		: 0;
	_categoryAnimation.stop();
	refreshCategoryGeometry();
	if (animations != ValidateIconAnimations::None && isVisible()) {
		// 连续切换从当前宽度与透明度接续，不重置到上一项的终点。
		_categoryAnimation.start([=] { updateCategoryAnimation(); },
			0., 1., 200, anim::easeOutCirc);
	}
	return true;
}

int StickersListFooter::categoryItemWidth(EmojiSection section) const {
	const auto target = (_categorySection == section)
		? std::max(_singleWidth, _categoryWidth)
		: _singleWidth;
	const auto from = _categoryWidthsFrom[int(section)];
	return int(base::SafeRound(from + (target - from) * _categoryAnimation.value(1.)));
}

float64 StickersListFooter::categoryActiveWeight(EmojiSection section) const {
	const auto from = _categoryWeightsFrom[int(section)];
	const auto target = (_categorySection == section) ? 1. : 0.;
	return from + (target - from) * _categoryAnimation.value(1.);
}

void StickersListFooter::updateCategoryAnimation() {
	auto contentWidth = 0;
	for (auto i = int(EmojiSection::People); i <= int(EmojiSection::Symbols); ++i) {
		contentWidth += categoryItemWidth(EmojiSection(i));
	}
	_subiconState.max = std::max(0, contentWidth - _subiconsWidth);
	if (_subiconsExpanded && !_subiconState.dragging) {
		const auto info = subiconInfo(_subiconState.selected);
		const auto target = std::clamp(
			info.left + (info.width - _subiconsWidth) / 2, 0, _subiconState.max);
		if (_subiconState.animation.animating()) {
			UpdateAnimated(_subiconState.x, target);
		} else {
			_subiconState.x = anim::value(target, target);
		}
	}
	updateEmojiWidthCallback();
}

void StickersListFooter::refreshCategoryGeometry() {
	if (_icons.size() > 1
		&& _icons[1].setId == EmojiSectionSetId(EmojiSection::People)) {
		const auto labelled = _categorySection.has_value();
		const auto available = width() - _iconsLeft - _iconsRight;
		const auto count = int(_icons.size());
		_singleWidth = std::max(st().iconWidth,
			std::min(available / count,
				(available - _categoryWidth) / (count - (labelled ? 1 : 0))));
	} else {
		_singleWidth = st().iconWidth;
	}
	_areaPosition = QPoint(
		(_singleWidth - st().iconArea) / 2,
		(st().footer - st().iconArea) / 2);
	refreshScrollableDimensions();
	refreshSubiconsGeometry();
}

void StickersListFooter::refreshSubiconsGeometry() {
	using Section = Ui::Emoji::Section;
	const auto current = _subiconState.x.current();
	_subiconState.x = anim::value(current, current);
	_subiconState.animationStart = 0;
	_subiconState.animation.stop();
	const auto half = _singleWidth / 2;
	const auto count = int(Section::Symbols) - int(Section::Recent);
	const auto labelled = _categorySection
		&& _categorySection != Section::Recent;
	const auto extra = labelled
		? std::max(_categoryWidth - _singleWidth, 0)
		: 0;
	const auto widthMax = count * _singleWidth;
	const auto widthMin = 5 * _singleWidth + half;
	const auto collapsedWidth = int(_icons.size()) * _singleWidth;
	_subiconsWidth = std::clamp(
		width() + _singleWidth - collapsedWidth,
		widthMin,
		widthMax);
	if (_subiconsWidth < widthMax) {
		_subiconsWidth = half
			+ (((_subiconsWidth - half) / _singleWidth) * _singleWidth);
	}
	_subiconState.max = std::max(
		widthMax + extra - _subiconsWidth,
		0);
	if (_subiconState.x.current() > _subiconState.max) {
		_subiconState.x = anim::value(_subiconState.max, _subiconState.max);
	}
	refreshScrollableDimensions();
}

void StickersListFooter::paintStickerSettingsIcon(QPainter &p) const {
	const auto settingsLeft = _iconsLeft - _singleWidth;
	st().icons.settings.paint(
		p,
		(settingsLeft + (_singleWidth - st().icons.settings.width()) / 2),
		_iconsTop + st::emojiCategoryIconTop,
		width());
}

void StickersListFooter::customEmojiRepaint() {
	if (!_repaintScheduled) {
		_repaintScheduled = true;
		update();
	}
}

void StickersListFooter::validateIconLottieAnimation(
		const StickerIcon &icon) {
	icon.ensureMediaCreated();
	if (icon.lottie
		|| !icon.sticker
		|| !HasLottieThumbnail(
			icon.set ? icon.set->thumbnailType() : StickerType(),
			icon.thumbnailMedia.get(),
			icon.stickerMedia.get())) {
		return;
	}
	auto player = LottieThumbnail(
		icon.thumbnailMedia.get(),
		icon.stickerMedia.get(),
		StickerLottieSize::StickersFooter,
		QSize(icon.pixw, icon.pixh) * style::DevicePixelRatio(),
		_renderer());
	if (!player) {
		return;
	}
	icon.lottie = std::move(player);

	const auto id = icon.setId;
	icon.lottie->updates(
	) | rpl::on_next([=] {
		updateSetIcon(id);
	}, icon.lifetime);
}

void StickersListFooter::validateIconWebmAnimation(
		const StickerIcon &icon) {
	icon.ensureMediaCreated();
	if (icon.webm
		|| icon.webm.isBad()
		|| !icon.sticker
		|| !HasWebmThumbnail(
			icon.set ? icon.set->thumbnailType() : StickerType(),
			icon.thumbnailMedia.get(),
			icon.stickerMedia.get())) {
		return;
	}
	const auto id = icon.setId;
	auto callback = [=](Media::Clip::Notification notification) {
		clipCallback(notification, id);
	};
	icon.webm = WebmThumbnail(
		icon.thumbnailMedia.get(),
		icon.stickerMedia.get(),
		std::move(callback));
}

void StickersListFooter::validateIconAnimation(
		const StickerIcon &icon) {
	const auto emoji = icon.sticker;
	if (emoji && emoji->sticker()->setType == Data::StickersType::Emoji) {
		if (!icon.custom) {
			const auto tag = Data::CustomEmojiManager::SizeTag::SetIcon;
			auto &manager = emoji->owner().customEmojiManager();
			icon.custom = manager.create(
				emoji->id,
				[=] { customEmojiRepaint(); },
				tag);
		}
		return;
	}
	validateIconWebmAnimation(icon);
	validateIconLottieAnimation(icon);
}

void StickersListFooter::updateSetIcon(uint64 setId) {
	enumerateVisibleIcons([&](const IconInfo &info) {
		if (_icons[info.index].setId != setId) {
			return;
		}
		updateSetIconAt(info.adjustedLeft);
	});
}

void StickersListFooter::updateSetIconAt(int left) {
	update(rtl() ? width() - left - _singleWidth : left,
		_iconsTop, _singleWidth, st().footer);
}

void StickersListFooter::paintSetIcon(
		Painter &p,
		const ExpandingContext &context,
		const IconInfo &info,
		crl::time now,
		bool paused) const {
	const auto &icon = _icons[info.index];
	const auto left = rtl()
		? width() - info.adjustedLeft - info.width
		: info.adjustedLeft;
	const auto expandingShift = context.expanding
		? QPoint(
			0,
			anim::interpolate(height() / 2, 0, context.progress))
		: QPoint();
	if (icon.sticker) {
		icon.ensureMediaCreated();
		const_cast<StickersListFooter*>(this)->validateIconAnimation(icon);
	}
	if (context.expanding) {
		if (icon.custom) {
			p.translate(expandingShift);
		} else {
			p.save();
			const auto center = QPoint(
				left + info.width / 2,
				_iconsTop + st().footer / 2);
			p.translate(expandingShift + center);
			p.scale(context.progress, context.progress);
			p.translate(-center);
		}
	}
	if (icon.sticker || icon.premiumRequired) {
		prepareSetIcon(context, info, now, paused);
		p.drawImage(left, _iconsTop, _setIconCache);
	} else {
		p.translate(left, _iconsTop);
		paintSetIconToCache(p, context, info, now, paused);
		p.translate(-left, -_iconsTop);
	}
	if (context.expanding) {
		if (icon.custom) {
			p.translate(-expandingShift);
		} else {
			p.restore();
		}
	}
}

void StickersListFooter::prepareSetIcon(
		const ExpandingContext &context,
		const IconInfo &info,
		crl::time now,
		bool paused) const {
	_setIconCache.fill(Qt::transparent);
	auto p = Painter(&_setIconCache);
	paintSetIconToCache(p, context, info, now, paused);
	const auto &icon = _icons[info.index];
	auto hq = PainterHighQualityEnabler(p);
	const auto area = st().iconArea;
	auto rect = QRect(_areaPosition, QSize(area, area));
	if (icon.sticker) {
		// Rounding the corners.
		p.setCompositionMode(QPainter::CompositionMode_Source);
		p.setBrush(Qt::NoBrush);
		auto pen = QPen(Qt::transparent);
		pen.setWidth(style::ConvertScaleExact(4.));
		p.setPen(pen);
		p.drawRoundedRect(rect, st::emojiPanRadius, st::emojiPanRadius);
	}
	if (!icon.premiumRequired) {
		return;
	}
	const auto &lock = st::emojiPremiumLock;
	const auto size = std::max(lock.width(), lock.height())
		+ 2 * st::lineWidth;
	const auto badge = QRect(
		rect.x() + (rtl() ? 0 : rect.width() - size),
		rect.y() + rect.height() - size,
		size,
		size);
	// The lock is cut through the icon, so it shows the panel behind.
	p.setCompositionMode(QPainter::CompositionMode_SourceOver);
	p.setPen(Qt::NoPen);
	p.setBrush(st().headerFg);
	p.drawEllipse(badge);
	p.setCompositionMode(QPainter::CompositionMode_DestinationOut);
	lock.paintInCenter(p, badge);
}

void StickersListFooter::paintSetIconToCache(
		Painter &p,
		const ExpandingContext &context,
		const IconInfo &info,
		crl::time now,
		bool paused) const {
	const auto &icon = _icons[info.index];
	if (icon.sticker) {
		const auto origin = icon.sticker->stickerSetOrigin();
		const auto thumb = icon.thumbnailMedia
			? icon.thumbnailMedia->image()
			: icon.stickerMedia
			? icon.stickerMedia->thumbnail()
			: nullptr;
		const auto x = (_singleWidth - icon.pixw) / 2;
		const auto y = (st().footer - icon.pixh) / 2;
		if (icon.custom) {
			icon.custom->paint(p, Ui::Text::CustomEmoji::Context{
				.textColor = (_customTextColor
					? _customTextColor()
					: st().textFg->c),
				.size = QSize(icon.pixw, icon.pixh),
				.now = now,
				.scale = context.progress,
				.position = { x, y },
				.paused = paused,
				.scaled = context.expanding,
				.internal = { .forceFirstFrame = _forceFirstFrame },
			});
		} else if (icon.lottie && icon.lottie->ready()) {
			const auto frame = icon.lottie->frame();
			const auto size = frame.size() / style::DevicePixelRatio();
			if (icon.savedFrame.isNull()) {
				icon.savedFrame = frame;
				icon.savedFrame.setDevicePixelRatio(
					style::DevicePixelRatio());
			}
			p.drawImage(
				QRect(
					(_singleWidth - size.width()) / 2,
					(st().footer - size.height()) / 2,
					size.width(),
					size.height()),
				frame);
			if (!paused) {
				icon.lottie->markFrameShown();
			}
		} else if (icon.webm && icon.webm->started()) {
			const auto frame = icon.webm->current(
				{ .frame = { icon.pixw, icon.pixh }, .keepAlpha = true },
				paused ? 0 : now);
			if (icon.savedFrame.isNull()) {
				icon.savedFrame = frame;
				icon.savedFrame.setDevicePixelRatio(
					style::DevicePixelRatio());
			}
			p.drawImage(x, y, frame);
		} else if (!icon.savedFrame.isNull()) {
			p.drawImage(x, y, icon.savedFrame);
		} else if (thumb) {
			const auto pixmap = (!icon.lottie && thumb)
				? thumb->pix(icon.pixw, icon.pixh)
				: QPixmap();
			if (pixmap.isNull()) {
				return;
			} else if (icon.savedFrame.isNull()) {
				icon.savedFrame = pixmap.toImage();
			}
			p.drawPixmapLeft(x, y, info.width, pixmap);
		}
	} else if (icon.megagroup) {
		const auto size = st::stickerGroupCategorySize;
		icon.megagroup->paintUserpicLeft(
			p,
			icon.megagroupUserpic,
			(_singleWidth - size) / 2,
			(st().footer - size) / 2,
			info.width,
			st::stickerGroupCategorySize);
	} else {
		using Section = Ui::Emoji::Section;
		const auto paintOne = [&](int left, const style::icon *icon) {
			left += (info.width - icon->width()) / 2;
			const auto top = (st().footer - icon->height()) / 2;
			if (_customTextColor) {
				icon->paint(p, left, top, info.width, _customTextColor());
			} else {
				icon->paint(p, left, top, info.width);
			}
		};
		if (_icons[info.index].setId == AllEmojiSectionSetId()
			&& info.width > _singleWidth) {
			const auto skip = st::emojiIconSelectSkip;
			p.save();
			p.setClipRect(
				skip,
				0,
				info.width - 2 * skip,
				st().footer,
				Qt::IntersectClip);
			enumerateSubicons([&](const IconInfo &subinfo) {
				if (subinfo.visible) {
					paintEmojiCategory(p,
						subinfo.adjustedLeft,
						subinfo.width,
						info.width,
						Section(int(Section::People) + subinfo.index));
				}
				return true;
			});
			p.restore();
		} else if (const auto section = SetIdEmojiSection(icon.setId)) {
			paintEmojiCategory(p, 0, info.width, info.width, *section);
		} else {
			paintOne(0, [&] {
				const auto selected = (info.index == _iconState.selected);
				if (icon.setId == AllEmojiSectionSetId()) {
					return &st().icons.people;
				} else if (icon.setId == Data::Stickers::CollectibleSetId) {
					return &st().icons.collectibles;
				}
				return selected ? &st().icons.recentActive : &st().icons.recent;
			}());
		}
	}
}

void StickersListFooter::paintEmojiCategory(
		Painter &p,
		int left,
		int itemWidth,
		int outerWidth,
		EmojiSection section) const {
	const auto icons = std::array{
		&st().icons.recent, &st().icons.people,
		&st().icons.nature, &st().icons.food,
		&st().icons.activity, &st().icons.travel,
		&st().icons.objects, &st().icons.symbols,
	};
	const auto icon = icons[int(section)];
	const auto active = categoryActiveWeight(section);
	const auto &label = _categoryLabels[int(section)];
	const auto labelWidth = st::emojiCategoryLabelFont->width(label);
	const auto extra = std::min(
		int(base::SafeRound((st::emojiCategoryLabelGap + labelWidth) * active)),
		std::max(0, itemWidth - icon->width() - 2 * st::emojiCategoryLabelPadding));
	const auto contentWidth = icon->width() + extra;
	const auto contentLeft = left + (itemWidth - contentWidth) / 2;
	const auto color = _customTextColor
		? _customTextColor()
		: st::windowActiveTextFg->c;
	p.save();
	if (active > 0.) {
		const auto inset = style::ConvertScale(2);
		const auto rect = QRect(
			(rtl() ? outerWidth - left - itemWidth : left) + inset,
			(st().footer - st().iconArea) / 2,
			itemWidth - 2 * inset,
			st().iconArea);
		auto fill = CategorySelectionColor(st());
		fill.setAlphaF(fill.alphaF() * active);
		const auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(fill);
		p.drawRoundedRect(rect, rect.height() / 2., rect.height() / 2.);
		p.save();
		const auto labelLeft = contentLeft + icon->width() + st::emojiCategoryLabelGap;
		const auto visibleWidth = std::max(0, extra - st::emojiCategoryLabelGap);
		p.setClipRect(style::rtlrect(labelLeft, 0, visibleWidth, st().footer, outerWidth),
			Qt::IntersectClip);
		p.setOpacity(p.opacity() * active);
		p.setFont(st::emojiCategoryLabelFont);
		p.setPen(color);
		p.drawTextLeft(labelLeft,
			(st().footer - st::emojiCategoryLabelFont->height) / 2,
			outerWidth, label, labelWidth);
		p.restore();
	}
	const auto top = (st().footer - icon->height()) / 2;
	icon->paint(p, contentLeft, top, outerWidth, _customTextColor
		? color : anim::color(st::emojiIconFg->c, color, active));
	p.restore();
}

LocalStickersManager::LocalStickersManager(not_null<Main::Session*> session)
: _session(session)
, _api(&session->mtp()) {
}

void LocalStickersManager::install(uint64 setId) {
	const auto &sets = _session->data().stickers().sets();
	const auto it = sets.find(setId);
	if (it == sets.cend()) {
		return;
	}
	const auto set = it->second.get();
	const auto input = set->mtpInput();
	if (!(set->flags & Data::StickersSetFlag::NotLoaded)
		&& !set->stickers.empty()) {
		sendInstallRequest(setId, input);
		return;
	}
	_api.request(MTPmessages_GetStickerSet(
		input,
		MTP_int(0) // hash
	)).done([=](const MTPmessages_StickerSet &result) {
		result.match([&](const MTPDmessages_stickerSet &data) {
			_session->data().stickers().feedSetFull(data);
		}, [](const MTPDmessages_stickerSetNotModified &) {
			LOG(("API Error: Unexpected messages.stickerSetNotModified."));
		});
		sendInstallRequest(setId, input);
	}).send();
}

bool LocalStickersManager::isInstalledLocally(uint64 setId) const {
	return _installedLocallySets.contains(setId);
}

void LocalStickersManager::sendInstallRequest(
		uint64 setId,
		const MTPInputStickerSet &input) {
	_api.request(MTPmessages_InstallStickerSet(
		input,
		MTP_bool(false)
	)).done([=](const MTPmessages_StickerSetInstallResult &result) {
		if (result.type() == mtpc_messages_stickerSetInstallResultArchive) {
			_session->data().stickers().applyArchivedResult(
				result.c_messages_stickerSetInstallResultArchive());
		}
	}).fail([=] {
		notInstalledLocally(setId);
		_session->data().stickers().undoInstallLocally(setId);
	}).send();

	installedLocally(setId);
	_session->data().stickers().installLocally(setId);
}

void LocalStickersManager::installedLocally(uint64 setId) {
	_installedLocallySets.insert(setId);
}

void LocalStickersManager::notInstalledLocally(uint64 setId) {
	_installedLocallySets.remove(setId);
}

void LocalStickersManager::removeInstalledLocally(uint64 setId) {
	_installedLocallySets.remove(setId);
}

bool LocalStickersManager::clearInstalledLocally() {
	if (_installedLocallySets.empty()) {
		return false;
	}
	_installedLocallySets.clear();
	return true;
}

} // namespace ChatHelpers
