#include "settings/sections/settings_local_storage.h"

#include "settings/settings_card_layout.h"
#include "settings/sections/settings_cache_widgets.h"
#include "settings/sections/settings_cache_chats.h"
#include "base/weak_qptr.h"
#include "base/weak_ptr.h"
#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "storage/storage_account.h"
#include "storage/cache/storage_cache_database.h"
#include "ui/layers/generic_box.h"
#include "ui/rect.h"
#include "ui/text/format_values.h"
#include "ui/text/text_utilities.h"
#include "ui/ui_utility.h"
#include "ui/vertical_list.h"
#include "ui/widgets/continuous_sliders.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_background_preview_box.h"
#include "styles/style_edit_peer_members.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_settings_local_storage.h"
#ifdef _DEBUG
#include "extras/debug/debug_login.h"
#include "settings.h"
#endif

namespace Settings {
namespace {

using Category = Extras::MediaCacheCategory;
using ChatType = Extras::MediaCacheChatType;

constexpr auto kMegabyte = int64(1024 * 1024);
constexpr auto kTotalSizeLimitsCount = 18;
constexpr auto kMediaSizeLimitsCount = 18;
constexpr auto kMinimalSizeLimit = 100 * kMegabyte;
constexpr auto kRetentionDays = std::array{ 1, 2, 7, 30, 0 };
constexpr auto kMediaDisplayOrder = std::array{
	Category::Stickers,
	Category::Images,
	Category::Documents,
	Category::Avatars,
	Category::Other,
};

int64 TotalSizeLimit(int index) {
	return ((index < 8)
		? int64(index + 2) * 100
		: int64(index - 7) * 1024) * kMegabyte;
}

int64 MediaSizeLimit(int index) {
	return ((index < 9)
		? int64(index + 1) * 100
		: int64(index - 8) * 1024) * kMegabyte;
}

QString SizeLimitText(int64 limit) {
	const auto mb = limit / kMegabyte;
	const auto gb = mb / 1024;
	return (gb > 0)
		? (QString::number(gb) + u" GB"_q)
		: (QString::number(mb) + u" MB"_q);
}

int64 totalBytes(const Extras::MediaCacheUsageList &categories) {
	auto result = int64();
	for (const auto &usage : categories) {
		result += usage.bytes;
	}
	return result;
}

QString retentionText(int days) {
	return days
		? tr::lng_days(tr::now, lt_count, days)
		: tr::extras_CacheKeepForever(tr::now);
}

QString chatTypeName(ChatType type) {
	switch (type) {
	case ChatType::Private: return tr::extras_CachePrivateChats(tr::now);
	case ChatType::Groups: return tr::extras_CacheGroups(tr::now);
	case ChatType::Channels: return tr::extras_CacheChannels(tr::now);
	case ChatType::Stories: return tr::extras_CacheStories(tr::now);
	case ChatType::Count: break;
	}
	Unexpected("Invalid cache chat type.");
}

QString chatTypeId(ChatType type) {
	const auto names = std::array{
		u"private"_q, u"groups"_q, u"channels"_q, u"stories"_q,
	};
	return names[int(type)];
}

QString cacheChatName(not_null<Main::Session*> session, PeerId peer) {
	const auto loaded = session->data().peerLoaded(peer);
	return (loaded && !loaded->name().isEmpty())
		? loaded->name()
		: tr::extras_CacheChatUnknown(tr::now, lt_id, QString::number(peer.value));
}

void reportClearResult(
		base::weak_ptr<Window::SessionController> controller,
		Extras::MediaCacheClearResult result) {
	const auto strong = controller.get();
	if (!strong) {
		return;
	}
	if (result.error.type != Storage::Cache::Error::Type::None) {
		strong->showToast(tr::extras_CacheClearFailed(tr::now));
		return;
	}
	strong->showToast(tr::lng_local_storage_cleared(
		tr::now,
		lt_size,
		Ui::FormatSizeText(result.freedBytes)));
}

void showRetentionPicker(
		not_null<Window::SessionController*> controller,
		not_null<Extras::MediaCacheManager*> manager,
		ChatType type) {
	controller->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(chatTypeName(type)));
		for (const auto days : kRetentionDays) {
			const auto row = AddButtonWithLabel(
				box->verticalLayout(),
				rpl::single(retentionText(days)),
				rpl::single(manager->retentionDays(type) == days ? u"✓"_q : QString()),
				st::settingsButtonNoIcon);
			row->setObjectName(u"cache/retention/"_q
				+ chatTypeId(type) + '/' + QString::number(days));
			row->setClickedCallback([=] {
				manager->setRetentionDays(type, days);
				box->closeBox();
			});
		}
		box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	}));
}

} // namespace

LocalStorage::LocalStorage(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller)
, _session(&controller->session())
, _manager(&_session->mediaCache()) {
	const auto &settings = _session->local().cacheSettings();
	const auto &settingsBig = _session->local().cacheBigFileSettings();
	_totalSizeLimit = settings.totalSizeLimit + settingsBig.totalSizeLimit;
	_mediaSizeLimit = settingsBig.totalSizeLimit;
	setupContent();
}

rpl::producer<QString> LocalStorage::title() {
	return tr::lng_local_storage_title();
}

void LocalStorage::showFinished() {
	Section::showFinished();
	controller()->checkHighlightControl(
		u"storage/clear-cache"_q,
		_clearButton,
		{ .rippleShape = true });
	const auto add = st::roundRadiusSmall;
	controller()->checkHighlightControl(
		u"storage/max-cache"_q,
		_totalSlider,
		{ .margin = Margins(-add), .radius = add });
}

void LocalStorage::setupContent() {
	const auto page = Ui::CreateChild<CardPage>(this);
	const auto content = page->content();
	const auto overview = AddCardGroup(content);
	_chart = overview->add(object_ptr<CacheChart>(overview, controller()));
	_chart->setObjectName(u"cache/chart"_q);
	_chart->setPartInfo([=](int index) {
		auto result = TextWithEntities();
		result.append(Ui::Text::Bold(CachePartName(index)));
		result.append('\n').append(Ui::FormatSizeText(_snapshot.categories[index].bytes));
		result.append(u" · "_q).append(QString::number(_chart->partPercent(index)) + '%');
		return result;
	});
	_deviceBar = overview->add(object_ptr<CacheDeviceBar>(overview));
	_deviceBar->setObjectName(u"cache/deviceUsage"_q);
	_status = overview->add(object_ptr<Ui::FlatLabel>(
		overview, tr::extras_CacheScanning(), st::localStorageAbout),
		st::localStorageAboutMargin);
	setupCategories(content);
	AddCardDescription(content, tr::extras_CacheClearAbout());
	setupRetention(content);
	AddCardTitle(content, tr::lng_local_storage_manage_title());
	setupLimits(AddCardGroup(content));
	AddCardDescription(content, tr::extras_CacheLimitsAbout());
	AddCardTitle(content, tr::extras_CacheByChat());
	const auto chats = AddCardGroup(content);
	_chats = chats->add(object_ptr<CacheChats>(chats, _session,
		[=](Extras::MediaCacheChat chat) { showChatCache(std::move(chat)); }));
	AddCardDescription(content, tr::extras_CacheChatListAbout());
	_manager->changes() | rpl::on_next([=] {
		updateClearButton();
		if (!_manager->clearing()) {
			requestSnapshot();
		}
	}, lifetime());
	requestSnapshot();
	Ui::ResizeFitChild(this, page);
}

void LocalStorage::setupCategories(not_null<Ui::VerticalLayout*> container) {
	AddCardTitle(container, tr::extras_CacheMedia());
	const auto media = AddCardGroup(container);
	const auto createRow = [&](not_null<Ui::VerticalLayout*> group, Category category) {
		const auto index = int(category);
		const auto row = group->add(object_ptr<CacheCategoryRow>(
			group, CachePartName(index), index, _selected[index],
			_snapshot.categories[index]));
		_rows[index] = row;
		row->setObjectName(u"cache/category/"_q + CacheCategoryId(index));
		row->checkedChanges() | rpl::on_next([=](bool selected) {
			_selected[index] = selected;
			updateClearButton();
		}, row->lifetime());
	};
	for (const auto category : kMediaDisplayOrder) {
		createRow(media, category);
	}
	AddCardTitle(container, tr::extras_CacheMessages());
	createRow(AddCardGroup(container), Category::Messages);
	AddCardDescription(container, tr::extras_CacheMessagesAbout());
	const auto action = AddCardGroup(container);
	_clearButton = action->add(
		object_ptr<CacheClearButton>(action), st::localStorageClearMargin);
	_clearButton->setObjectName(u"storage/clear-cache"_q);
	_clearButton->setClickedCallback([=] { clearSelected(); });
	updateClearButton();
}

void LocalStorage::setupRetention(not_null<Ui::VerticalLayout*> container) {
	AddCardTitle(container, tr::extras_CacheAutoDeleteMedia());
	const auto group = AddCardGroup(container);
	for (auto index = 0; index != Extras::kMediaCacheChatTypeCount; ++index) {
		const auto type = ChatType(index);
		const auto row = AddButtonWithLabel(
			group,
			rpl::single(chatTypeName(type)),
			_manager->retentionValue(type) | rpl::map(retentionText),
			st::settingsButtonNoIcon);
		row->setObjectName(u"cache/retention/"_q + chatTypeId(type));
		row->setClickedCallback([=] { showRetentionPicker(controller(), _manager, type); });
	}
	AddCardDescription(container, tr::extras_CacheAutoDeleteAbout());
}

void LocalStorage::requestSnapshot() {
#ifdef _DEBUG
	if (_debugChats) {
		return;
	}
#endif
	if (_snapshotPending) {
		_snapshotAgain = true;
		return;
	}
	_snapshotPending = true;
	_manager->snapshot(crl::guard(this, [=](Extras::MediaCacheSnapshot snapshot) {
		_snapshotPending = false;
#ifdef _DEBUG
		if (_debugChats) {
			return;
		}
#endif
		updateSnapshot(std::move(snapshot));
		if (base::take(_snapshotAgain)) {
			requestSnapshot();
		}
	}));
}

#ifdef _DEBUG
void LocalStorage::setDebugChats(int count) {
	Expects(cDebugProfile() && ExtrasDebug::isSimulationSession(_session));
	_debugChats = true;
	auto snapshot = Extras::MediaCacheSnapshot();
	for (auto i = 0; i != count; ++i) {
		auto chat = Extras::MediaCacheChat{ .peer = UserId(8900000000ULL + i) };
		chat.bytes = (count - i) * int64(1024);
		chat.categories[int(Category::Images)] = { chat.bytes, 1 };
		snapshot.categories[int(Category::Images)].bytes += chat.bytes;
		++snapshot.categories[int(Category::Images)].count;
		snapshot.chats.push_back(chat);
	}
	updateSnapshot(std::move(snapshot));
}
#endif

void LocalStorage::updateSnapshot(Extras::MediaCacheSnapshot snapshot) {
	if (snapshot.error.type != Storage::Cache::Error::Type::None) {
		_loaded = false;
		_status->setText(tr::extras_CacheScanFailed(tr::now));
		updateClearButton();
		return;
	}
	_snapshot = std::move(snapshot);
	_loaded = true;
	auto sizes = std::array<int64, kChartPartsCount>();
	for (auto index = 0; index != kChartPartsCount; ++index) {
		sizes[index] = _snapshot.categories[index].bytes;
	}
	_chart->setLoaded();
	_chart->setParts(sizes, totalBytes(_snapshot.categories));
	_deviceBar->setCache(totalBytes(_snapshot.categories));
	for (auto index = 0; index != kChartPartsCount; ++index) {
		_rows[index]->setUsage(_snapshot.categories[index]);
		_rows[index]->setPercent(_chart->partPercent(index));
	}
	_status->setText(QString());
	updateClearButton();
	updateChats();
}

Extras::MediaCacheMask LocalStorage::selectedMask() const {
	auto result = Extras::MediaCacheMask();
	for (auto index = 0; index != Extras::kMediaCacheCategoryCount; ++index) {
		if (_selected[index]) {
			result |= Extras::mediaCacheBit(Category(index));
		}
	}
	return result;
}

int64 LocalStorage::selectedBytes() const {
	auto result = int64();
	for (auto index = 0; index != Extras::kMediaCacheCategoryCount; ++index) {
		if (_selected[index]) {
			result += _snapshot.categories[index].bytes;
		}
	}
	return result;
}

void LocalStorage::updateClearButton() {
	const auto clearing = _manager->clearing();
	const auto bytes = selectedBytes();
	_clearButton->setText(
		clearing
			? tr::lng_local_storage_clearing(tr::now)
			: tr::lng_local_storage_clear_selected(tr::now),
		Ui::FormatSizeText(bytes));
	_clearButton->setDisabled(clearing || !_loaded || !bytes);
	for (const auto row : _rows) {
		row->setDisabled(clearing);
	}
	if (_chats) {
		_chats->setClearing(clearing);
	}
}

void LocalStorage::clearSelected() {
	if (_manager->clearing() || !selectedBytes()) {
		return;
	}
	const auto weakController = base::make_weak(controller());
	const auto done = [=](Extras::MediaCacheClearResult result) {
		reportClearResult(weakController, result);
	};
	if (!_manager->clear(selectedMask(), PeerId(), done)) {
		controller()->showToast(tr::extras_CacheAlreadyClearing(tr::now));
	}
	updateClearButton();
}

void LocalStorage::updateChats() {
	_chats->setChats(_snapshot.chats, totalBytes(_snapshot.unassigned));
}

void LocalStorage::showChatCache(Extras::MediaCacheChat chat) {
	const auto manager = _manager;
	const auto session = _session;
	const auto weakController = base::make_weak(controller());
	controller()->show(Box([=](not_null<Ui::GenericBox*> box) {
		box->setTitle(rpl::single(cacheChatName(session, chat.peer)));
		box->setWidth(st::boxWideWidth);
		box->addRow(object_ptr<Ui::FlatLabel>(
			box, tr::extras_CacheChatClearAbout(), st::localStorageAbout));
		struct State {
			Extras::MediaCacheUsageList categories;
			Extras::MediaCacheMask selected = 0;
			std::array<CacheCategoryRow*, Extras::kMediaCacheCategoryCount> rows = {};
			CacheClearButton *button = nullptr;
		};
		const auto state = box->lifetime().make_state<State>();
		state->categories = chat.categories;
		const auto refresh = [=] {
			auto bytes = int64();
			for (auto index = 0; index != Extras::kMediaCacheCategoryCount; ++index) {
				if (state->selected & Extras::mediaCacheBit(Category(index))) {
					bytes += state->categories[index].bytes;
				}
				if (state->rows[index]) {
					state->rows[index]->setDisabled(manager->clearing());
				}
			}
			state->button->setText(
				manager->clearing()
					? tr::lng_local_storage_clearing(tr::now)
					: tr::extras_CacheClearChat(tr::now),
				Ui::FormatSizeText(bytes));
			state->button->setDisabled(manager->clearing() || !bytes);
		};
		const auto createRow = [&](Category category, bool checked) {
			const auto index = int(category);
			const auto usage = chat.categories[index];
			const auto bit = Extras::mediaCacheBit(category);
			if (!usage.bytes) {
				return;
			}
			if (checked) {
				state->selected |= bit;
			}
			const auto row = box->addRow(object_ptr<CacheCategoryRow>(
				box, CachePartName(index), index, checked, usage), QMargins());
			state->rows[index] = row;
			row->setObjectName(u"cache/chat/category/"_q + CacheCategoryId(index));
			row->checkedChanges() | rpl::on_next([=](bool selected) {
				if (selected) {
					state->selected |= bit;
				} else {
					state->selected &= ~bit;
				}
				refresh();
			}, row->lifetime());
		};
		for (const auto category : kMediaDisplayOrder) {
			createRow(category, true);
		}
		if (chat.categories[int(Category::Messages)].bytes) {
			Ui::AddSubsectionTitle(box->verticalLayout(), tr::extras_CacheMessages());
			createRow(Category::Messages, false);
			box->addRow(object_ptr<Ui::FlatLabel>(
				box, tr::extras_CacheMessagesAbout(), st::localStorageAbout));
		}
		state->button = box->addRow(
			object_ptr<CacheClearButton>(box), st::localStorageClearMargin);
		state->button->setObjectName(u"cache/chat/clear"_q);
		const auto weakBox = base::make_weak(box);
		state->button->setClickedCallback([=] {
			if (!manager->clear(state->selected, chat.peer,
					[=](Extras::MediaCacheClearResult result) {
				if (const auto strong = weakBox.get()) {
					strong->closeBox();
				}
				reportClearResult(weakController, result);
			})) {
				if (const auto strong = weakController.get()) {
					strong->showToast(tr::extras_CacheAlreadyClearing(tr::now));
				}
			}
			refresh();
		});
		manager->changes() | rpl::on_next(refresh, box->lifetime());
		refresh();
		box->addButton(tr::lng_close(), [=] { box->closeBox(); });
	}));
}

template <typename Value, typename Convert, typename Callback>
not_null<Ui::MediaSlider*> LocalStorage::createLimitsSlider(
		not_null<Ui::VerticalLayout*> container,
		int valuesCount,
		const QString &name,
		Convert &&convert,
		Value currentValue,
		Callback &&callback) {
	const auto row = container->add(
		object_ptr<Ui::FixedHeightWidget>(
			container, st::localStorageLimitLabel.font->height),
		st::localStorageLimitLabelMargin);
	const auto title = Ui::CreateChild<Ui::LabelSimple>(
		row, st::localStorageLimitLabel, name);
	const auto label = Ui::CreateChild<Ui::LabelSimple>(row, st::localStorageLimitValue);
	rpl::combine(row->widthValue(), label->widthValue()) | rpl::on_next([=](int width, int) {
		title->moveToLeft(0, 0, width);
		label->moveToRight(0, 0, width);
	}, row->lifetime());
	callback(label, currentValue);
	const auto slider = container->add(
		object_ptr<Ui::MediaSlider>(container, st::localStorageLimitSlider),
		st::localStorageLimitMargin);
	slider->resize(st::localStorageLimitSlider.seekSize);
	slider->setPseudoDiscrete(
		valuesCount, std::forward<Convert>(convert), currentValue,
		[=, callback = std::forward<Callback>(callback)](Value value) {
			callback(label, value);
		});
	return slider;
}

void LocalStorage::updateMediaLimit() {
	const auto good = [&](int64 mediaLimit) {
		return (_totalSizeLimit - mediaLimit >= kMinimalSizeLimit);
	};
	if (good(_mediaSizeLimit) || !_mediaSlider || !_mediaLabel) {
		return;
	}
	auto index = 1;
	while ((index < kMediaSizeLimitsCount)
		&& (MediaSizeLimit(index) * 2 <= _totalSizeLimit)) {
		++index;
	}
	--index;
	_mediaSizeLimit = MediaSizeLimit(index);
	_mediaSlider->setValue(index / float64(kMediaSizeLimitsCount - 1));
	updateMediaLabel();
	Ensures(good(_mediaSizeLimit));
}

void LocalStorage::updateTotalLimit() {
	const auto good = [&](int64 totalLimit) {
		return (totalLimit - _mediaSizeLimit >= kMinimalSizeLimit);
	};
	if (good(_totalSizeLimit) || !_totalSlider || !_totalLabel) {
		return;
	}
	auto index = kTotalSizeLimitsCount - 1;
	while ((index > 0)
		&& (TotalSizeLimit(index - 1) >= 2 * _mediaSizeLimit)) {
		--index;
	}
	_totalSizeLimit = TotalSizeLimit(index);
	_totalSlider->setValue(index / float64(kTotalSizeLimitsCount - 1));
	updateTotalLabel();
	Ensures(good(_totalSizeLimit));
}

void LocalStorage::updateTotalLabel() {
	Expects(_totalLabel != nullptr);
	_totalLabel->setText(SizeLimitText(_totalSizeLimit));
}

void LocalStorage::updateMediaLabel() {
	Expects(_mediaLabel != nullptr);
	_mediaLabel->setText(SizeLimitText(_mediaSizeLimit));
}

void LocalStorage::setupLimits(not_null<Ui::VerticalLayout*> container) {
	_totalSlider = createLimitsSlider(
		container,
		kTotalSizeLimitsCount,
		tr::lng_local_storage_size_limit_title(tr::now),
		TotalSizeLimit,
		_totalSizeLimit,
		[=](not_null<Ui::LabelSimple*> label, int64 limit) {
			_totalSizeLimit = limit;
			_totalLabel = label;
			updateTotalLabel();
			updateMediaLimit();
			applyLimits();
		});
	_totalSlider->setObjectName(u"storage/max-cache"_q);
	_mediaSlider = createLimitsSlider(
		container,
		kMediaSizeLimitsCount,
		tr::extras_CacheVideoDocumentLimit(tr::now),
		MediaSizeLimit,
		_mediaSizeLimit,
		[=](not_null<Ui::LabelSimple*> label, int64 limit) {
			_mediaSizeLimit = limit;
			_mediaLabel = label;
			updateMediaLabel();
			updateTotalLimit();
			applyLimits();
		});
	_mediaSlider->setObjectName(u"cache/videoDocumentLimit"_q);
	Ui::AddSkip(container, st::localStorageIslandPadding);
}

void LocalStorage::applyLimits() {
	const auto &settings = _session->local().cacheSettings();
	const auto &settingsBig = _session->local().cacheBigFileSettings();
	const auto sizeLimit = _totalSizeLimit - _mediaSizeLimit;
	const auto changed = (settings.totalSizeLimit != sizeLimit)
		|| (settingsBig.totalSizeLimit != _mediaSizeLimit)
		|| settings.totalTimeLimit
		|| settingsBig.totalTimeLimit;
	if (!changed) {
		return;
	}
	auto update = Storage::Cache::Database::SettingsUpdate();
	update.totalSizeLimit = sizeLimit;
	update.totalTimeLimit = 0;
	auto updateBig = Storage::Cache::Database::SettingsUpdate();
	updateBig.totalSizeLimit = _mediaSizeLimit;
	updateBig.totalTimeLimit = 0;
	_session->local().updateCacheSettings(update, updateBig);
	_session->data().cache().updateSettings(update);
	_session->data().cacheBigFile().updateSettings(updateBig);
}

Type LocalStorageId() {
	return LocalStorage::Id();
}

} // namespace Settings
