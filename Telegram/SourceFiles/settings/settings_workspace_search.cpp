#include "settings/settings_workspace_search.h"

#include "base/event_filter.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "extras/features/media_viewer/viewer_host.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/painter.h"
#include "ui/search_field_controller.h"
#include "ui/ui_utility.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_layers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

#include <QTextEdit>

namespace Settings {
namespace {

// 结果区最多显示的行数，超出后滚动。
constexpr auto kMaxVisibleRows = 6.5;
constexpr auto kMaxRecent = 5;
// 窗口缩放或换主题后等界面稳定再重新模糊底层。
constexpr auto kBackdropRefreshDelay = crl::time(200);

QString searchTerms(const QString &text) {
	return text.normalized(QString::NormalizationForm_KC).toCaseFolded().simplified();
}

QString entryKey(const Builder::SearchEntry &entry, const QString &path) {
	return entry.id.isEmpty() ? path + '/' + entry.title : entry.id;
}

int resultRowHeight() {
	return st::mentionPadding.top() + st::mentionFont->height
		+ style::ConvertScale(4) + st::normalFont->height
		+ st::mentionPadding.bottom();
}

class SearchResultButton final : public Ui::AbstractButton {
public:
	SearchResultButton(QWidget *parent, QString title, QString path,
			const style::icon *icon)
	: AbstractButton(parent)
	, _title(std::move(title))
	, _path(std::move(path))
	, _icon(icon ? icon : &st::menuIconSettings) {
	}

protected:
	int resizeGetHeight(int width) override {
		return resultRowHeight();
	}
	void paintEvent(QPaintEvent *e) override {
		auto p = Painter(this);
		if (isOver()) {
			auto hq = PainterHighQualityEnabler(p);
			const auto radius = style::ConvertScale(10);
			p.setPen(Qt::NoPen);
			p.setBrush(st::windowBgOver);
			p.drawRoundedRect(rect(), radius, radius);
		}
		const auto left = 2 * st::mentionPadding.left() + st::mentionPhotoSize;
		const auto available = width() - left - st::mentionPadding.right();
		_icon->paint(p, st::mentionPadding.left()
			+ (st::mentionPhotoSize - _icon->width()) / 2,
			(height() - _icon->height()) / 2, width());
		p.setFont(st::mentionFont);
		p.setPen(isOver() ? st::mentionNameFgOver : st::mentionNameFg);
		p.drawTextLeft(left, st::mentionPadding.top(), width(),
			st::mentionFont->elided(_title, available));
		p.setFont(st::normalFont);
		p.setPen(isOver() ? st::mentionFgOver : st::mentionFg);
		p.drawTextLeft(left, st::mentionPadding.top() + st::mentionFont->height
			+ style::ConvertScale(4), width(), st::normalFont->elided(_path, available));
	}

private:
	const QString _title;
	const QString _path;
	const style::icon *_icon;
};

} // namespace

WorkspaceSearch::WorkspaceSearch(
	not_null<Main::Session*> session,
	std::shared_ptr<std::vector<QString>> recent,
	Fn<void(Builder::SearchEntry)> activate)
: _session(session)
, _recent(std::move(recent))
, _activate(std::move(activate))
, _search(std::make_unique<Ui::SearchFieldController>(QString()))
, _backdropTimer([=] { refreshBackdrop(); }) {
	setObjectName(u"settings-search"_q);
	_field = _search->createField(this, st::dialogsFilter).release();
	_field->setObjectName(u"settings-search-input"_q);
	_field->rawTextEdit()->setObjectName(u"settings-search-editor"_q);
	_field->setPlaceholder(tr::extras_SettingsSearchPlaceholder());
	_field->setAccessibleName(tr::extras_SettingsSearchPlaceholder(tr::now));
	_field->customUpDown(true);
	_field->show();
	_clear = Ui::CreateChild<Ui::CrossButton>(_field, st::dialogsCancelSearch);
	_clear->setObjectName(u"settings-search-clear"_q);
	_clear->setAccessibleName(tr::lng_sr_clear_search(tr::now));
	_clear->setClickedCallback([=] {
		_field->setText({});
		_field->setFocusFast();
	});
	_field->sizeValue() | rpl::on_next([=](QSize size) {
		_clear->moveToRight(0, (size.height() - _clear->height()) / 2);
	}, lifetime());
	_scroll = Ui::CreateChild<Ui::ScrollArea>(this);
	_scroll->setObjectName(u"settings-search-scroll"_q);
	_list = _scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(_scroll));
	_clear->toggle(false, anim::type::instant);
	_search->queryChanges() | rpl::on_next([=](const QString &query) {
		_clear->toggle(!query.isEmpty(), anim::type::instant);
		refreshResults();
	}, lifetime());
	base::install_event_filter(this, _field->rawTextEdit(), [=](not_null<QEvent*> e) {
		return (e->type() == QEvent::KeyPress && handleKey(static_cast<QKeyEvent*>(e.get())))
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
	Lang::Updated() | rpl::on_next([=] {
		rebuildIndex();
		refreshResults();
	}, lifetime());
	style::PaletteChanged() | rpl::on_next([=] {
		_backdropTimer.callOnce(kBackdropRefreshDelay);
	}, lifetime());
	rebuildIndex();
	refreshResults();
}

WorkspaceSearch::~WorkspaceSearch() = default;

void WorkspaceSearch::parentResized() {
	resizeToWidth(parentWidget()->width());
	moveToLeft(0, 0);
	if (_backdrop.isNull()) {
		refreshBackdrop();
	} else if (_backdrop.size() != size() * style::DevicePixelRatio()) {
		_backdropTimer.callOnce(kBackdropRefreshDelay);
	}
}

void WorkspaceSearch::refreshBackdrop() {
	using namespace ExtrasFeatures::MediaViewer;
	_backdrop = prepareBackdrop(grabUnderLayer(this));
	update();
}

int WorkspaceSearch::resizeGetHeight(int newWidth) {
	const auto parent = parentWidget();
	if (!parent) {
		return height();
	}
	const auto available = newWidth - 2 * st::settingsCardPagePadding;
	const auto column = std::max(1, std::min(st::settingsSearchLayerWidth, available));
	const auto left = (newWidth - column) / 2;
	_field->resizeToWidth(column);
	_field->moveToLeft(left, st::settingsSearchLayerTop, newWidth);
	const auto &padding = st::settingsSearchResultsPadding;
	_list->resizeToWidth(std::max(1, column - padding.left() - padding.right()));
	update();
	if (!_list->height()) {
		_scroll->hide();
		_card = QRect();
		return parent->height();
	}
	// 结果卡片下方留出与顶部相同的距离，超出部分在卡片内滚动。
	const auto top = _field->y() + _field->height() + st::settingsSearchResultsSkip;
	const auto room = parent->height() - top - st::settingsSearchLayerTop
		- padding.top() - padding.bottom();
	const auto scrollHeight = std::max(resultRowHeight(), std::min({
		_list->height(),
		int(kMaxVisibleRows * resultRowHeight()),
		room,
	}));
	_card = QRect(
		left,
		top,
		column,
		padding.top() + scrollHeight + padding.bottom());
	_scroll->setGeometry(_card.marginsRemoved(padding));
	_scroll->show();
	return parent->height();
}

void WorkspaceSearch::paintEvent(QPaintEvent *e) {
	auto p = QPainter(this);
	p.drawImage(rect(), _backdrop);
	if (_card.isEmpty()) {
		return;
	}
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(st::boxBg);
	p.drawRoundedRect(_card, st::boxRadius, st::boxRadius);
}

void WorkspaceSearch::mousePressEvent(QMouseEvent *e) {
	// 点到输入框与结果卡片之外的模糊背景即关闭，关闭可能立即销毁自身。
	if (_card.contains(e->pos())) {
		return;
	}
	Ui::PostponeCall(this, [=] { closeLayer(); });
}

void WorkspaceSearch::doSetInnerFocus() {
	_field->setFocusFast();
}

void WorkspaceSearch::rebuildIndex() {
	_entries.clear();
	const auto &registry = Builder::SearchRegistry::Instance();
	auto seen = base::flat_set<QString>();
	auto destinations = base::flat_map<QString, int>();
	for (auto &entry : registry.collectAll(_session)) {
		if (entry.title.isEmpty() || (!entry.section && entry.deeplink.isEmpty())) {
			continue;
		}
		const auto path = registry.sectionPath(entry.section, entry.id.isEmpty());
		if (!seen.emplace(entryKey(entry, path)).second) {
			continue;
		}
		const auto destination = path + '\n' + entry.title;
		const auto [i, added] = destinations.emplace(destination, int(_entries.size()));
		if (!added && (entry.id.isEmpty() || _entries[i->second].entry.id.isEmpty())) {
			_entries[i->second].keywordTerms += ' ' + searchTerms(entry.keywords.join(' '));
			continue;
		}
		_entries.push_back({
			.entry = std::move(entry),
			.path = path,
		});
		auto &indexed = _entries.back();
		indexed.titleTerms = searchTerms(indexed.entry.title);
		indexed.keywordTerms = searchTerms(indexed.entry.keywords.join(' '));
		indexed.pathTerms = searchTerms(path);
	}
}

void WorkspaceSearch::refreshResults() {
	_buttons.clear();
	_list->clear();
	_results.clear();
	_selected = -1;
	const auto query = searchTerms(_search->query());
	if (query.isEmpty()) {
		for (const auto &key : *_recent) {
			for (auto i = 0; i != int(_entries.size()); ++i) {
				if (entryKey(_entries[i].entry, _entries[i].path) == key) {
					_results.push_back(i);
					break;
				}
			}
		}
		if (!_results.empty()) {
			_list->add(object_ptr<Ui::FlatLabel>(_list,
				tr::extras_SettingsSearchRecent(), st::settingsCardHint),
				st::settingsSearchHintPadding);
		}
	} else {
		const auto words = query.split(' ', Qt::SkipEmptyParts);
		auto ranked = std::vector<std::pair<int, int>>();
		for (auto i = 0; i != int(_entries.size()); ++i) {
			const auto &entry = _entries[i];
			auto score = 0;
			for (const auto &word : words) {
				const auto part = entry.titleTerms.contains(word) ? 100
					: entry.keywordTerms.contains(word) ? 40
					: entry.pathTerms.contains(word) ? 10 : 0;
				if (!part) {
					score = 0;
					break;
				}
				score += part;
			}
			if (!score) {
				continue;
			}
			score += entry.titleTerms == query ? 1000
				: entry.titleTerms.startsWith(query) ? 700
				: entry.titleTerms.contains(query) ? 500 : 0;
			ranked.emplace_back(score, i);
		}
		std::stable_sort(ranked.begin(), ranked.end(), [&](const auto &a, const auto &b) {
			return (a.first != b.first) ? a.first > b.first
				: _entries[a.second].path.size() < _entries[b.second].path.size();
		});
		for (const auto &[score, index] : ranked) {
			_results.push_back(index);
		}
		if (_results.empty()) {
			_list->add(object_ptr<Ui::FlatLabel>(_list,
				tr::extras_SettingsSearchEmpty(), st::settingsCardHint),
				st::settingsSearchHintPadding);
		}
	}
	for (auto i = 0; i != int(_results.size()); ++i) {
		const auto &indexed = _entries[_results[i]];
		const auto button = _list->add(object_ptr<SearchResultButton>(_list,
			indexed.entry.title, indexed.path, indexed.entry.icon.icon),
			style::al_justify);
		button->setObjectName(u"settings-search-result-%1"_q.arg(i));
		button->setProperty("settingsSearchControlId", indexed.entry.id);
		button->setAccessibleName(indexed.entry.title + u", "_q + indexed.path);
		button->setClickedCallback([=] { activateResult(i); });
		button->events() | rpl::on_next([=](not_null<QEvent*> e) {
			if (e->type() == QEvent::Enter) {
				selectResult(i);
			}
		}, button->lifetime());
		_buttons.push_back(button);
	}
	resizeToWidth(width());
	_scroll->scrollToY(0);
	if (!query.isEmpty()) {
		selectResult(0);
	}
}

void WorkspaceSearch::activateResult(int index) {
	if (index < 0 || index >= int(_results.size())) {
		return;
	}
	const auto &indexed = _entries[_results[index]];
	const auto entry = indexed.entry;
	const auto key = entryKey(entry, indexed.path);
	auto &recent = *_recent;
	recent.erase(std::remove(recent.begin(), recent.end(), key), recent.end());
	recent.insert(recent.begin(), key);
	if (int(recent.size()) > kMaxRecent) {
		recent.resize(kMaxRecent);
	}
	// 关闭浮层可能立即销毁自身，之后只使用局部副本。
	const auto activate = _activate;
	closeLayer();
	activate(entry);
}

void WorkspaceSearch::selectResult(int index) {
	if (_buttons.empty()) {
		return;
	}
	_selected = std::clamp(index, 0, int(_buttons.size()) - 1);
	for (auto i = 0; i != int(_buttons.size()); ++i) {
		_buttons[i]->setSynteticOver(i == _selected);
	}
	const auto selected = _buttons[_selected];
	_scroll->scrollToY(selected->y(), selected->y() + selected->height());
}

bool WorkspaceSearch::handleKey(QKeyEvent *event) {
	const auto key = event->key();
	if (key == Qt::Key_Escape) {
		closeLayer();
		return true;
	} else if (key == Qt::Key_Return || key == Qt::Key_Enter) {
		activateResult(std::max(_selected, 0));
		return true;
	} else if (key == Qt::Key_Up || key == Qt::Key_Down) {
		selectResult(_selected + ((key == Qt::Key_Down) ? 1 : -1));
		return true;
	}
	return false;
}

} // namespace Settings
