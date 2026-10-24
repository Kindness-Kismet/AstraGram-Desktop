#pragma once

#include "extras/data/media_cache_manager.h"
#include "settings/settings_common.h"

namespace Settings {

class CacheChats final : public Ui::RpWidget {
public:
	CacheChats(QWidget *parent, not_null<Main::Session*> session,
		Fn<void(Extras::MediaCacheChat)> chosen);
	~CacheChats();
	void setChats(const std::vector<Extras::MediaCacheChat> &chats, int64 unassigned);
	void setClearing(bool clearing);

protected:
	int resizeGetHeight(int newWidth) override;
	void visibleTopBottomUpdated(int top, int bottom) override;
	bool focusNextPrevChild(bool next) override;

private:
	struct Row;
	void setupViewport();
	void updateViewport();
	void refreshRows();
	void scrollToRow(int index);
	[[nodiscard]] int rowHeight();
	[[nodiscard]] int rowCount() const;

	const not_null<Main::Session*> _session;
	Fn<void(Extras::MediaCacheChat)> _chosen;
	std::vector<Extras::MediaCacheChat> _chats;
	base::flat_map<int, std::unique_ptr<Row>> _rows;
	int64 _unassigned = 0;
	int _visibleTop = 0;
	int _visibleBottom = 0;
	bool _clearing = false;
};

} // namespace Settings
