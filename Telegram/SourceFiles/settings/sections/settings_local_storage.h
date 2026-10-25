#pragma once

#include "settings/settings_common_session.h"
#include "extras/data/media_cache_manager.h"

namespace Main {
class Session;
} // namespace Main

namespace Ui {
class VerticalLayout;
class LabelSimple;
class MediaSlider;
} // namespace Ui

namespace Settings {

class CacheCategoryRow;
class CacheChart;
class CacheClearButton;
class CacheDeviceBar;
class CacheChats;

[[nodiscard]] Type LocalStorageId();

class LocalStorage : public Section<LocalStorage> {
public:
	LocalStorage(
		QWidget *parent,
		not_null<Window::SessionController*> controller);

	[[nodiscard]] rpl::producer<QString> title() override;
	void showFinished() override;
#ifdef _DEBUG
	void setDebugChats(int count);
	void setDebugUsage(std::array<int64, Extras::kMediaCacheCategoryCount> sizes);
#endif

private:
	void setupContent();
	void setupCategories(not_null<Ui::VerticalLayout*> container);
	void setupRetention(not_null<Ui::VerticalLayout*> container);
	void setupLimits(not_null<Ui::VerticalLayout*> container);
	void requestSnapshot();
	void updateSnapshot(Extras::MediaCacheSnapshot snapshot);
	void updateClearButton();
	void updateChats();
	void clearSelected();
	void showChatCache(Extras::MediaCacheChat chat);
	void updateMediaLimit();
	void updateTotalLimit();
	void updateTotalLabel();
	void updateMediaLabel();
	void applyLimits();
	[[nodiscard]] Extras::MediaCacheMask selectedMask() const;
	[[nodiscard]] int64 selectedBytes() const;

	template <typename Value, typename Convert, typename Callback>
	not_null<Ui::MediaSlider*> createLimitsSlider(
		not_null<Ui::VerticalLayout*> container,
		int valuesCount,
		const QString &name,
		Convert &&convert,
		Value currentValue,
		Callback &&callback);

	const not_null<Main::Session*> _session;
	const not_null<Extras::MediaCacheManager*> _manager;
	Extras::MediaCacheSnapshot _snapshot;
	std::array<CacheCategoryRow*, Extras::kMediaCacheCategoryCount> _rows = {};
	std::array<bool, Extras::kMediaCacheCategoryCount> _selected
		= { true, true, true, true, true, false };
	CacheChart *_chart = nullptr;
	CacheClearButton *_clearButton = nullptr;
	CacheDeviceBar *_deviceBar = nullptr;
	CacheChats *_chats = nullptr;
	Ui::FlatLabel *_status = nullptr;
	Ui::MediaSlider *_totalSlider = nullptr;
	Ui::LabelSimple *_totalLabel = nullptr;
	Ui::MediaSlider *_mediaSlider = nullptr;
	Ui::LabelSimple *_mediaLabel = nullptr;
	int64 _totalSizeLimit = 0;
	int64 _mediaSizeLimit = 0;
	bool _snapshotPending = false;
	bool _snapshotAgain = false;
	bool _loaded = false;
#ifdef _DEBUG
	bool _debugChats = false;
#endif
};

} // namespace Settings
