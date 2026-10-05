#include "settings/settings_workspace_search.h"

#include "base/event_filter.h"
#include "base/flat_map.h"
#include "base/flat_set.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "ui/painter.h"
#include "ui/chat/floating_bar.h"
#include "ui/search_field_controller.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/scroll_area.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_dialogs.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_widgets.h"

#include <QApplication>
#include <QTextEdit>

namespace Settings {
namespace {

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
		Ui::PaintChatBar(p, this, rect(), st::defaultEmojiPan.bg->c,
			isOver() ? st::mentionBgOver->c : QColor());
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
	QWidget *parent,
	QWidget *popupParent,
	not_null<Main::Session*> session,
	Fn<void(Builder::SearchEntry)> activate)
: RpWidget(parent)
, _session(session)
, _activate(std::move(activate))
, _search(std::make_unique<Ui::SearchFieldController>(QString()))
, _popup(popupParent) {
	setObjectName(u"settings-search"_q);
	_field = _search->createField(this, st::settingsSearchField).release();
	_field->setObjectName(u"settings-search-input"_q);
	_field->rawTextEdit()->setObjectName(u"settings-search-editor"_q);
	_field->setPlaceholder(tr::extras_SettingsSearchPlaceholder());
	_field->setAccessibleName(tr::extras_SettingsSearchPlaceholder(tr::now));
	_field->customUpDown(true);
	_field->show();
	const auto icon = Ui::CreateChild<Ui::RpWidget>(_field);
	icon->resize(st::dialogsFilterSearch.size());
	icon->setAttribute(Qt::WA_TransparentForMouseEvents);
	icon->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(icon);
		st::dialogsFilterSearch.paint(p, 0, 0, icon->width());
	}, icon->lifetime());
	icon->show();
	_clear = Ui::CreateChild<Ui::CrossButton>(_field, st::dialogsCancelSearch);
	_clear->setObjectName(u"settings-search-clear"_q);
	_clear->setAccessibleName(tr::lng_sr_clear_search(tr::now));
	_clear->setClickedCallback([=] {
		_field->setText({});
		_field->setFocusFast();
	});
	_field->sizeValue() | rpl::on_next([=](QSize size) {
		icon->moveToLeft(style::ConvertScale(12), (size.height() - icon->height()) / 2);
		_clear->moveToRight(0, (size.height() - _clear->height()) / 2);
	}, lifetime());
	_popup->setObjectName(u"settings-search-results"_q);
	Ui::ApplyAutocompleteSurface(_popup.data(), st::defaultEmojiPan.bg);
	_scroll = Ui::CreateChild<Ui::ScrollArea>(_popup.data());
	_scroll->setAutoFillBackground(false);
	_scroll->setVerticalBarTopSkip(st::windowCardRadius);
	_scroll->setVerticalBarBottomSkip(st::windowCardRadius);
	_scroll->setObjectName(u"settings-search-scroll"_q);
	_list = _scroll->setOwnedWidget(object_ptr<Ui::VerticalLayout>(_scroll));
	_scroll->show();
	_popup->hide();
	_clear->toggle(false, anim::type::instant);
	_search->queryChanges() | rpl::on_next([=](const QString &query) {
		_clear->toggle(!query.isEmpty(), anim::type::instant);
		if (_entries.empty()) {
			rebuildIndex();
		}
		refreshResults();
	}, lifetime());
	_field->focusedChanges() | rpl::on_next([=](bool focused) {
		_focused = focused;
		if (focused) {
			rebuildIndex();
			refreshResults();
		}
	}, lifetime());
	base::install_event_filter(this, _field->rawTextEdit(), [=](not_null<QEvent*> e) {
		return (e->type() == QEvent::KeyPress && handleKey(static_cast<QKeyEvent*>(e.get())))
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
	base::install_event_filter(this, qApp, [=](not_null<QEvent*> e) {
		if (!isVisible()) {
			return base::EventFilterResult::Continue;
		}
		if (e->type() == QEvent::KeyPress) {
			const auto key = static_cast<QKeyEvent*>(e.get());
			if (key->key() == Qt::Key_F && key->modifiers() == Qt::ControlModifier
				&& !QApplication::activeModalWidget()) {
				_field->setFocusFast();
				_field->selectAll();
				return base::EventFilterResult::Cancel;
			}
		}
		if (e->type() == QEvent::MouseButtonPress) {
			const auto global = static_cast<QMouseEvent*>(e.get())->globalPos();
			if (_field->rect().contains(_field->mapFromGlobal(global))) {
				if (!_popup->isVisible()) {
					rebuildIndex();
					refreshResults();
				}
			} else if (!_popup->rect().contains(_popup->mapFromGlobal(global))) {
				hideResults();
			}
		}
		return base::EventFilterResult::Continue;
	});
	Lang::Updated() | rpl::on_next([=] {
		rebuildIndex();
		if (_focused) {
			refreshResults();
		}
	}, lifetime());
}

WorkspaceSearch::~WorkspaceSearch() {
	lifetime().destroy();
	_popup.destroy();
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
		for (const auto &key : _recent) {
			for (auto i = 0; i != int(_entries.size()); ++i) {
				if (entryKey(_entries[i].entry, _entries[i].path) == key) {
					_results.push_back(i);
					break;
				}
			}
		}
		if (_results.empty()) {
			hideResults();
			return;
		}
		_list->add(object_ptr<Ui::FlatLabel>(_list,
			tr::extras_SettingsSearchRecent(), st::settingsCardHint),
			st::settingsSearchHintPadding);
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
	_resultsShown = true;
	updateLayout();
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
	_recent.erase(std::remove(_recent.begin(), _recent.end(), key), _recent.end());
	_recent.insert(_recent.begin(), key);
	if (_recent.size() > 5) {
		_recent.resize(5);
	}
	_field->rawTextEdit()->clearFocus();
	_field->setText({});
	hideResults();
	_activate(entry);
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
		if (!dismiss()) {
			_field->rawTextEdit()->clearFocus();
			_field->setText({});
			hideResults();
		}
		return true;
	}
	if (key != Qt::Key_Up && key != Qt::Key_Down
		&& key != Qt::Key_Return && key != Qt::Key_Enter) {
		return false;
	}
	if (!_popup->isVisible()) {
		rebuildIndex();
		refreshResults();
	}
	if (key == Qt::Key_Return || key == Qt::Key_Enter) {
		activateResult(std::max(_selected, 0));
	} else {
		selectResult(_selected + ((key == Qt::Key_Down) ? 1 : -1));
	}
	return true;
}

bool WorkspaceSearch::dismiss() {
	if (!_resultsShown) {
		return false;
	}
	hideResults();
	return true;
}

void WorkspaceSearch::hideResults() {
	_resultsShown = false;
	_popup->hide();
	_selected = -1;
}

bool WorkspaceSearch::containsGlobalPoint(QPoint point) const {
	return isVisible() && _field->rect().contains(_field->mapFromGlobal(point));
}

void WorkspaceSearch::updateLayout() {
	const auto padding = st::settingsCardPagePadding;
	const auto fieldWidth = std::max(1, std::min(width() - 2 * padding,
		style::ConvertScale(560)));
	_field->resizeToWidth(fieldWidth);
	_field->moveToLeft((width() - fieldWidth) / 2,
		(height() - _field->height() + 1) / 2);
	_list->resizeToWidth(fieldWidth);
	const auto popupParent = _popup->parentWidget();
	const auto position = popupParent->mapFromGlobal(_field->mapToGlobal(QPoint(0,
		_field->height() + style::ConvertScale(4))));
	const auto top = std::max(0, position.y());
	const auto available = std::max(1, popupParent->height() - top - padding);
	const auto popupHeight = std::min({ _list->height(),
		int(4.5 * resultRowHeight()), available });
	_popup->setGeometry(position.x(), top, fieldWidth, popupHeight);
	_scroll->setGeometry(0, 0, fieldWidth, popupHeight);
	// 页面动画会显示所有直接子控件，这里恢复结果面板自身的显示状态。
	_popup->setVisible(_resultsShown);
	if (_resultsShown) {
		_popup->raise();
	}
}

void WorkspaceSearch::resizeEvent(QResizeEvent *e) {
	updateLayout();
}

void WorkspaceSearch::hideEvent(QHideEvent *e) {
	hideResults();
	Ui::RpWidget::hideEvent(e);
}

} // namespace Settings
