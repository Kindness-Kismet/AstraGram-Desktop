#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

Content::Content(
	not_null<Ui::SeparatePanel*> panel,
	std::shared_ptr<Main::SessionShow> show)
: RpWidget(panel)
, _show(std::move(show))
, _panel(panel)
, _scroll(this, st::defaultScrollArea)
, _loadMoreCheck([this] { checkLoadMore(); }) {
	auto &wallet = _show->session().wallet();
	wallet.startPolling();

	setupContent();
	_scroll->show();
}

Content::~Content() {
	_show->session().wallet().stopPolling();
}

[[nodiscard]] bool HistoryShown(not_null<Main::Session*> session) {
	const auto wallet = &session->wallet();
	return !wallet->listsGated()
		&& (!wallet->historyVisibleEmpty()
			|| !wallet->listedSubmittedTransactions().empty()
			|| wallet->sendingTransaction(wallet->windowSend()));
}

[[nodiscard]] rpl::producer<bool> HistoryShownValue(
		not_null<Main::Session*> session) {
	const auto wallet = &session->wallet();
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		wallet->historyUpdates(),
		wallet->sendStateValue() | rpl::to_empty,
		wallet->listsGatedValue() | rpl::to_empty
	)) | rpl::map([=] {
		return HistoryShown(session);
	}) | rpl::distinct_until_changed();
}

[[nodiscard]] rpl::producer<bool> CollectiblesShownValue(
		not_null<Main::Session*> session) {
	const auto wallet = &session->wallet();
	return rpl::single(rpl::empty) | rpl::then(rpl::merge(
		wallet->collectiblesUpdates(),
		wallet->listsGatedValue() | rpl::to_empty
	)) | rpl::map([=] {
		return !wallet->listsGated() && !wallet->collectibles().empty();
	}) | rpl::distinct_until_changed();
}

void PaintBottomRoundedPlate(
		QPainter &p,
		QRect rect,
		const style::color &bg) {
	auto hq = PainterHighQualityEnabler(p);
	p.setPen(Qt::NoPen);
	p.setBrush(bg);
	p.drawRoundedRect(
		rect.marginsAdded({ 0, 2 * st::callRadius, 0, 0 }),
		st::callRadius,
		st::callRadius);
}

[[nodiscard]] std::vector<ListAnchorRow> CountListAnchor(
		const std::vector<ListedRow> &rows,
		not_null<QWidget*> column,
		int visibleTop) {
	if (rows.empty() || visibleTop <= 0) {
		return {};
	}
	auto tops = std::vector<int>();
	tops.reserve(rows.size());
	for (const auto &row : rows) {
		tops.push_back(Ui::MapFrom(column, row.widget, QPoint()).y());
	}
	if (visibleTop < tops.front()) {
		return {};
	}
	const auto count = int(rows.size());
	auto index = count - 1;
	for (auto i = 0; i != count; ++i) {
		if (tops[i] + rows[i].widget->height() > visibleTop) {
			index = i;
			break;
		}
	}
	auto result = std::vector<ListAnchorRow>();
	result.reserve(count);
	const auto add = [&](int i) {
		const auto &key = rows[i].key;
		if (!key.operationId.empty() || !key.id.isEmpty()) {
			result.push_back({ .key = key, .top = tops[i] });
		}
	};
	for (auto i = index; i != count; ++i) {
		add(i);
	}
	for (auto i = index - 1; i >= 0; --i) {
		add(i);
	}
	return result;
}

[[nodiscard]] int CountKeptScrollTop(
		const std::vector<ListAnchorRow> &anchor,
		const std::vector<ListedRow> &rows,
		not_null<QWidget*> column,
		int scrollTop,
		int reserve,
		int maxTop) {
	auto shift = 0;
	for (const auto &entry : anchor) {
		const auto found = ranges::find(rows, entry.key, &ListedRow::key);
		if (found != end(rows)) {
			shift = Ui::MapFrom(column, found->widget, QPoint()).y()
				- entry.top;
			break;
		}
	}
	const auto lower = anchor.empty() ? 0 : std::min(reserve, maxTop);
	return std::clamp(scrollTop + shift, lower, maxTop);
}

void Content::setupContent() {
	_container = _scroll->setOwnedWidget(
		object_ptr<Ui::RpWidget>(_scroll.data()));
	_column = Ui::CreateChild<Ui::PaddingWrap<Ui::VerticalLayout>>(
		_container,
		object_ptr<Ui::VerticalLayout>(_container),
		style::margins());
	_column->show();
	const auto column = _column->entity();
	const auto wallet = &_show->session().wallet();
	auto collectiblesShown = CollectiblesShownValue(&_show->session());

	setupPinned();
	setupBalance();
	setupTabs(rpl::duplicate(collectiblesShown));
	setupStrip();
	setupListsLoading();
	setupProtectRow();

	const auto media = std::make_shared<CollectibleMedia>(&_show->session());
	const auto wrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto about = wrap->entity();
	Ui::AddSkip(about, st::walletAboutTopSkip);
	const auto addEntry = [&](
			rpl::producer<QString> title,
			rpl::producer<QString> text,
			const style::icon &icon) {
		const auto top = about->add(
			object_ptr<Ui::FlatLabel>(
				about,
				std::move(title),
				st::walletAboutTitleLabel),
			st::walletAboutPadding);
		Ui::AddSkip(about, st::walletAboutTitleSkip);
		about->add(
			object_ptr<Ui::FlatLabel>(
				about,
				std::move(text),
				st::walletAboutTextLabel),
			st::walletAboutPadding);
		const auto left = Ui::CreateChild<Ui::RpWidget>(about);
		left->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(left);
			icon.paint(p, 0, 0, left->width());
		}, left->lifetime());
		left->resize(icon.size());
		top->geometryValue(
		) | rpl::on_next([=](const QRect &g) {
			left->moveToLeft(
				st::walletAboutIconLeft,
				g.top() + (top->height() - left->height()) / 2);
		}, left->lifetime());
	};
	addEntry(
		tr::lng_wallet_about_instant_title(),
		tr::lng_wallet_about_instant_text(),
		st::walletAboutInstantIcon);
	Ui::AddSkip(about, st::walletAboutRowSkip);
	addEntry(
		tr::lng_wallet_about_fees_title(),
		tr::lng_wallet_about_fees_text(
			lt_count,
			GaslessDailyTransfersValue(&_show->session()) | tr::to_count()),
		st::walletAboutFeesIcon);
	Ui::AddSkip(about, st::walletAboutRowSkip);
	addEntry(
		tr::lng_wallet_about_chain_title(),
		tr::lng_wallet_about_chain_text(),
		st::walletAboutChainIcon);
	Ui::AddSkip(about, st::walletAboutBottomSkip);

	auto emptyFace = rpl::combine(
		HistoryShownValue(&_show->session()),
		wallet->collectiblesTabValue(),
		wallet->listsEmptyStateValue(),
		wallet->presenceValue()
	) | rpl::map([](
			bool history,
			bool collectibles,
			ListsEmptyState lists,
			Presence presence) {
		return (!lists.confirmedEmpty || history || collectibles)
			? EmptyFace::None
			: (presence == Presence::Unavailable)
			? EmptyFace::Unavailable
			: (lists.unreachable || (presence == Presence::AddressUnreadable))
			? EmptyFace::Unreachable
			: EmptyFace::About;
	});
	wrap->toggleOn(rpl::duplicate(emptyFace) | rpl::map(
		rpl::mappers::_1 == EmptyFace::About));
	wrap->finishAnimating();

	const auto statementWrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto statement = statementWrap->entity();
	Ui::AddSkip(statement, st::walletAboutTopSkip);
	statement->add(
		object_ptr<Ui::FlatLabel>(
			statement,
			rpl::combine(
				tr::lng_wallet_unavailable(),
				tr::lng_wallet_state_error(),
				rpl::duplicate(emptyFace)
			) | rpl::map([](
					const QString &unavailable,
					const QString &error,
					EmptyFace face) {
				return (face == EmptyFace::Unavailable) ? unavailable : error;
			}),
			st::walletAboutTextLabel),
		st::boxRowPadding,
		style::al_top);
	Ui::AddSkip(statement, st::walletAboutBottomSkip);
	statementWrap->toggleOn(std::move(emptyFace) | rpl::map(
		(rpl::mappers::_1 == EmptyFace::Unavailable)
		|| (rpl::mappers::_1 == EmptyFace::Unreachable)));
	statementWrap->finishAnimating();

	const auto rowsTopSkip = column->add(Ui::CreateSlideSkipWidget(
		column,
		st::walletRowsTopSkip));
	rowsTopSkip->toggleOn(rpl::combine(
		std::move(collectiblesShown),
		wallet->collectiblesTabValue(),
		HistoryShownValue(&_show->session())
	) | rpl::map([](bool available, bool collectibles, bool history) {
		return history || (available && collectibles);
	}));
	rowsTopSkip->finishAnimating();

	const auto listWrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	const auto rows = listWrap->entity();
	const auto list = rows->add(object_ptr<Ui::VerticalLayout>(rows));
	struct ListPlace {
		std::vector<ListedRow> rows;
		int lastTop = 0;
		bool rebuilding = false;
	};
	const auto place = lifetime().make_state<ListPlace>();
	place->lastTop = _scroll->scrollTop();
	const auto listedRows = [=] {
		auto result = std::vector<ListedRow>();
		result.reserve(place->rows.size() + 1);
		if (_sendingRow) {
			result.push_back({
				.key = { .operationId = _sendingRow->operationId },
				.widget = _sendingRow->slot,
			});
		}
		result.insert(end(result), begin(place->rows), end(place->rows));
		return result;
	};
	const auto fitContainer = [=] {
		const auto height = _column->height();
		_container->resize(
			_container->width(),
			(place->rebuilding
				? std::max(height, _container->height())
				: height));
	};
	const auto releaseSending = [=] {
		if (_sendingRow) {
			delete _sendingRow->slot;
			_sendingRow = nullptr;
		}
	};
	const auto rebuild = list->lifetime().make_state<Fn<void()>>();
	const auto holdSending = [=](
			const std::string &operationId,
			const TransferItem &item,
			bool sending) {
		auto created = false;
		if (!_sendingRow || _sendingRow->operationId != operationId) {
			releaseSending();
			_sendingRow = std::make_unique<SendingRow>(SendingRow{
				.operationId = operationId,
				.slot = rows->insert(
					0,
					object_ptr<Ui::VerticalLayout>(rows)),
				.revealPending = true,
			});
			created = true;
		}
		auto content = HistoryRowContent();
		if (sending) {
			auto shown = item;
			shown.status = TransferItem::Status::Success;
			content = RowContentFromItem(shown, &_show->session());
			content.date = tr::lng_wallet_row_sending(tr::now);
		} else {
			content = RowContentFromItem(item, &_show->session());
		}
		_sendingRow->item = item;
		const auto slot = _sendingRow->slot;
		const auto look = _sendingRow->look;
		const auto settle = look
			&& !sending
			&& (look->settling()
				|| (!look->settled()
					&& !anim::Disabled()
					&& look->surfaceShown()
					&& look->inView()
					&& (content.itemAmount
						== _sendingRow->content.itemAmount)));
		if (look && sending) {
			if (content != _sendingRow->content) {
				look->setContent(content);
				_sendingRow->content = std::move(content);
			}
			return created;
		} else if (settle) {
			if (!look->settling() || content != _sendingRow->content) {
				// Only the diamond's landing bursts; a collectible lands none.
				const auto burst = !content.itemAmount
					&& (item.status == TransferItem::Status::Success);
				look->settle(
					content,
					burst,
					crl::guard(list, [=] { (*rebuild)(); }));
				_sendingRow->content = std::move(content);
			}
			return created;
		} else if (!slot->count()
			|| _sendingRow->look
			|| content != _sendingRow->content) {
			slot->clear();
			_sendingRow->look = nullptr;
			const auto click = [=] {
				if (_sendingRow) {
					ShowWalletTransactionBox(_show, _sendingRow->item, media);
				}
			};
			if (sending) {
				_sendingRow->look = AddSendingHistoryRow(
					slot,
					column,
					listWrap,
					content,
					media,
					click);
			} else {
				AddHistoryRow(slot, content, click, media);
			}
			_sendingRow->content = std::move(content);
		}
		return created;
	};
	const auto rebuildList = [=] {
		const auto scrollTop = _scroll->scrollTop();
		const auto anchor = listWrap->toggled()
			? CountListAnchor(listedRows(), column, scrollTop - _reserve)
			: std::vector<ListAnchorRow>();
		place->rows.clear();
		// WHY: clear() collapses the column and the scroll area clamps to it
		// at once, so the geometry readers wait for the rebuilt rows, which
		// then keep their place on screen in one move.
		place->rebuilding = true;
		list->clear();
		const auto &history = wallet->history();
		auto submitted = wallet->listedSubmittedTransactions();
		// A row sits by date, so old failures stop covering fresh history.
		ranges::stable_sort(submitted, ranges::greater(), [](const auto &entry) {
			return entry.item.date.value_or(kUndatedRowDate);
		});
		const auto addItem = [=](const TransferItem &item, ListRowKey key) {
			const auto content = RowContentFromItem(item, &_show->session());
			place->rows.push_back({
				.key = std::move(key),
				.widget = AddHistoryRow(list, content, [=] {
					ShowWalletTransactionBox(_show, item, media);
				}, media),
			});
		};
		auto created = false;
		if (HistoryShown(&_show->session())) {
			const auto &op = wallet->windowSend();
			const auto sending = wallet->sendingTransaction(op);
			const auto settled = (op.empty() || sending)
				? end(submitted)
				: ranges::find(
					submitted,
					op,
					&ListedSubmittedTransfer::operationId);
			const auto newest = [&] {
				auto result = TimeId();
				for (const auto &item : history) {
					if (item.date && !wallet->historyItemHidden(item)) {
						result = std::max(result, *item.date);
					}
				}
				for (auto i = begin(submitted); i != end(submitted); ++i) {
					if (i != settled) {
						result = std::max(
							result,
							i->item.date.value_or(kUndatedRowDate));
					}
				}
				return result;
			};
			const auto settling = !sending
				&& _sendingRow
				&& _sendingRow->operationId == op
				&& _sendingRow->look
				&& _sendingRow->look->settling();
			const auto kept = !settling
				? std::optional<TransferItem>()
				: (settled != end(submitted))
				? std::make_optional(settled->item)
				: wallet->submittedTransaction(op);
			const auto held = sending
				|| kept
				|| (settled != end(submitted)
					&& _sendingRow
					&& _sendingRow->operationId == op
					&& (settled->item.date.value_or(kUndatedRowDate)
						>= newest()));
			if (!held) {
				releaseSending();
			} else if (sending) {
				created = holdSending(op, *sending, true);
			} else {
				created = holdSending(
					op,
					kept ? *kept : settled->item,
					false);
			}
			const auto skipId = !held
				? QString()
				: sending
				? sending->id
				: kept
				? kept->id
				: QString();
			auto next = begin(submitted);
			const auto addNext = [&] {
				const auto &entry = *(next++);
				if (!held || entry.operationId != op) {
					addItem(entry.item, { .operationId = entry.operationId });
				}
			};
			const auto addNewerThan = [&](TimeId date) {
				while (next != end(submitted)
					&& next->item.date.value_or(kUndatedRowDate) >= date) {
					addNext();
				}
			};
			for (const auto &item : history) {
				if (item.date) {
					addNewerThan(*item.date);
				}
				if (!wallet->historyItemHidden(item)
					&& (skipId.isEmpty() || item.id != skipId)) {
					addItem(item, { .id = item.id });
				}
			}
			while (next != end(submitted)) {
				addNext();
			}
			Ui::AddSkip(list, st::walletRowsTopSkip);
		} else {
			releaseSending();
		}
		if (const auto width = rows->width()) {
			rows->resizeToWidth(width);
		}
		if (width() && height()) {
			const auto columnHeight = column->height();
			const auto regions = countRegions(columnHeight);
			_scroll->scrollToY(CountKeptScrollTop(
				anchor,
				listedRows(),
				column,
				scrollTop,
				_reserve,
				std::max(
					columnHeight + regions.reserve - regions.scrollHeight,
					0)));
		}
		place->rebuilding = false;
		fitContainer();
		updateRegions();
		if (created) {
			Ui::PostponeCall(this, [=] { revealSendingRow(); });
		}
	};
	*rebuild = rebuildList;
	rpl::merge(
		wallet->historyUpdates(),
		wallet->collectiblesUpdates(),
		wallet->sendStateValue() | rpl::to_empty,
		wallet->listsGatedValue() | rpl::to_empty
	) | rpl::on_next(rebuildList, list->lifetime());
	listWrap->toggleOn(TransactionsShownValue(&_show->session()));
	listWrap->finishAnimating();

	const auto collectiblesWrap = column->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			column,
			object_ptr<Ui::VerticalLayout>(column)));
	AddCollectiblesList(collectiblesWrap->entity(), _show, media);
	collectiblesWrap->toggleOn(wallet->collectiblesTabValue());
	collectiblesWrap->finishAnimating();

	_scroll->scrolls(
	) | rpl::on_next([=] {
		// A reader who moved the list down is asking for more of it, so
		// the bound the session spends on hidden transaction pages is
		// re-armed here, and only for that. Every other way this fires is
		// a clamp nobody made - a section sliding shut, a
		// resize growing the viewport - and a clamp can only lower the
		// position, so requiring it to grow rejects all of them. The last
		// seen position is kept beside the handler and not inside it,
		// because rpl invokes a copy of the handler on every emission and
		// a value captured in it would never carry to the next one.
		const auto top = _scroll->scrollTop();
		// A rebuild's anchored move grows it too, and is no reader's scroll.
		const auto moved = !place->rebuilding && (top > place->lastTop);
		place->lastTop = top;
		if (moved) {
			wallet->resetHiddenHistoryPages();
		}
		_loadMoreCheck.call();
	}, lifetime());

	_scroll->scrollTopValue(
	) | rpl::on_next([=](int) {
		if (place->rebuilding) {
			return;
		}
		updatePinned();
		updateVisibleArea();
	}, lifetime());

	_container->widthValue(
	) | rpl::on_next([=](int width) {
		_column->resizeToWidth(width);
	}, _column->lifetime());
	_column->heightValue(
	) | rpl::on_next([=] {
		fitContainer();
	}, _column->lifetime());

	_pinnedInner->heightValue(
	) | rpl::on_next([=] {
		updateRegions();
	}, lifetime());

	_column->entity()->heightValue(
	) | rpl::on_next([=] {
		if (!place->rebuilding) {
			updateRegions();
		}
		_loadMoreCheck.call();
	}, lifetime());

	_pinnedBackground->raise();
	_card->raise();
	_pinned->raise();
	_cardButton->raise();
	_tabsShadow->raise();
	_headerShadow->raise();
	_stripShadow->raise();
	_strip->raise();

	const auto local = &_show->session().local();
	if (!local->readPref<bool>(kIntroTooltipShownPref)) {
		local->writePref<bool>(kIntroTooltipShownPref, true);
		SetupIntroTooltip(this, _card, [=] {
			return (foldProgress() > 0.)
				? QRect()
				: _ink->markRect(cardRest());
		}, _pinned->heightValue() | rpl::to_empty);
	}
}

void Content::setupPinned() {
	_pinnedBackground = Ui::CreateChild<Ui::RpWidget>(this);
	_pinnedBackground->setAttribute(Qt::WA_TransparentForMouseEvents);
	_pinnedBackground->setGeometry(QRect());
	_pinnedBackground->show();

	_pinned = Ui::CreateChild<Ui::RpWidget>(this);
	_pinned->show();
	_pinnedInner = Ui::CreateChild<Ui::VerticalLayout>(_pinned);
	_pinnedInner->show();

	setupInfoIsland();

	Ui::AddSkip(_pinnedInner, st::walletCardTopSkip);
	_cardPlaceholder = _pinnedInner->add(
		object_ptr<Ui::FixedHeightWidget>(
			_pinnedInner,
			st::walletCardHeight),
		st::walletCardMargin);
	_card = Ui::CreateChild<Card>(
		this,
		_show,
		CardNameValue(&_show->session()));
	_card->setGeometry(Ui::MapFrom(
		this,
		_cardPlaceholder,
		_cardPlaceholder->rect()));
	_card->setAttribute(Qt::WA_TransparentForMouseEvents);
	_card->show();
	_card->followCursor();
	_cardButton = Ui::CreateChild<Ui::AbstractButton>(this);
	_cardButton->setClickedCallback([=] {
		ShowWalletReceiveBox(&_show->session(), _show);
	});
	_cardButton->show();

	const auto buttons = _pinnedInner->add(
		object_ptr<Ui::FixedHeightWidget>(
			_pinnedInner,
			st::walletSendButton.height),
		st::walletSendButtonMargin,
		style::al_justify);
	const auto addPill = [&](
			rpl::producer<QString> text,
			Fn<void()> callback) {
		const auto button = Ui::CreateChild<Ui::RoundButton>(
			buttons,
			std::move(text),
			st::walletSendButton);
		button->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		button->setClickedCallback(std::move(callback));
		button->show();
		return button;
	};
	const auto addFunds = addPill(tr::lng_wallet_add_funds(), [=] {
		ShowWalletReceiveBox(&_show->session(), _show);
	});
	const auto send = addPill(tr::lng_send_button(), [show = _show] {
		WhenWalletReady(show, [=] {
			show->showBox(Box(
				WalletSendRecipientBox,
				show,
				QString(),
				nullptr));
		});
	});
	buttons->widthValue(
	) | rpl::on_next([=](int width) {
		const auto single = (width - st::walletButtonsSkip) / 2;
		addFunds->setFullWidth(single);
		addFunds->moveToLeft(0, 0, width);
		const auto left = single + st::walletButtonsSkip;
		send->setFullWidth(width - left);
		send->moveToLeft(left, 0, width);
	}, buttons->lifetime());

	_show->session().wallet().presenceValue(
	) | rpl::on_next([=](Presence presence) {
		const auto ready = (presence == Presence::Ready);
		for (const auto button : { addFunds, send }) {
			SetButtonDisabledLook(button, !ready);
		}
	}, buttons->lifetime());

	Ui::AddSkip(_pinnedInner, st::walletHeaderBottomSkip);

	_headerShadow = Ui::CreateChild<Ui::PlainShadow>(this);

	_pinnedBackground->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(_pinnedBackground);
		const auto height = _pinnedBackground->height();
		const auto tabsTop = height - pinnedMin();
		p.fillRect(
			0,
			0,
			_pinnedBackground->width(),
			tabsTop,
			st::windowBgOver);
		if (tabsTop < height) {
			p.fillRect(
				0,
				tabsTop,
				_pinnedBackground->width(),
				height - tabsTop,
				st::windowBg);
		}
	}, _pinnedBackground->lifetime());

	const auto forwardWheel = [=](not_null<QEvent*> e) {
		if (e->type() != QEvent::Wheel) {
			return base::EventFilterResult::Continue;
		}
		_scroll->viewportEvent(e);
		return base::EventFilterResult::Cancel;
	};
	base::install_event_filter(_pinned, forwardWheel);
	base::install_event_filter(_cardButton, forwardWheel);
}

} // namespace ContentDetails

} // namespace Wallet
