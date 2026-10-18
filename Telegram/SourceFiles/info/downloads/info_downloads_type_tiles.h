#pragma once

#include "info/downloads/info_downloads_common.h"
#include "ui/rp_widget.h"

namespace Info::Downloads {

// 下载分类色块：一行五等分，图标底色沿用文件图标配色。
class TypeTiles final : public Ui::RpWidget {
public:
	TypeTiles(
		QWidget *parent,
		rpl::producer<TypeFilter> active,
		rpl::producer<std::optional<TypeCounts>> counts);

	[[nodiscard]] rpl::producer<TypeFilter> activations() const;

protected:
	int resizeGetHeight(int newWidth) override;

private:
	class Tile;

	std::vector<not_null<Tile*>> _tiles;
	rpl::event_stream<TypeFilter> _activations;

};

} // namespace Info::Downloads
