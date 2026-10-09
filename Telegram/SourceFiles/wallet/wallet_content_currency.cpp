#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

[[nodiscard]] bool ForwardCurrencyNavigation(
		not_null<QKeyEvent*> e,
		not_null<CurrencyListWidget*> list,
		int pageHeight) {
	if (e->key() == Qt::Key_Down) {
		list->selectSkip(1);
	} else if (e->key() == Qt::Key_Up) {
		list->selectSkip(-1);
	} else if (e->key() == Qt::Key_PageDown) {
		list->selectSkipPage(pageHeight, 1);
	} else if (e->key() == Qt::Key_PageUp) {
		list->selectSkipPage(pageHeight, -1);
	} else {
		return false;
	}
	return true;
}

void WalletChooseCurrencyBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	box->setTitle(tr::lng_wallet_currency_title());
	box->setWidth(st::boxWideWidth);
	box->setMaxHeight(st::boxMaxListHeight);
	AddBoxCloseButton(box);
	const auto select = box->setPinnedToTopContent(
		object_ptr<Ui::MultiSelect>(
			box,
			st::defaultMultiSelect,
			tr::lng_country_ph()));
	const auto list = box->addRow(
		object_ptr<CurrencyListWidget>(
			box,
			show,
			[=](QString code) {
				show->session().wallet().rates().setCurrency(code);
				box->closeBox();
			}),
		style::margins());
	box->setFocusCallback([=] { select->setInnerFocus(); });
	select->setQueryChangedCallback([=](const QString &query) {
		box->scrollToY(0);
		list->updateFilter(query);
	});
	select->setSubmittedCallback([=](Qt::KeyboardModifiers) {
		list->chooseSelected();
	});
	select->setCancelledCallback([=] { box->closeBox(); });
	list->mustScrollTo(
	) | rpl::on_next([=](Ui::ScrollToRequest request) {
		box->scrollToY(request.ymin, request.ymax);
	}, list->lifetime());
	base::install_event_filter(select, [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::KeyPress) {
			return base::EventFilterResult::Continue;
		}
		const auto key = static_cast<QKeyEvent*>(e.get());
		const auto pageHeight = box->height() - select->height();
		return ForwardCurrencyNavigation(key, list, pageHeight)
			? base::EventFilterResult::Cancel
			: base::EventFilterResult::Continue;
	});
	box->setShowFinishedCallback([=] { list->scrollToCurrent(); });
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

CurrencyListWidget::CurrencyListWidget(
	not_null<QWidget*> parent,
	std::shared_ptr<Main::SessionShow> show,
	Fn<void(QString)> chosen)
: RpWidget(parent)
, _show(std::move(show))
, _chosen(std::move(chosen)) {
	setAttribute(Qt::WA_OpaquePaintEvent);
	_show->session().wallet().rates().value(
	) | rpl::on_next([=] {
		refreshRows();
	}, lifetime());
}

void CurrencyListWidget::updateFilter(const QString &query) {
	auto filter = TextUtilities::PrepareSearchWords(query);
	if (_filter == filter) {
		return;
	}
	_filter = std::move(filter);
	refreshFiltered();
	_selected = current().empty() ? -1 : 0;
	update();
}

void CurrencyListWidget::selectSkip(int direction) {
	_mouseSelection = false;
	const auto &list = current();
	const auto moved = _selected + direction;
	const auto next = (moved <= 0)
		? (list.empty() ? -1 : 0)
		: (moved >= int(list.size()))
		? -1
		: moved;
	setSelected(next);
	if (_selected >= 0) {
		_mustScrollTo.fire(Ui::ScrollToRequest(
			st::walletCurrencyListSkip
				+ _selected * st::walletCurrencyRowHeight,
			st::walletCurrencyListSkip
				+ (_selected + 1) * st::walletCurrencyRowHeight));
	}
}

void CurrencyListWidget::selectSkipPage(int height, int direction) {
	const auto rows = height / st::walletCurrencyRowHeight;
	if (!rows) {
		return;
	}
	selectSkip(rows * direction);
}

void CurrencyListWidget::chooseSelected() {
	const auto &list = current();
	if (_selected < 0 || _selected >= int(list.size())) {
		return;
	}
	const auto code = list[_selected].code;
	_chosen(code);
}

void CurrencyListWidget::scrollToCurrent() {
	const auto &list = current();
	for (auto i = 0, count = int(list.size()); i != count; ++i) {
		if (list[i].code == _activeCode) {
			_mustScrollTo.fire(Ui::ScrollToRequest(
				st::walletCurrencyListSkip
					+ i * st::walletCurrencyRowHeight,
				st::walletCurrencyListSkip
					+ (i + 1) * st::walletCurrencyRowHeight));
			return;
		}
	}
}

rpl::producer<Ui::ScrollToRequest> CurrencyListWidget::mustScrollTo() const {
	return _mustScrollTo.events();
}

void CurrencyListWidget::paintEvent(QPaintEvent *e) {
	auto p = Painter(this);
	const auto clip = e->rect();
	p.setClipRect(clip);

	const auto &list = current();
	if (list.empty()) {
		p.fillRect(clip, st::windowBg);
		p.setFont(st::noContactsFont);
		p.setPen(st::noContactsColor);
		p.drawText(
			QRect(0, 0, width(), st::noContactsHeight),
			tr::lng_wallet_currency_none(tr::now),
			style::al_center);
		return;
	}
	const auto skip = st::walletCurrencyListSkip;
	const auto rowHeight = st::walletCurrencyRowHeight;
	const auto count = int(list.size());
	const auto top = QRect(0, 0, width(), skip);
	if (clip.intersects(top)) {
		p.fillRect(clip.intersected(top), st::windowBg);
	}
	const auto from = std::clamp((clip.y() - skip) / rowHeight, 0, count);
	const auto till = std::clamp(
		(clip.y() + clip.height() - skip + rowHeight - 1) / rowHeight,
		0,
		count);
	const auto &icon = st::walletCurrencyCheckIcon;
	const auto left = st::walletCurrencyRowPadding.left();
	const auto right = st::walletCurrencyRowPadding.right();
	for (auto i = from; i != till; ++i) {
		const auto &row = list[i];
		const auto selected = (i == (_pressed >= 0 ? _pressed : _selected));
		const auto y = skip + i * rowHeight;
		p.fillRect(
			0,
			y,
			width(),
			rowHeight,
			selected ? st::windowBgOver : st::windowBg);
		if (i < int(_ripples.size()) && _ripples[i]) {
			_ripples[i]->paint(p, 0, y, width());
			if (_ripples[i]->empty()) {
				_ripples[i].reset();
			}
		}
		const auto textTop = y + st::walletCurrencyRowPadding.top();
		p.setFont(st::walletCurrencyRowCodeFont);
		p.setPen(st::walletCurrencyRowCodeFg);
		p.drawTextLeft(left, textTop, width(), row.code);
		if (!row.name.isEmpty()) {
			const auto codeWidth = st::walletCurrencyRowCodeFont->width(
				row.code);
			const auto nameLeft = left
				+ codeWidth
				+ st::walletCurrencyRowNameSkip;
			const auto available = width()
				- nameLeft
				- right
				- icon.width()
				- st::walletCurrencyRowNameSkip;
			p.setFont(st::normalFont);
			p.setPen(st::walletCurrencyRowNameFg);
			p.drawTextLeft(
				nameLeft,
				textTop,
				width(),
				st::normalFont->elided(row.name, available));
		}
		if (row.code == _activeCode) {
			icon.paint(
				p,
				width() - right - icon.width(),
				y + (rowHeight - icon.height()) / 2,
				width());
		}
	}
}

void CurrencyListWidget::enterEventHook(QEnterEvent *e) {
	setMouseTracking(true);
}

void CurrencyListWidget::leaveEventHook(QEvent *e) {
	_mouseSelection = false;
	setMouseTracking(false);
	setSelected(-1);
}

void CurrencyListWidget::mouseMoveEvent(QMouseEvent *e) {
	_mouseSelection = true;
	updateSelected(e->pos());
}

void CurrencyListWidget::mousePressEvent(QMouseEvent *e) {
	_mouseSelection = true;
	updateSelected(e->pos());
	setPressed(_selected);
	const auto &list = current();
	if (_pressed < 0 || _pressed >= int(list.size())) {
		return;
	}
	if (int(_ripples.size()) <= _pressed) {
		_ripples.resize(_pressed + 1);
	}
	if (!_ripples[_pressed]) {
		auto mask = Ui::RippleAnimation::RectMask(
			QSize(width(), st::walletCurrencyRowHeight));
		_ripples[_pressed] = std::make_unique<Ui::RippleAnimation>(
			st::defaultRippleAnimation,
			std::move(mask),
			[this, index = _pressed] { updateRow(index); });
		_ripples[_pressed]->add(e->pos() - QPoint(
			0,
			st::walletCurrencyListSkip
				+ _pressed * st::walletCurrencyRowHeight));
	}
}

void CurrencyListWidget::mouseReleaseEvent(QMouseEvent *e) {
	const auto pressed = _pressed;
	setPressed(-1);
	updateRow(_selected);
	if (e->button() == Qt::LeftButton
		&& pressed >= 0
		&& pressed == _selected) {
		chooseSelected();
	}
}

auto CurrencyListWidget::current() const
-> const std::vector<CurrencyListWidget::Row> & {
	return _filtered;
}

bool CurrencyListWidget::rowMatches(const Row &row) const {
	return ranges::all_of(_filter, [&](const QString &word) {
		return ranges::any_of(row.words, [&](const QString &name) {
			return name.startsWith(word);
		});
	});
}

void CurrencyListWidget::refreshRows() {
	auto &rates = _show->session().wallet().rates();
	_activeCode = rates.current().currency;
	_rows.clear();
	const auto codes = rates.currencies();
	const auto top = TopCurrencies(&_show->session());
	auto ordered = std::vector<QString>();
	ordered.reserve(codes.size());
	for (const auto &code : top) {
		if (ranges::contains(codes, code)) {
			ordered.push_back(code);
		}
	}
	for (const auto &code : codes) {
		if (!ranges::contains(top, code)) {
			ordered.push_back(code);
		}
	}
	_rows.reserve(ordered.size());
	for (const auto &code : ordered) {
		const auto names = LookupCurrencyNames(code);
		_rows.push_back({
			.code = code,
			.name = (names.localized.isEmpty()
				? names.english
				: names.localized),
			.words = TextUtilities::PrepareSearchWords(QStringList{
				code,
				names.english,
				names.localized,
			}.join(QChar(' '))),
		});
	}
	refreshFiltered();
}

void CurrencyListWidget::refreshFiltered() {
	_filtered.clear();
	_filtered.reserve(_rows.size());
	for (const auto &row : _rows) {
		if (_filter.isEmpty() || rowMatches(row)) {
			_filtered.push_back(row);
		}
	}
	_ripples.clear();
	_pressed = -1;
	if (_selected >= int(current().size())) {
		_selected = -1;
	}
	refreshHeight();
	update();
}

void CurrencyListWidget::refreshHeight() {
	const auto &list = current();
	resize(
		width(),
		list.empty()
			? st::noContactsHeight
			: (st::walletCurrencyListSkip
				+ int(list.size()) * st::walletCurrencyRowHeight));
}

void CurrencyListWidget::updateSelected(QPoint localPos) {
	if (!_mouseSelection) {
		return;
	}
	const auto in = visibleRegion().boundingRect().contains(
		mapFromGlobal(QCursor::pos()));
	const auto &list = current();
	const auto skip = st::walletCurrencyListSkip;
	const auto rowsHeight = int(list.size()) * st::walletCurrencyRowHeight;
	const auto selected = (in
		&& localPos.y() >= skip
		&& localPos.y() < skip + rowsHeight)
		? ((localPos.y() - skip) / st::walletCurrencyRowHeight)
		: -1;
	setSelected(selected);
}

void CurrencyListWidget::setSelected(int index) {
	if (_selected == index) {
		return;
	}
	updateRow(_selected);
	_selected = index;
	updateRow(_selected);
}

void CurrencyListWidget::setPressed(int pressed) {
	if (_pressed >= 0
		&& _pressed < int(_ripples.size())
		&& _ripples[_pressed]) {
		_ripples[_pressed]->lastStop();
	}
	_pressed = pressed;
}

void CurrencyListWidget::updateRow(int index) {
	if (index >= 0) {
		update(
			0,
			st::walletCurrencyListSkip
				+ index * st::walletCurrencyRowHeight,
			width(),
			st::walletCurrencyRowHeight);
	}
}

void AcquireKeyThroughLadder(
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<CommentScope> scope,
		Fn<bool()> current,
		rpl::lifetime &lifetime,
		Fn<void(KeyAuthorization)> done,
		rpl::producer<QString> importAbout) {
	const auto context = std::make_shared<KeyContext>(
		std::move(show),
		std::move(scope),
		std::move(current),
		std::move(done));
	lifetime.add([context] { context->cancel(); });
	RunKeyRequiringAction(context, [=] {
		if (!context->valid()) {
			context->cancel();
			return;
		}
		AcquireVaultUnlock({
			.show = context,
			.done = [=](KeyAuthorization auth) {
				context->ready(std::move(auth));
			},
		});
	}, KeyActionKind::ResumeAfterRestore, context,
		std::move(importAbout));
}

} // namespace ContentDetails

} // namespace Wallet
