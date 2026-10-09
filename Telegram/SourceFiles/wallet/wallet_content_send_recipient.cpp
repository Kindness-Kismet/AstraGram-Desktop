#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

bool EndSendEntryWait(const std::shared_ptr<SendEntryWait> &wait) {
	if (wait->ended) {
		return false;
	}
	wait->ended = true;
	wait->lifetime.destroy();
	if (const auto close = base::take(wait->closeBusy)) {
		close();
	}
	return wait->show->valid();
}

void FailSendEntryWait(
		const std::shared_ptr<SendEntryWait> &wait,
		const QString &error) {
	if (EndSendEntryWait(wait)) {
		wait->show->showToast(SendUserLoadErrorText(error));
	}
}

[[nodiscard]] std::shared_ptr<SendEntryWait> StartSendEntryWait(
		std::shared_ptr<Main::SessionShow> show) {
	const auto session = &show->session();
	const auto wait = std::make_shared<SendEntryWait>();
	wait->show = show;
	wait->text = tr::lng_wallet_send_wallet_loading(tr::now);
	wait->timeoutError = u"WALLET_NOT_READY"_q;
	const auto weak = std::weak_ptr<SendEntryWait>(wait);
	base::call_delayed(kSendOwnerLookupDelay, session, [=] {
		const auto strong = weak.lock();
		if (!strong || strong->ended || !show->valid()) {
			return;
		}
		strong->closeBusy = ShowWalletBusyBox(
			show,
			strong->text.value(),
			[=] {
				if (const auto strong = weak.lock()) {
					strong->closeBusy = nullptr;
					EndSendEntryWait(strong);
				}
			});
	});
	base::call_delayed(kSendUserLoadTimeout, session, [=] {
		if (const auto strong = weak.lock()) {
			FailSendEntryWait(strong, strong->timeoutError);
		}
	});
	return wait;
}

void AwaitWalletReady(
		const std::shared_ptr<SendEntryWait> &wait,
		Fn<void()> ready,
		Fn<void()> notReady) {
	const auto session = &wait->show->session();
	const auto wallet = &session->wallet();
	if (wallet->presence() == Presence::Ready) {
		ready();
		return;
	}
	wallet->refreshState();
	// A caller with its own answer shows a wallet still being created.
	const auto waitCreated = !notReady;
	wallet->presenceValue(
	) | rpl::filter([=](Presence presence) {
		return (presence != Presence::Unknown)
			&& (!waitCreated || presence != Presence::Provisioning);
	}) | rpl::take(1) | rpl::on_next([=](Presence presence) {
		if (presence == Presence::Ready) {
			// The update that made the wallet Ready finishes first.
			crl::on_main(session, [=] {
				if (!wait->show->valid()) {
					EndSendEntryWait(wait);
				} else if (!wait->ended) {
					ready();
				}
			});
		} else if (notReady) {
			if (EndSendEntryWait(wait)) {
				notReady();
			}
		} else {
			FailSendEntryWait(wait, (presence == Presence::Unavailable)
				? u"WALLET_UNAVAILABLE"_q
				: u"WALLET_NOT_READY"_q);
		}
	}, wait->lifetime);
}

void WhenWalletReady(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> ready,
		Fn<void()> notReady) {
	const auto wait = StartSendEntryWait(std::move(show));
	AwaitWalletReady(wait, [=] {
		if (EndSendEntryWait(wait)) {
			ready();
		}
	}, std::move(notReady));
}

void OpenSendFlow(
		std::shared_ptr<Main::SessionShow> show,
		SendFlow flow,
		AddressOwner owner,
		base::weak_qptr<Ui::BoxContent> origin) {
	if (TransferLinkExpired(flow.expiresAt)) {
		show->showToast(tr::lng_wallet_send_link_expired(tr::now));
		return;
	}
	// WHY: a wallet Telegram names an owner for may not be deployed yet,
	// and a bounceable message to one is returned instead of delivered.
	if (owner.userId) {
		flow.bounce = false;
		flow.displayForm = FormatFriendly(flow.destination, false);
	}
	const auto session = &show->session();
	const auto user = SendableUser(session, owner.userId);
	const auto toUser = user
		&& !user->isSelf()
		&& (!user->gramAddress()
			|| *user->gramAddress() == flow.destination);
	show->showBox(Box(
		WalletSendBox,
		show,
		std::make_optional(std::move(flow)),
		toUser ? user : nullptr,
		Fn<void()>(),
		Fn<void()>(),
		int64(0),
		origin));
}

// A transfer to a user is gasless, even when the link named a wallet.
void ResolveOwnerAndOpenSendFlow(
		std::shared_ptr<Main::SessionShow> show,
		SendFlow flow) {
	const auto session = &show->session();
	const auto wait = StartSendEntryWait(show);
	AwaitWalletReady(wait, [=] {
		if (SendsToOwnWallet(session, flow.destination)) {
			if (EndSendEntryWait(wait)) {
				OpenSendFlow(show, flow, AddressOwner());
			}
			return;
		}
		wait->text = tr::lng_wallet_send_recipient_loading(tr::now);
		wait->timeoutError = u"WALLET_ADDRESS_INVALID"_q;
		session->wallet().userAddresses().resolveOwner(
			flow.destination,
			crl::guard(session, [=](AddressOwner owner) {
				if (EndSendEntryWait(wait)) {
					OpenSendFlow(show, flow, std::move(owner));
				}
			}),
			crl::guard(session, [=](bool silent) {
				if (silent) {
					EndSendEntryWait(wait);
				} else {
					FailSendEntryWait(wait, u"WALLET_ADDRESS_INVALID"_q);
				}
			}));
	});
}

[[nodiscard]] CollectibleRecipient CollectibleRecipientFrom(
		not_null<Main::Session*> session,
		const SendFlow &flow,
		const AddressOwner &owner) {
	const auto user = SendableUser(session, owner.userId);
	const auto toUser = user
		&& !user->isSelf()
		&& (!user->gramAddress()
			|| *user->gramAddress() == flow.destination);
	return {
		.destination = flow.destination,
		.tonName = flow.tonName,
		.userId = toUser ? owner.userId : UserId(),
		.bounce = owner.userId ? false : flow.bounce,
	};
}

void WalletSendRecipientBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		QString text,
		std::shared_ptr<const CollectibleTransfer> collectible) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setTitle(collectible
		? tr::lng_gift_transfer_title(
			lt_name,
			CollectibleNameValue(collectible->media, collectible->address))
		: tr::lng_wallet_send_title());
	AddBoxCloseButton(box);

	const auto session = &show->session();

	struct State {
		std::optional<SendFlow> flow;
		QString name;
		rpl::variable<bool> valid = false;
		rpl::variable<bool> invalid = false;
		rpl::variable<RecipientError> error = RecipientError::Invalid;
		rpl::variable<bool> resolving = false;
		rpl::variable<QString> search;
		rpl::variable<bool> recentHidden = false;
		base::Timer deadline;
		Fn<void(not_null<UserData*>)> chooseUser;
		uint64 revision = 0;
		bool closed = false;
		bool searchCreated = false;
		bool proceedOnName = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto lookup = box->lifetime().make_state<TonNameLookup>(session);
	const auto choose = [=](not_null<UserData*> user) {
		state->chooseUser(user);
	};

	const auto recipient = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins(),
		style::al_justify);
	const auto field = AddSendField(
		recipient,
		st::walletSendField,
		tr::lng_wallet_details_recipient(),
		text);
	const auto errorWrap = recipient->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			recipient,
			object_ptr<Ui::FlatLabel>(
				recipient,
				(state->error.value()
					| rpl::map(RecipientErrorText)
					| rpl::flatten_latest()),
				st::walletSendErrorLabel)),
		style::margins(
			st::walletSendFieldMargin.left(),
			0,
			st::walletSendFieldMargin.right(),
			0));
	errorWrap->toggleOn(state->invalid.value());
	errorWrap->finishAnimating();
	recipient->add(MakeRecentMoneyRecipientsList(
		box,
		show,
		state->recentHidden.value(),
		choose));

	const auto stop = [=] {
		++state->revision;
		state->deadline.cancel();
		state->resolving = false;
	};
	const auto parse = [=] {
		stop();
		state->proceedOnName = false;
		const auto trimmed = field->getLastText().trimmed();
		auto input = ClassifyRecipientInput(trimmed);
		state->flow = std::move(input.flow);
		state->name = (input.kind == RecipientInputKind::Name)
			? trimmed
			: QString();
		state->valid = state->flow.has_value() || !state->name.isEmpty();
		state->invalid = (input.kind == RecipientInputKind::Invalid);
		if (state->invalid.current()) {
			state->error = RecipientError::Invalid;
		}
		const auto searching = (input.kind == RecipientInputKind::Search);
		if (searching && !state->searchCreated) {
			state->searchCreated = true;
			recipient->add(MakeMoneyRecipientSearchList(
				box,
				show,
				state->search.value(),
				choose));
			recipient->resizeToWidth(recipient->width());
		}
		state->search = searching ? trimmed : QString();
		state->recentHidden = searching || !state->name.isEmpty();
		lookup->setName(state->name);
	};
	const auto proceed = [=](SendFlow flow, AddressOwner owner) {
		stop();
		if (state->closed
			|| !show->valid()
			|| &show->session() != session) {
			return;
		} else if (collectible) {
			show->showBox(Box(
				CollectibleTransferBox,
				show,
				collectible,
				CollectibleRecipientFrom(session, flow, owner)));
			return;
		}
		OpenSendFlow(show, std::move(flow), std::move(owner), box.get());
	};
	const auto failName = [=](RecipientError error) {
		stop();
		state->error = error;
		state->invalid = true;
	};
	const auto lookupOwner = [=](SendFlow flow) {
		if (SendsToOwnWallet(session, flow.destination)) {
			if (collectible) {
				failName(RecipientError::OwnWallet);
			} else {
				proceed(flow, AddressOwner());
			}
			return;
		}
		const auto revision = ++state->revision;
		const auto answer = [=](AddressOwner owner) {
			if (revision == state->revision) {
				proceed(flow, std::move(owner));
			}
		};
		const auto fail = [=](bool silent) {
			if (revision != state->revision) {
				return;
			} else if (silent) {
				stop();
			} else {
				failName(RecipientError::LookupFailed);
			}
		};
		state->resolving = true;
		state->deadline.setCallback([=] { fail(false); });
		// resolveOwner has no deadline of its own.
		state->deadline.callOnce(kSendUserLoadTimeout);
		session->wallet().userAddresses().resolveOwner(
			flow.destination,
			crl::guard(session, crl::guard(box, answer)),
			crl::guard(session, crl::guard(box, fail)));
	};
	if (!collectible) {
		state->chooseUser = [=](not_null<UserData*> user) {
			ChooseMoneyRecipient(box, show, user);
		};
	} else {
		state->chooseUser = [=](not_null<UserData*> user) {
			if (state->closed || state->resolving.current()) {
				return;
			} else if (user->isSelf()) {
				failName(RecipientError::OwnWallet);
				return;
			}
			const auto userId = peerToUser(user->id);
			const auto revision = ++state->revision;
			const auto answer = [=](QString address) {
				if (revision != state->revision) {
					return;
				}
				const auto flow = ParseRecipientFlow(
					FormatFriendly(address, false));
				if (!flow) {
					failName(RecipientError::LookupFailed);
				} else if (SendsToOwnWallet(session, flow->destination)) {
					failName(RecipientError::OwnWallet);
				} else {
					proceed(*flow, AddressOwner{
						.userId = userId,
						.address = address,
					});
				}
			};
			const auto fail = [=](ForceResolveError error) {
				if (revision != state->revision) {
					return;
				}
				stop();
				if (!error.silent) {
					show->showToast(SendUserLoadErrorText(error.type));
				}
			};
			state->resolving = true;
			state->deadline.setCallback([=] {
				if (revision == state->revision) {
					failName(RecipientError::LookupFailed);
				}
			});
			state->deadline.callOnce(kSendUserLoadTimeout);
			session->wallet().userAddresses().forceResolve(
				userId,
				crl::guard(session, crl::guard(box, answer)),
				crl::guard(session, crl::guard(box, fail)));
		};
	}
	const auto proceedName = [=](const TonNameState &value) {
		if (auto flow = ParseRecipientFlow(value.address)) {
			flow->tonName = value.name;
			lookupOwner(std::move(*flow));
		} else {
			failName(RecipientError::NameFailed);
		}
	};
	const auto submit = [=] {
		if (state->closed || state->resolving.current()) {
			return;
		} else if (!state->name.isEmpty()) {
			if (lookup->current().status == TonNameStatus::Resolved) {
				proceedName(lookup->current());
			} else {
				state->proceedOnName = true;
				state->resolving = true;
				lookup->request();
			}
			return;
		} else if (!state->flow) {
			field->showError();
			return;
		} else if (TransferLinkExpired(state->flow->expiresAt)) {
			show->showToast(tr::lng_wallet_send_link_expired(tr::now));
			return;
		}
		lookupOwner(*state->flow);
	};
	recipient->add(MakeTonNameResultList(
		box,
		session,
		lookup->value(),
		submit));
	lookup->value() | rpl::on_next([=](const TonNameState &value) {
		const auto status = value.status;
		if (status == TonNameStatus::Pending) {
			state->invalid = false;
		} else if ((status == TonNameStatus::NotFound)
			|| (status == TonNameStatus::Failed)) {
			state->proceedOnName = false;
			failName((status == TonNameStatus::NotFound)
				? RecipientError::NameNotFound
				: RecipientError::NameFailed);
		} else if ((status == TonNameStatus::Resolved)
			&& base::take(state->proceedOnName)) {
			proceedName(value);
		}
	}, box->lifetime());

	const auto button = box->addButton(
		BusyFooterLabel(tr::lng_continue(), state->resolving.value()),
		submit).data();
	state->valid.value() | rpl::on_next([=](bool valid) {
		SetButtonDisabledLook(button, !valid);
	}, button->lifetime());
	AddBusyFooterSpinner(button, state->resolving.value());

	field->changes() | rpl::on_next(parse, field->lifetime());
	field->submits() | rpl::on_next(submit, field->lifetime());
	box->boxClosing() | rpl::on_next([=] {
		state->closed = true;
		stop();
		lookup->close();
	}, box->lifetime());
	box->setFocusCallback([=] { field->setFocusFast(); });
	parse();
}

} // namespace ContentDetails

} // namespace Wallet
