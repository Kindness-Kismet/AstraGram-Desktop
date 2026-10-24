#include "settings/sections/settings_cache_chats.h"

#include "base/event_filter.h"
#include "crl/crl_on_main.h"

#include "data/data_peer.h"
#include "data/data_session.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_card_layout.h"
#include "ui/text/format_values.h"
#include "ui/widgets/elastic_scroll.h"
#include "styles/style_settings.h"

#include <QtWidgets/QApplication>

namespace Settings {

struct CacheChats::Row {
	~Row() { button.destroy(); }
	rpl::variable<QString> name;
	rpl::variable<QString> size;
	object_ptr<Button> button = { nullptr };
};

CacheChats::CacheChats(QWidget *parent, not_null<Main::Session*> session,
		Fn<void(Extras::MediaCacheChat)> chosen)
: RpWidget(parent)
, _session(session)
, _chosen(std::move(chosen)) {
	setObjectName(u"cache/chats"_q);
	setVisualTabOrder(true);
	crl::on_main(this, [=] { setupViewport(); });
}

CacheChats::~CacheChats() = default;

void CacheChats::setupViewport() {
	for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
		base::install_event_filter(parent, [=](not_null<QEvent*> event) {
			if (event->type() == QEvent::Move || event->type() == QEvent::Resize) {
				updateViewport();
			}
			return base::EventFilterResult::Continue;
		}, lifetime());
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			scroll->scrollTopValue() | rpl::on_next([=] { updateViewport(); }, lifetime());
			break;
		}
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(parent)) {
			scroll->scrollTopValue() | rpl::on_next([=] { updateViewport(); }, lifetime());
			break;
		}
	}
}

void CacheChats::updateViewport() {
	for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			const auto top = scroll->scrollTop() - mapTo(scroll->widget(), QPoint()).y();
			visibleTopBottomUpdated(top, top + scroll->height());
			return;
		}
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(parent)) {
			const auto top = scroll->scrollTop() - mapTo(scroll->widget(), QPoint()).y();
			visibleTopBottomUpdated(top, top + scroll->viewport()->height());
			return;
		}
	}
}

int CacheChats::rowHeight() {
	return IsInCardGroup(this)
		? st::settingsCardRowHeight
		: st::settingsButtonNoIcon.height
			+ st::settingsButtonNoIcon.padding.top()
			+ st::settingsButtonNoIcon.padding.bottom();
}

int CacheChats::rowCount() const {
	return std::max(int(_chats.size()), 1) + (_unassigned > 0);
}

void CacheChats::setChats(const std::vector<Extras::MediaCacheChat> &chats,
		int64 unassigned) {
	_chats = chats;
	_unassigned = unassigned;
	resizeToWidth(width());
	updateViewport();
	refreshRows();
}

void CacheChats::setClearing(bool clearing) {
	if (_clearing == clearing) {
		return;
	}
	_clearing = clearing;
	refreshRows();
}

int CacheChats::resizeGetHeight(int newWidth) {
	for (const auto &[index, row] : _rows) {
		row->button->resizeToWidth(newWidth);
	}
	return rowCount() * rowHeight();
}

void CacheChats::visibleTopBottomUpdated(int top, int bottom) {
	_visibleTop = top;
	_visibleBottom = bottom;
	refreshRows();
}

void CacheChats::refreshRows() {
	const auto height = rowHeight();
	const auto count = rowCount();
	const auto first = std::clamp(_visibleTop / height - 1, 0, count);
	const auto last = std::clamp((_visibleBottom + height - 1) / height + 1, first, count);
	for (auto i = _rows.begin(); i != _rows.end();) {
		if (i->first >= first && i->first < last) {
			++i;
		} else {
			i = _rows.erase(i);
		}
	}
	// 只创建可见行及上下各一行，统计更新复用已有控件。
	for (auto index = first; index != last; ++index) {
		auto &row = _rows[index];
		if (!row) {
			row = std::make_unique<Row>();
			row->button = CreateButtonWithIcon(this, row->name.value(), st::settingsButtonNoIcon);
			AddSectionRowDetails(row->button);
			CreateRightLabel(row->button, row->size.value(), st::settingsButtonNoIcon, row->name.value());
			row->button->setClickedCallback([=] {
				if (!_clearing && index < _chats.size()) {
					_chosen(_chats[index]);
				}
			});
		}
		const auto hasChat = index < _chats.size();
		const auto peer = hasChat ? _chats[index].peer : PeerId();
		const auto loaded = hasChat ? _session->data().peerLoaded(peer) : nullptr;
		row->name = hasChat
			? ((loaded && !loaded->name().isEmpty()) ? loaded->name()
				: tr::extras_CacheChatUnknown(tr::now, lt_id, QString::number(peer.value)))
			: (_chats.empty() && index == 0)
			? tr::extras_CacheNoChats(tr::now) : tr::extras_CacheUnassigned(tr::now);
		row->size = hasChat ? Ui::FormatSizeText(_chats[index].bytes)
			: (_chats.empty() && index == 0) ? QString() : Ui::FormatSizeText(_unassigned);
		row->button->setObjectName(hasChat
			? u"cache/chat/"_q + QString::number(peer.value) : u"cache/chat-summary"_q);
		row->button->setDisabled(_clearing || !hasChat);
		row->button->resizeToWidth(width());
		row->button->moveToLeft(0, index * height);
		row->button->show();
	}
}

void CacheChats::scrollToRow(int index) {
	const auto top = index * rowHeight();
	for (auto parent = parentWidget(); parent; parent = parent->parentWidget()) {
		if (const auto scroll = dynamic_cast<Ui::ElasticScroll*>(parent)) {
			const auto offset = mapTo(scroll->widget(), QPoint(0, top)).y();
			scroll->scrollToY(offset, offset + rowHeight());
			break;
		}
		if (const auto scroll = dynamic_cast<Ui::ScrollArea*>(parent)) {
			const auto offset = mapTo(scroll->widget(), QPoint(0, top)).y();
			scroll->scrollToY(offset, offset + rowHeight());
			break;
		}
	}
}

bool CacheChats::focusNextPrevChild(bool next) {
	const auto focused = QApplication::focusWidget();
	for (const auto &[index, row] : _rows) {
		if (focused != row->button && !row->button->isAncestorOf(focused)) {
			continue;
		}
		const auto target = index + (next ? 1 : -1);
		if (target < 0 || target >= _chats.size()) {
			break;
		}
		scrollToRow(target);
		const auto i = _rows.find(target);
		if (i != _rows.end()) {
			i->second->button->setFocus(next ? Qt::TabFocusReason : Qt::BacktabFocusReason);
			return true;
		}
		break;
	}
	return RpWidget::focusNextPrevChild(next);
}

} // namespace Settings
