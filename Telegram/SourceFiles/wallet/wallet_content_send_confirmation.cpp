#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

[[nodiscard]] SendConfirmFee QuoteConfirmFee(const SendQuote &quote) {
	return {
		.feeNano = quote.feeNano,
		.gasless = quote.dependencies.gaslessTerms.eligible(
			quote.args.amountNano,
			quote.args.destination),
	};
}

void FillSendConfirmTable(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Ui::Show> show,
		not_null<Main::Session*> session,
		const QString &address,
		const SendConfirmFee &fee,
		TimeId date) {
	const auto table = AddDetailsTableFrame(container);
	Ui::AddTableRow(
		table,
		tr::lng_wallet_details_address(),
		AddressValueLabel(table, show, address));
	if (fee.feeNano) {
		auto item = TransferItem();
		item.feeNano = fee.feeNano;
		item.gasless = fee.gasless;
		AddFeeTableRow(table, std::move(show), session, item);
	} else {
		AddPendingFeeTableRow(
			table,
			fee.pending ? DetailsFee::Loading : DetailsFee::Failed);
	}
	Ui::AddTableRow(
		table,
		tr::lng_wallet_details_date(),
		rpl::single(tr::marked(langDateTime(base::unixtime::parse(date)))));
}

void AddSendCommentLock(
		not_null<Ui::InputField*> field,
		const style::InputField &st,
		std::shared_ptr<SendDraft> draft) {
	const auto lock = Ui::CreateChild<Ui::IconButton>(
		field,
		st::walletSendConfirmLock);
	lock->setClickedCallback([=] {
		auto comment = draft->comment.current();
		comment.isPublic = !comment.isPublic;
		draft->comment = std::move(comment);
	});
	draft->comment.value() | rpl::map([](const SendComment &comment) {
		return comment.isPublic;
	}) | rpl::distinct_until_changed() | rpl::on_next([=](bool isPublic) {
		const auto icon = isPublic
			? &st::walletSendConfirmLockOff
			: &st::walletSendConfirmLockOn;
		lock->setIconOverride(icon, icon);
	}, lock->lifetime());
	draft->encryptable.value() | rpl::on_next([=](bool encryptable) {
		lock->setVisible(encryptable);
	}, lock->lifetime());
	field->widthValue() | rpl::on_next([=, &st](int) {
		lock->moveToRight(
			0,
			st.textMargins.top()
				+ (st.style.font->height - lock->height()) / 2);
	}, lock->lifetime());
}

SendConfirmNotes AddSendConfirmNotes(
		not_null<Ui::GenericBox*> box,
		rpl::producer<bool> captionShown,
		rpl::producer<QString> refusal) {
	const auto caption = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_wallet_comment_public(),
				st::walletCommentCaptionLabel),
			st::walletCommentCaptionMargin),
		style::margins());
	caption->toggleOn(std::move(captionShown));
	caption->finishAnimating();

	const auto shown = box->addRow(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			box,
			object_ptr<Ui::FlatLabel>(
				box,
				rpl::duplicate(refusal),
				st::walletCommentErrorLabel),
			st::walletCommentCaptionMargin),
		style::margins());
	shown->toggleOn(std::move(refusal) | rpl::map([](const QString &text) {
		return !text.isEmpty();
	}));
	shown->finishAnimating();
	box->addSkip(st::walletSendConfirmBottomSkip);
	return { .caption = caption, .refusal = shown };
}

void WalletSendConfirmBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		SendConfirmArgs args) {
	const auto session = &show->session();
	const auto draft = args.draft;
	const auto address = args.address;
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);
	// An empty title keeps the band the close button stands in.
	box->setTitle(rpl::single(QString()));

	auto item = TransferItem();
	item.amountNano = args.amountNano;
	AddWalletLottie(box);
	AddDetailsAmountHeader(
		box->verticalLayout(),
		item,
		st::walletDetailsLottieSkip,
		st::walletDetailsAmountBottomSkip,
		FiatRateValue(session));

	const auto details = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());
	const auto openedAt = base::unixtime::now();
	struct State {
		std::optional<SendConfirmFee> built;
		SendConfirmFee pending;
		SendConfirmFee shown;
		bool scheduled = false;
	};
	const auto state = box->lifetime().make_state<State>();
	rpl::combine(
		rpl::duplicate(args.fee),
		rpl::duplicate(args.busy)
	) | rpl::filter([](const SendConfirmFee &fee, bool busy) {
		return !busy;
	}) | rpl::map([](const SendConfirmFee &fee, auto) {
		return fee;
	}) | rpl::distinct_until_changed() | rpl::on_next([=](
			const SendConfirmFee &fee) {
		const auto lost = !fee.feeNano && !fee.pending;
		const auto keep = lost && state->built && state->built->feeNano;
		state->pending = keep ? *state->built : fee;
		if (state->scheduled) {
			return;
		}
		state->scheduled = true;
		Ui::PostponeCall(box, [=] {
			state->scheduled = false;
			// The counted value stands while the next one is counted.
			auto shown = state->pending;
			if (shown.feeNano) {
				state->shown = shown;
			} else if (shown.pending && state->shown.feeNano) {
				shown = state->shown;
			}
			if (state->built == shown) {
				return;
			}
			state->built = shown;
			details->clear();
			FillSendConfirmTable(
				details,
				box->uiShow(),
				session,
				address,
				shown,
				openedAt);
		});
	}, details->lifetime());

	const auto field = box->addRow(
		object_ptr<Ui::InputField>(
			box,
			st::walletSendConfirmCommentField,
			Ui::InputField::Mode::NoNewlines,
			tr::lng_wallet_send_comment_optional(),
			draft->comment.current().text),
		st::walletCommentFieldMargin);
	BindCommentField(field, draft);
	AddSendCommentLock(field, st::walletSendConfirmCommentField, draft);
	ApplyCommentLimit(
		field,
		st::walletSendConfirmLock.width + st::walletCommentLimitSkip);
	rpl::duplicate(args.busy) | rpl::on_next([=](bool busy) {
		field->setDisabled(busy);
	}, field->lifetime());

	AddSendConfirmNotes(
		box,
		draft->comment.value() | rpl::map([](const SendComment &comment) {
			return comment.isPublic && !comment.text.isEmpty();
		}),
		std::move(args.refusal));

	const auto send = [=, callback = args.send] {
		callback(state->built ? *state->built : SendConfirmFee());
	};
	const auto button = box->addButton(
		BusyFooterLabel(
			tr::lng_wallet_send_amount(
				lt_amount,
				rpl::single(Ui::FormatTonAmount(args.amountNano).full)),
			rpl::duplicate(args.busy)),
		send).data();
	rpl::combine(
		std::move(args.canSend),
		std::move(args.fee),
		rpl::duplicate(args.busy)
	) | rpl::on_next([=](
			bool canSend,
			const SendConfirmFee &fee,
			bool busy) {
		SetButtonDisabledLook(button, !busy && (!canSend || fee.pending));
	}, button->lifetime());
	AddBusyFooterSpinner(button, std::move(args.busy));

	field->submits() | rpl::on_next(send, field->lifetime());
	box->setFocusCallback([=] { field->setFocusFast(); });
	AddBoxCloseButton(box);
}

[[nodiscard]] rpl::producer<QString> CollectibleNameValue(
		std::shared_ptr<CollectibleMedia> media,
		QString address) {
	return rpl::single(rpl::empty) | rpl::then(
		media->changed(
		) | rpl::filter([=](const QString &changed) {
			return (changed == address);
		}) | rpl::to_empty
	) | rpl::map([=] {
		return CollectibleTitleText(media->view(address)).text;
	});
}

void CollectibleTransferBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::shared_ptr<const CollectibleTransfer> collectible,
		CollectibleRecipient recipient) {
	const auto session = &show->session();
	const auto weakSession = base::make_weak(session);
	const auto wallet = &session->wallet();
	const auto weak = base::make_weak(box);
	const auto identity = wallet->transferWalletIdentity();
	const auto sessionValid = [=] {
		return weakSession
			&& show->valid()
			&& (&show->session() == session);
	};
	const auto current = [=] {
		return weak
			&& sessionValid()
			&& identity
			&& wallet->transferWalletIdentityCurrent(*identity);
	};
	if (!current()) {
		box->closeBox();
		return;
	}
	const auto address = collectible->address;
	const auto media = collectible->media;
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletDetailsBox);
	box->setNoContentMargin(true);
	AddBoxCloseButton(box);

	const auto openedAt = base::unixtime::now();
	const auto item = TransferItem{
		.kind = TransferItem::Kind::Collectible,
		.incoming = false,
		.counterparty = recipient.destination,
		.counterpartyBounceable = recipient.bounce,
		.counterpartyName = recipient.tonName,
		.counterpartyPeer = (recipient.userId
			? peerFromUser(recipient.userId).value
			: quint64()),
		.collectible = address,
		.date = openedAt,
		.status = TransferItem::Status::Success,
	};
	media->resolve(address);
	AddDetailsCollectibleHeader(
		box->verticalLayout(),
		session,
		media,
		item,
		st::walletDetailsAmountBottomSkip / 2,
		false);
	const auto details = box->addRow(
		object_ptr<Ui::VerticalLayout>(box),
		style::margins());

	auto owned = object_ptr<Ui::InputField>(
		box,
		st::walletCollectibleCommentField,
		Ui::InputField::Mode::NoNewlines,
		tr::lng_wallet_send_comment_optional());
	const auto field = owned.data();
	box->addRow(
		MakeCommentBubble(box, std::move(owned), st::windowBg),
		st::walletCommentFieldMargin);
	ApplyCommentLimit(field);

	using ShownFee = std::pair<std::optional<int64>, DetailsFee>;
	struct State {
		std::shared_ptr<const PreparedSend> prepared;
		KeyAuthorization authorization;
		base::Timer signingWait;
		rpl::variable<QString> comment;
		rpl::variable<std::optional<int64>> fee;
		rpl::variable<SendError> error = SendError::None;
		rpl::variable<bool> estimating = false;
		rpl::variable<bool> sending = false;
		Fn<void()> estimate;
		Fn<void()> continueSend;
		ShownFee table;
		std::optional<ShownFee> built;
		uint64 owner = 0;
		uint64 revision = 0;
		int refusals = 0;
		bool tableQueued = false;
		bool requoteQueued = false;
		bool requoteForced = false;
		bool unlocking = false;
		bool submitted = false;
		bool handedOver = false;
		rpl::lifetime signingLifetime;
		// WHY: last, so the key ladder cancelled first finds the rest alive.
		rpl::lifetime keyLifetime;
	};
	const auto state = box->lifetime().make_state<State>();
	state->owner = wallet->createPreviewOwner(box->lifetime());

	wallet->transferWalletIdentityChanges(
	) | rpl::filter([=] {
		return !current();
	}) | rpl::take(1) | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());

	const auto args = [=] {
		return SendArgs{
			.destination = recipient.destination,
			.collectible = address,
			.userId = recipient.userId,
			.comment = SendComment{
				.text = state->comment.current(),
				.isPublic = true,
			},
			.bounce = recipient.bounce,
		};
	};
	state->estimate = [=] {
		if (state->estimating.current()) {
			state->requoteQueued = true;
			return;
		}
		const auto text = state->comment.current();
		if (!CommentFits(text)) {
			state->prepared = nullptr;
			state->fee = std::nullopt;
			state->error = SendError::CommentTooLong;
			return;
		}
		const auto revision = ++state->revision;
		const auto signing = wallet->signingReady();
		state->estimating = true;
		wallet->estimateFee(
			KeyAuthorization(),
			state->owner,
			args(),
			crl::guard(box, [=](FeeResult result) {
				if (revision != state->revision) {
					return;
				}
				const auto forced = base::take(state->requoteForced);
				const auto requote = base::take(state->requoteQueued)
					&& (forced
						|| text != state->comment.current()
						|| signing != wallet->signingReady());
				const auto error = (result.error == SendError::None
						&& !result.prepared)
					? SendError::Failed
					: result.error;
				if (requote || error == SendError::QuoteExpired) {
					state->prepared = nullptr;
					state->estimating = false;
					state->estimate();
				} else {
					state->prepared = std::move(result.prepared);
					state->fee = (error == SendError::None)
						? std::make_optional(result.feeNano)
						: std::nullopt;
					state->error = error;
					state->estimating = false;
				}
				if (state->sending.current() && !state->submitted) {
					state->continueSend();
				}
			}));
	};
	wallet->signingReadyValue(
	) | rpl::skip(1) | rpl::on_next([=](bool ready) {
		if (ready) {
			state->signingWait.cancel();
		}
		if (!state->submitted) {
			state->prepared = nullptr;
			state->estimate();
		}
	}, box->lifetime());
	rpl::merge(
		wallet->sendStateValue() | rpl::skip(1) | rpl::to_empty,
		wallet->presenceValue() | rpl::skip(1) | rpl::to_empty
	) | rpl::on_next([=] {
		if (state->sending.current() || state->submitted) {
			return;
		}
		state->prepared = nullptr;
		state->requoteForced = true;
		state->estimate();
	}, box->lifetime());
	rpl::merge(
		wallet->balanceNanoValue() | rpl::skip(1) | rpl::to_empty,
		wallet->custodyUpdates()
	) | rpl::on_next([=] {
		if (state->sending.current()
			|| state->submitted
			|| (!state->estimating.current()
				&& state->prepared
				&& state->error.current() == SendError::None)) {
			return;
		}
		state->prepared = nullptr;
		state->requoteForced = true;
		state->estimate();
	}, box->lifetime());

	const auto insufficient = [](std::optional<int64> fee, int64 balance) {
		return fee && (CollectibleTransferAttachedNanos() + *fee > balance);
	};
	const auto refusalText = [=](
			SendError error,
			std::optional<int64> fee,
			int64 balance) {
		if (error == SendError::InsufficientBalance
			|| error == SendError::InsufficientFees
			|| insufficient(fee, balance)) {
			return SendErrorText(
				SendError::InsufficientFees,
				TransferMinNanos(session));
		} else if (error == SendError::SigningUnavailable
			|| error == SendError::QuoteExpired
			|| error == SendError::None) {
			return QString();
		}
		return SendErrorText(error, TransferMinNanos(session));
	};
	const auto refusalNow = [=] {
		return refusalText(
			state->error.current(),
			state->fee.current(),
			wallet->balanceNano());
	};

	rpl::combine(
		state->fee.value(),
		state->estimating.value(),
		state->error.value()
	) | rpl::map([](
			std::optional<int64> fee,
			bool estimating,
			SendError error) {
		const auto loading = estimating
			|| (error == SendError::None)
			|| (error == SendError::SigningUnavailable);
		const auto shown = fee
			? DetailsFee::Known
			: loading
			? DetailsFee::Loading
			: DetailsFee::Failed;
		return ShownFee(fee, shown);
	}) | rpl::distinct_until_changed() | rpl::on_next([=](ShownFee shown) {
		state->table = shown;
		if (state->tableQueued) {
			return;
		}
		state->tableQueued = true;
		Ui::PostponeCall(box, [=] {
			state->tableQueued = false;
			if (state->built == state->table) {
				return;
			}
			state->built = state->table;
			auto counted = item;
			counted.feeNano = state->table.first;
			const auto top = box->scrollTop();
			const auto atBottom = (top + box->scrollHeight()
				>= box->verticalLayout()->height());
			details->clear();
			AddDetailsTable(box, details, show, counted, state->table.second);
			box->scrollToY(atBottom ? ScrollMax : top);
		});
	}, details->lifetime());

	const auto notes = AddSendConfirmNotes(
		box,
		state->comment.value() | rpl::map([](const QString &text) {
			return !text.isEmpty();
		}),
		rpl::combine(
			state->error.value(),
			state->fee.value(),
			wallet->balanceNanoValue(),
			rpl::single(rpl::empty) | rpl::then(Lang::Updated())
		) | rpl::map([=](
				SendError error,
				std::optional<int64> fee,
				int64 balance,
				rpl::empty_value) {
			return refusalText(error, fee, balance);
		}));
	rpl::combine(
		notes.caption->heightValue(),
		notes.refusal->heightValue()
	) | rpl::map([](int captionHeight, int refusalHeight) {
		return captionHeight + refusalHeight;
	}) | rpl::combine_previous(
	) | rpl::filter([](int was, int now) {
		return (now > was);
	}) | rpl::to_empty | rpl::on_next([=] {
		box->scrollToY(ScrollMax);
	}, box->lifetime());

	const auto pressable = [=] {
		return !state->sending.current()
			&& !state->estimating.current()
			&& refusalNow().isEmpty()
			&& (state->prepared
				|| state->error.current() == SendError::SigningUnavailable);
	};
	const auto press = [=] {
		if (!pressable()) {
			return;
		}
		state->sending = true;
		state->refusals = 0;
		state->continueSend();
	};
	const auto button = box->addButton(
		BusyFooterLabel(
			tr::lng_wallet_collectible_send(
				lt_name,
				CollectibleNameValue(media, address)),
			state->sending.value()),
		press).data();
	rpl::combine(
		state->sending.value(),
		state->estimating.value(),
		state->error.value(),
		state->fee.value(),
		wallet->balanceNanoValue()
	) | rpl::to_empty | rpl::on_next([=] {
		SetButtonDisabledLook(
			button,
			!state->sending.current() && !pressable());
	}, button->lifetime());
	AddBusyFooterSpinner(button, state->sending.value());

	field->changes() | rpl::on_next([=] {
		state->comment = field->getLastText();
		if (!state->sending.current()) {
			state->estimate();
		}
	}, field->lifetime());
	field->submits() | rpl::on_next(press, field->lifetime());
	state->sending.value() | rpl::on_next([=](bool sending) {
		field->setDisabled(sending);
	}, field->lifetime());
	box->setFocusCallback([=] { field->setFocusFast(); });

	const auto stop = [=] {
		state->authorization = {};
		state->signingWait.cancel();
		state->signingLifetime.destroy();
		state->sending = false;
	};
	const auto acquireKey = [=] {
		state->keyLifetime.destroy();
		state->unlocking = true;
		AcquireWalletKey(
			show,
			current,
			state->keyLifetime,
			crl::guard(box, [=](KeyAuthorization auth) {
				state->unlocking = false;
				if (!state->sending.current() || state->submitted) {
					return;
				} else if (!auth.valid()) {
					stop();
					return;
				}
				state->authorization = std::move(auth);
				state->continueSend();
			}),
			tr::lng_wallet_restore_text());
	};
	const auto awaitSigning = [=] {
		if (state->signingWait.isActive()) {
			return;
		}
		state->signingLifetime.destroy();
		wallet->signingReadyValue(
		) | rpl::filter([](bool ready) {
			return ready;
		}) | rpl::take(1) | rpl::on_next([=] {
			state->continueSend();
		}, state->signingLifetime);
		state->signingWait.callOnce(kSigningReadyTimeout);
	};
	state->signingWait.setCallback([=] {
		stop();
		state->error = SendError::Failed;
	});
	const auto started = [=](SendStarted value) {
		state->handedOver = true;
		const auto panel = wallet->panel();
		if (panel && box->window() == panel->window()) {
			wallet->setWindowSend(value.operationId);
		}
		show->hideLayer();
	};
	const auto sent = [=](SendError error) {
		if (error == SendError::KeyChanged) {
			if (weak && !state->handedOver) {
				weak->closeBox();
			}
			if (sessionValid()) {
				ShowWalletKeyChanged(show);
			}
			return;
		} else if (!current() || state->handedOver) {
			return;
		} else if (error == SendError::None
			|| error == SendError::SubmissionUnknown) {
			show->hideLayer();
			return;
		}
		state->submitted = false;
		const auto retry = (error == SendError::QuoteExpired)
			|| (error == SendError::SigningUnavailable);
		if (retry && ++state->refusals <= kSendRefusalRetries) {
			state->estimate();
			return;
		}
		stop();
		state->error = (error == SendError::QuoteExpired)
			? SendError::Failed
			: error;
	};
	state->continueSend = [=] {
		if (!state->sending.current()
			|| state->submitted
			|| state->unlocking) {
			return;
		} else if (!current()) {
			stop();
			return;
		} else if (state->estimating.current()) {
			return;
		} else if (!state->authorization.valid()) {
			acquireKey();
			return;
		} else if (!wallet->signingReady()) {
			awaitSigning();
			return;
		} else if (!state->prepared) {
			const auto error = state->error.current();
			const auto retry = (error == SendError::None)
				|| (error == SendError::SigningUnavailable);
			if (retry && ++state->refusals <= kSendRefusalRetries) {
				state->estimate();
				return;
			}
			stop();
			if (retry) {
				state->error = SendError::Failed;
			}
			return;
		} else if (!refusalNow().isEmpty()) {
			stop();
			return;
		}
		state->submitted = true;
		wallet->send(
			state->authorization,
			base::take(state->prepared),
			crl::guard(session, sent),
			crl::guard(session, crl::guard(box, started)));
	};

	state->estimate();
}

} // namespace ContentDetails

} // namespace Wallet
