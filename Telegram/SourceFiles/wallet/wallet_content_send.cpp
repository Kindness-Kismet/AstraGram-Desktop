#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

void WalletSendBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::optional<SendFlow> initial,
		UserData *user,
		Fn<void()> sent,
		Fn<void()> notReady,
		int64 amountNano,
		base::weak_qptr<Ui::BoxContent> origin) {
	Expects(user || initial);

	// The box this one was opened over comes back unless the user leaves.
	const auto discardOrigin = [=] {
		if (const auto strong = origin.get()) {
			strong->closeBox();
		}
	};
	const auto self = (!user
			&& SendsToOwnWallet(&show->session(), initial->destination))
		? show->session().user().get()
		: nullptr;
	const auto recipient = user ? user : self;

	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);

	const auto weak = base::make_weak(box.get());
	const auto openProfile = recipient
		? Fn<void()>([=] {
			const auto window = MakeChatShow(show, true)->resolveWindow();
			if (!window) {
				return;
			}
			const auto peer = recipient;
			if (weak
				&& weak->hasDelegate()
				&& window->widget()->window() == weak->window()) {
				discardOrigin();
				weak->closeBox();
			}
			window->showPeerInfo(peer);
			window->window().activate();
		})
		: nullptr;
	if (recipient) {
		// WHY: the box title label is private inside lib_ui and takes no
		// click filter, so the name is clickable only while this file owns
		// the label itself.
		const auto wrap = box->setPinnedToTopContent(
			object_ptr<Ui::FixedHeightWidget>(
				box,
				st::boxTitleHeight - st::boxTopMargin));
		const auto title = Ui::CreateChild<Ui::FlatLabel>(
			wrap,
			tr::lng_wallet_send_user_title(
				lt_user,
				Info::Profile::NameValue(recipient) | rpl::map([](QString name) {
					return tr::link(name);
				}),
				tr::marked),
			st::boxTitle);
		title->setClickHandlerFilter([=](const auto &...) {
			const auto onstack = openProfile;
			onstack();
			return false;
		});
		rpl::combine(
			wrap->widthValue(),
			title->naturalWidthValue()
		) | rpl::on_next([=](int width, int) {
			const auto buttons = st::boxTitleClose.width
				+ st::boxTitleMenu.width;
			title->resizeToNaturalWidth(
				width - 2 * st::boxTitlePosition.x() - buttons);
			title->moveToLeft(
				st::boxTitlePosition.x(),
				st::boxTitlePosition.y() - st::boxTopMargin,
				width);
		}, title->lifetime());
	} else {
		box->setTitle(initial->tonName.isEmpty()
			? SendRecipientTitle(box, initial->displayForm, Qt::ElideMiddle)
			: SendRecipientTitle(box, initial->tonName, Qt::ElideRight));
	}
	AddBoxCloseButton(box);

	const auto session = &show->session();
	const auto weakSession = base::make_weak(session);
	const auto wallet = &session->wallet();

	struct State {
		std::optional<SendFlow> flow;
		std::optional<SendQuoteDependencies> previewDependencies;
		base::unique_qptr<Ui::PopupMenu> menu;
		base::weak_qptr<Ui::GenericBox> commentBox;
		base::weak_qptr<Ui::GenericBox> confirmBox;
		std::optional<TransferWalletIdentity> senderIdentity;
		QByteArray recipientKey;
		uint64 previewRevision = 0;
		uint64 loadRevision = 0;
		base::Timer loadDeadline;
		rpl::lifetime previewLifetime;
		bool forceIssued = false;
		bool terminal = false;
		bool recomputeQueued = false;
		bool recipientRequested = false;
		bool feeRefreshQueued = false;
		rpl::variable<bool> loading = false;
		rpl::variable<QString> loadError;
		rpl::variable<bool> silentFailure = false;
		rpl::variable<int64> amount = 0;
		rpl::variable<int64> fee = 0;
		rpl::variable<int64> minTransfer = kTransferMinNanosDefault;
		std::shared_ptr<SendDraft> draft = std::make_shared<SendDraft>();
		rpl::variable<bool> sending = false;
		std::optional<SendQuoteDependencies> sendRequest;
		std::optional<uint64> sendExpiresAt;
		std::optional<SendConfirmFee> heldFee;
		KeyAuthorization sendAuthorization;
		KeyAuthorization heldAuthorization;
		base::Timer heldTimeout;
		int sendRefusals = 0;
		bool unlocking = false;
		bool submitted = false;
		bool handedOver = false;
		Fn<void()> continueSend;
		rpl::variable<bool> previewInsufficient = false;
		rpl::variable<SendError> previewError = SendError::None;
		rpl::variable<bool> insufficient = false;
		rpl::variable<bool> unfunded = false;
		rpl::variable<bool> canSend = false;
		rpl::variable<bool> raisable = false;
		std::shared_ptr<KeyContext> keyContext;
		base::Timer signingWait;
		bool signingTimedOut = false;
		rpl::variable<FiatRate> rate;
		rpl::variable<bool> entryFiat = false;
		QString previousCurrency;
		bool settingUnitText = false;
		bool closed = false;
		Fn<void()> swapUnit;
	};
	const auto state = box->lifetime().make_state<State>();
	if (initial) {
		state->draft = initial->draft;
	}
	const auto draft = state->draft;
	const auto previewOwner = wallet->createPreviewOwner(state->previewLifetime);
	state->senderIdentity = user
		? std::nullopt
		: wallet->transferWalletIdentity();
	state->loading = !state->senderIdentity;
	state->rate = FiatRateValue(session);
	state->minTransfer = TransferMinNanos(session);
	const auto userId = user ? peerToUser(user->id) : UserId();
	const auto sessionValid = [=] {
		return weakSession
			&& show->valid()
			&& (&show->session() == session);
	};
	const auto userError = [=] {
		if (!sessionValid()
			|| (state->senderIdentity
				&& !wallet->transferWalletIdentityCurrent(
					*state->senderIdentity))) {
			return u"WALLET_NOT_READY"_q;
		}
		const auto error = wallet->userAddresses().forceResolveError(userId);
		if (!error.isEmpty()) {
			return error;
		} else if (session->data().userLoaded(userId) != user) {
			return u"WALLET_USER_INVALID"_q;
		} else if (state->flow
			&& (!user->gramAddress()
				|| *user->gramAddress() != state->flow->destination)) {
			return u"WALLET_ADDRESS_INVALID"_q;
		}
		return QString();
	};
	const auto originValid = [=] {
		return weak
			&& !state->closed
			&& !state->terminal
			&& sessionValid()
			&& state->senderIdentity
			&& wallet->transferWalletIdentityCurrent(*state->senderIdentity)
			&& !state->loading.current()
			&& state->loadError.current().isEmpty()
			&& (!user || (state->flow && userError().isEmpty()));
	};
	const auto receive = [=] {
		if (originValid()
			|| (state->unfunded.current()
				&& !state->closed
				&& !state->terminal
				&& sessionValid())) {
			ShowWalletReceiveBox(session, box->uiShow());
		}
	};
	const auto editComment = [=] {
		if (!originValid()
			|| state->commentBox
			|| state->sending.current()) {
			return;
		}
		auto editor = Box(
			WalletSendCommentBox,
			draft,
			originValid,
			draft->encryptable.value());
		const auto raw = editor.data();
		state->commentBox = base::make_weak(raw);
		raw->boxClosing() | rpl::on_next([=] {
			if (weak && state->commentBox.get() == raw) {
				state->commentBox = nullptr;
			}
		}, raw->lifetime());
		box->uiShow()->showBox(std::move(editor));
	};
	const auto toggle = box->addTopButton(st::boxTitleMenu);
	toggle->setClickedCallback([=] {
		if (!originValid() || state->menu) {
			return;
		}
		state->menu = base::make_unique_q<Ui::PopupMenu>(
			box,
			st::popupMenuWithIcons);
		const auto raw = state->menu.get();
		raw->setDestroyedCallback(crl::guard(toggle, [=] {
			toggle->setForceRippled(false);
		}));
		toggle->setForceRippled(true);
		raw->addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_add_funds(tr::now)),
			receive,
			&st::menuIconAdd);
		raw->addAction(
			Ui::Text::FixAmpersandInAction(
				tr::lng_wallet_comment_title(tr::now)),
			editComment,
			&st::menuIconChatBubble);
		raw->setForcedOrigin(Ui::PanelAnimation::Origin::TopRight);
		const auto scope = WindowPaletteScope(box);
		raw->popup(toggle->mapToGlobal(QPoint(
			toggle->width(),
			toggle->height())));
	});
	const auto quoteDependencies = [=] {
		const auto validSession = sessionValid();
		return SendQuoteDependencies{
			.senderIdentity = validSession
				? wallet->transferWalletIdentity()
				: std::nullopt,
			.gaslessTerms = validSession
				? wallet->gaslessTerms()
				: GaslessTerms(),
			.destination = state->flow ? state->flow->destination : QString(),
			.comment = draft->comment.current(),
			.custody = validSession
				? wallet->deviceCustodyState()
				: DeviceCustodyState(),
			.recipientPublicKey = state->recipientKey,
			.userId = userId,
			.amountNano = state->amount.current(),
			.balanceNano = validSession ? wallet->balanceNano() : 0,
			.minTransferNano = state->minTransfer.current(),
			.bounce = state->flow && state->flow->bounce,
			.ready = validSession
				&& wallet->presenceCurrent() == Presence::Ready,
			.valid = originValid(),
		};
	};
	const auto stopSending = [=] {
		state->sending = false;
		state->sendRequest.reset();
		state->sendExpiresAt.reset();
		state->heldFee.reset();
		state->sendAuthorization = {};
		state->sendRefusals = 0;
		state->submitted = false;
		state->signingWait.cancel();
		state->signingTimedOut = false;
	};
	const auto scheduleContinueSend = [=] {
		Ui::PostponeCall(box, [=] { state->continueSend(); });
	};
	// The signing client is awaited for a bounded time only. A journal
	// recovery that keeps failing would otherwise hold the press forever,
	// while letting the press through after the bound has the session
	// refuse it typed, the way it did before the wait existed.
	const auto awaitSigning = [=] {
		if (!state->signingWait.isActive()) {
			state->signingWait.callOnce(kSigningReadyTimeout);
		}
	};
	state->signingWait.setCallback([=] {
		state->signingTimedOut = true;
		scheduleContinueSend();
	});
	const auto dropHeldKey = [=] {
		state->heldAuthorization = {};
		state->heldTimeout.cancel();
	};
	state->heldTimeout.setCallback(dropHeldKey);
	const auto failLoading = [=](const QString &error, bool silent = false) {
		if (state->closed || state->terminal) {
			return;
		}
		state->terminal = true;
		state->silentFailure = silent;
		stopSending();
		++state->loadRevision;
		state->loadDeadline.cancel();
		++state->previewRevision;
		state->previewLifetime.destroy();
		draft->preparing = false;
		draft->quote = std::nullopt;
		draft->authorization = {};
		draft->privateEpoch.reset();
		state->flow = std::nullopt;
		state->loadError = error.isEmpty() ? u"WALLET_ADDRESS_INVALID"_q : error;
		state->loading = false;
		if (!silent) {
			show->showToast(SendUserLoadErrorText(state->loadError.current()));
		}
		box->closeBox();
	};
	box->boxClosing() | rpl::on_next([=] {
		state->closed = true;
		if (const auto confirm = base::take(state->confirmBox)) {
			if (confirm->hasDelegate()) {
				confirm->closeBox();
			}
		}
		if (const auto context = base::take(state->keyContext)) {
			context->cancel();
		}
		++state->loadRevision;
		state->loadDeadline.cancel();
		++state->previewRevision;
		state->previewLifetime.destroy();
		draft->preparing = false;
		draft->quote = std::nullopt;
		draft->authorization = {};
		draft->privateEpoch.reset();
		state->sendAuthorization = {};
		dropHeldKey();
		state->flow.reset();
	}, box->lifetime());

	const auto entrySeparator = [=] {
		if (!state->entryFiat.current()) {
			return Ui::TonAmountSeparator();
		}
		const auto rule = Ui::LookupCurrencyRule(
			state->rate.current().currency);
		return QString(QChar(rule.decimal));
	};

	const auto inner = box->verticalLayout();
	auto address = user
		? rpl::producer<QString>(state->loading.value(
		) | rpl::map([=](bool loading) {
			return (!loading && state->flow)
				? state->flow->displayForm
				: QString();
		}))
		: rpl::producer<QString>(rpl::single(initial->displayForm));
	inner->add(
		object_ptr<SendRecipientCard>(
			inner,
			box->uiShow(),
			recipient,
			std::move(address),
			(recipient
				? Fn<void()>([=] {
					if (!state->flow) {
						return;
					}
					ShowSendRecipientWallet(
						box->uiShow(),
						recipient,
						state->flow->displayForm,
						openProfile);
				})
				: nullptr)),
		style::margins(
			st::boxRowPadding.left(),
			st::walletSendUserCardTopSkip,
			st::boxRowPadding.right(),
			0),
		style::al_justify);

	auto helper = Ui::Text::CustomEmojiHelper();
	const auto gramMark = GramMark(
		helper,
		st::walletSendUserFiatButton.style.font);
	auto equivalent = rpl::combine(
		state->amount.value(),
		state->entryFiat.value(),
		state->rate.value()
	) | rpl::map([=](int64 amount, bool fiat, const FiatRate &rate) {
		if (fiat) {
			const auto formatted = Ui::FormatTonAmount(amount);
			return AmountLabel{
				.prefix = TextWithEntities(gramMark).append(u" "_q),
				.amount = formatted.full,
				.decimal = formatted.separator,
			};
		}
		const auto rule = Ui::LookupCurrencyRule(rate.currency);
		return AmountLabel{
			.prefix = tr::marked(QChar('~')),
			.amount = FormatFiatAmount(amount, rate),
			.decimal = QString(QChar(rule.decimal)),
			.suffix = u" "_q + rate.currency,
			.unit = rate.currency,
		};
	});
	const auto amountField = AddAmountField(
		inner,
		st::walletSendUserCardAmountSkip,
		{
			.value = std::min(
				initial ? initial->amountNano : amountNano,
				kMaxAmountNano),
			.fractionDigits = [=] {
				return state->entryFiat.current()
					? Ui::LookupCurrencyRule(
						state->rate.current().currency).exponent
					: 9;
			},
			.separator = entrySeparator,
			.entryFiat = state->entryFiat.value(),
			.currency = state->rate.value(
			) | rpl::map([](const FiatRate &rate) {
				return rate.currency;
			}) | rpl::distinct_until_changed(),
			.equivalent = std::move(equivalent),
			.equivalentContext = helper.context(),
			.swap = [=] { state->swapUnit(); },
			.equivalentShown = rpl::combine(
				state->loading.value(),
				state->loadError.value()
			) | rpl::map([](bool loading, const QString &error) {
				return !loading && error.isEmpty();
			}),
		});
	const auto comment = inner->add(
		object_ptr<Ui::SlideWrap<SendCommentBubble>>(
			inner,
			object_ptr<SendCommentBubble>(
				inner,
				draft->comment.value(),
				rpl::combine(
					state->sending.value(),
					state->loading.value(),
					state->loadError.value()
				) | rpl::map([=](bool sending, bool, const QString &) {
					return !sending && originValid();
				}),
				editComment)),
		style::margins(),
		style::al_justify);
	comment->toggleOn(draft->comment.value() | rpl::map([](
			const SendComment &value) {
		return !value.text.isEmpty();
	}));
	comment->finishAnimating();
	const auto publicWarning = inner->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			inner,
			object_ptr<Ui::FlatLabel>(
				inner,
				tr::lng_wallet_comment_public(),
				st::walletSendUserBalanceLabel),
			style::margins(
				st::walletSendFieldMargin.left(),
				0,
				st::walletSendFieldMargin.right(),
				st::walletSendFieldMargin.bottom())),
		style::margins(),
		style::al_justify);
	publicWarning->entity()->setTryMakeSimilarLines(true);
	publicWarning->toggleOn(draft->comment.value() | rpl::map([](
			const SendComment &value) {
		return value.isPublic && !value.text.isEmpty();
	}));
	publicWarning->finishAnimating();

	const auto updateAmount = [=] {
		// The confirmation on top shows this amount, so a rate waits for it.
		if (state->settingUnitText || state->confirmBox) {
			return;
		}
		const auto parsed = Ui::ParseTonAmountString(
			amountField->getLastText(),
			entrySeparator()).value_or(0);
		const auto rate = state->rate.current();
		state->amount = !state->entryFiat.current()
			? parsed
			: rate.available()
			? std::min(
				int64(std::clamp(
					base::SafeRound(double(parsed) / rate.perGram),
					0.,
					double(kMaxAmountNano))),
				kMaxAmountNano)
			: 0;
	};
	amountField->changes(
	) | rpl::on_next(updateAmount, amountField->lifetime());
	updateAmount();

	const auto fiatUnitsText = [=](int64 units, int64 quantum) {
		const auto formatted = Ui::FormatTonAmount(
			units * quantum,
			Ui::TonFormatFlag::Simple);
		auto result = formatted.wholeString;
		if (!formatted.nanoString.isEmpty()) {
			result += entrySeparator() + formatted.nanoString;
		}
		return result;
	};
	const auto renderUnitText = [=] {
		const auto amount = state->amount.current();
		if (!state->entryFiat.current()) {
			return amount
				? Ui::FormatTonAmount(
					amount,
					Ui::TonFormatFlag::Simple).full
				: QString();
		}
		const auto rate = state->rate.current();
		const auto quantum = FiatMinorUnitNanos(rate.currency);
		const auto maxUnits = kMaxFiatUnits * (Ui::kNanosInOne / quantum);
		const auto units = int64(std::min(
			base::SafeRound(amount * rate.perGram / double(quantum)),
			double(maxUnits)));
		if (!units) {
			return QString();
		}
		return fiatUnitsText(units, quantum);
	};
	const auto setUnitText = [=] {
		state->settingUnitText = true;
		Ui::PostponeCall(amountField, [=] {
			state->settingUnitText = false;
		});
		amountField->setText(renderUnitText());
		amountField->setFocusFast();
	};
	const auto switchEntryUnit = [=](bool fiat) {
		if (state->entryFiat.current() == fiat
			|| (fiat && !state->rate.current().available())) {
			return;
		}
		state->entryFiat = fiat;
		setUnitText();
	};
	const auto raiseToMinimum = [=] {
		const auto minimum = state->minTransfer.current();
		if (!state->entryFiat.current()) {
			amountField->setText(Ui::FormatTonAmount(
				minimum,
				Ui::TonFormatFlag::Simple).full);
		} else {
			const auto rate = state->rate.current();
			if (!rate.available()) {
				return;
			}
			const auto quantum = FiatMinorUnitNanos(rate.currency);
			const auto maxUnits = kMaxFiatUnits * (Ui::kNanosInOne / quantum);
			const auto units = int64(std::min(
				std::ceil(double(minimum) * rate.perGram / double(quantum)),
				double(maxUnits)));
			amountField->setText(fiatUnitsText(units, quantum));
		}
		amountField->setFocusFast();
	};
	state->swapUnit = [=] { switchEntryUnit(!state->entryFiat.current()); };
	state->previousCurrency = state->rate.current().currency;
	state->rate.value() | rpl::on_next([=](const FiatRate &now) {
		const auto currencyChanged
			= (now.currency != state->previousCurrency);
		state->previousCurrency = now.currency;
		if (!now.available()) {
			switchEntryUnit(false);
		} else if (state->entryFiat.current()) {
			if (currencyChanged) {
				setUnitText();
			} else {
				updateAmount();
			}
		}
	}, box->lifetime());

	const auto invalidateFee = [=] {
		if (!weak || state->closed || state->terminal) {
			return;
		}
		++state->previewRevision;
		draft->preparing = false;
		draft->quote = std::nullopt;
		draft->authorization = {};
		draft->privateEpoch.reset();
		state->previewInsufficient = false;
		state->previewError = SendError::None;
		if (weakSession) {
			wallet->cancelFeeEstimate(previewOwner);
		}
	};
	// A comment encrypts for a key Telegram named for the destination, or for
	// the one the recipient's `get_public_key` answers. A wallet that was
	// never deployed answers neither unless it belongs to a Telegram user, so
	// when the engine refuses to encrypt for a destination, the box stops
	// offering encryption for it and the comment becomes a public one. The
	// send in flight stops there: a comment written to be private is never
	// published by the press that was meant to encrypt it.
	const auto switchToPlain = [=] {
		if (!state->flow) {
			return;
		}
		draft->encryptable = false;
		auto comment = draft->comment.current();
		if (!comment.isPublic) {
			comment.isPublic = true;
			draft->comment = std::move(comment);
		}
	};
	const auto prepareFee = [=](KeyAuthorization authorization) {
		if (!originValid() || draft->preparing.current()) {
			return;
		}
		invalidateFee();
		const auto dependencies = quoteDependencies();
		state->previewDependencies = dependencies;
		const auto revision = state->previewRevision;
		// A key just acquired for this press is followed by the client swap
		// to the signing one, and an estimate refused in that window is not
		// the press failing: the press waits for the signing client and
		// estimates again under the same authorization; a hidden refusal
		// during a press is the press's to state, never a silent stop.
		const auto fail = [=](SendError error) {
			if (revision != state->previewRevision) {
				return;
			}
			const auto swapping = (error == SendError::SigningUnavailable)
				&& state->sendAuthorization.valid()
				&& !wallet->signingReady()
				&& !state->signingTimedOut;
			const auto handOff = !swapping
				&& (error == SendError::SigningUnavailable)
				&& state->sending.current()
				&& !state->submitted;
			if (swapping) {
				awaitSigning();
			} else if (!state->submitted && !handOff) {
				stopSending();
			}
			invalidateFee();
			state->previewError = error;
			if (handOff) {
				scheduleContinueSend();
			}
		};
		const auto drifted = [=] {
			if (revision != state->previewRevision) {
				return;
			}
			invalidateFee();
			state->previewDependencies.reset();
			if (state->sending.current() && !state->submitted) {
				scheduleContinueSend();
			}
		};
		if (!CommentFits(dependencies.comment.text)) {
			fail(SendError::CommentTooLong);
			return;
		} else if (!dependencies.ready || dependencies.amountNano <= 0) {
			fail(SendError::InvalidRequest);
			return;
		} else if (TransferAmountBelowMinimum(
				dependencies.amountNano,
				dependencies.minTransferNano)) {
			fail(SendError::AmountTooSmall);
			return;
		} else if (dependencies.amountNano > dependencies.balanceNano) {
			fail(SendError::InsufficientBalance);
			return;
		}
		const auto args = SendArgs{
			.destination = dependencies.destination,
			.amountNano = dependencies.amountNano,
			.userId = dependencies.userId,
			.comment = dependencies.comment,
			.recipientPublicKey = dependencies.recipientPublicKey,
			.bounce = dependencies.bounce,
		};
		const auto isPrivate = !args.comment.text.isEmpty()
			&& !args.comment.isPublic;
		const auto keyed = isPrivate && authorization.valid();
		const auto privateEpoch = keyed
			? std::make_optional(wallet->vault().clearEpoch())
			: std::nullopt;
		draft->preparing = true;
		const auto current = [=] {
			if (!originValid() || revision != state->previewRevision) {
				return false;
			}
			const auto now = quoteDependencies();
			return revision == state->previewRevision && dependencies == now;
		};
		const auto estimate = crl::guard(session, crl::guard(box, [=](
				KeyAuthorization auth) {
			if (!current()) {
				drifted();
				return;
			} else if (privateEpoch
				&& *privateEpoch != wallet->vault().clearEpoch()) {
				fail(SendError::Locked);
				return;
			}
			draft->authorization = auth;
			draft->privateEpoch = privateEpoch;
			wallet->estimateFee(
				std::move(auth),
				previewOwner,
				args,
				crl::guard(session, crl::guard(box, [=](FeeResult result) {
					if (!current()) {
						drifted();
						return;
					} else if (privateEpoch
						&& (*privateEpoch != wallet->vault().clearEpoch()
							|| !draft->authorization.valid()
							|| !wallet->vault().unlocked())) {
						fail(SendError::Locked);
						return;
					}
					switch (result.error) {
					case SendError::None:
						if (!result.prepared && (keyed || !isPrivate)) {
							fail(SendError::Failed);
							return;
						}
						draft->quote = SendQuote{
							.args = args,
							.dependencies = dependencies,
							.prepared = std::move(result.prepared),
							.feeNano = result.feeNano,
							.revision = revision,
						};
						draft->preparing = false;
						if (state->sending.current() && !state->submitted) {
							scheduleContinueSend();
						}
						return;
					case SendError::InsufficientBalance:
					case SendError::InsufficientFees:
						fail(result.error);
						state->previewInsufficient = true;
						return;
					case SendError::QuoteExpired:
						drifted();
						return;
					case SendError::CommentEncryptionUnavailable:
						if (!state->submitted) {
							const auto held = state->sendAuthorization;
							stopSending();
							state->heldAuthorization = held;
							state->heldTimeout.callOnce(kHeldSendKeyTimeout);
						}
						invalidateFee();
						switchToPlain();
						return;
					case SendError::AmountTooSmall:
					case SendError::CommentTooLong:
					case SendError::InvalidRequest:
					case SendError::PreviousUnresolved:
					case SendError::AlreadySending:
					case SendError::SigningUnavailable:
					case SendError::Locked:
					case SendError::Failed:
					case SendError::Rejected:
					case SendError::DataInvalid:
					case SendError::KeyMismatch:
					case SendError::KeyChanged:
					case SendError::CollectibleUnavailable:
					case SendError::CollectibleRejected:
					case SendError::Silent:
					case SendError::SubmissionUnknown:
						fail(result.error);
						return;
					}
					Unexpected("Error value in the send box fee estimate.");
				})));
		}));
		estimate(keyed ? std::move(authorization) : KeyAuthorization());
	};
	const auto refreshFee = [=] {
		if (state->closed || state->terminal) {
			return;
		} else if (state->unfunded.current()) {
			state->previewDependencies.reset();
			invalidateFee();
			return;
		}
		const auto dependencies = quoteDependencies();
		if (state->previewDependencies == dependencies) {
			return;
		}
		// WHY: an estimate prices the whole input, so a change made while
		// one is counting waits for it and is counted once, instead of a
		// count queued for every letter typed into the comment.
		if (draft->preparing.current() && !state->sending.current()) {
			state->feeRefreshQueued = true;
			return;
		}
		state->feeRefreshQueued = false;
		state->previewDependencies = dependencies;
		invalidateFee();
		if (!CommentFits(dependencies.comment.text)) {
			state->previewError = SendError::CommentTooLong;
		} else if (TransferAmountBelowMinimum(
				dependencies.amountNano,
				dependencies.minTransferNano)) {
			state->previewError = SendError::AmountTooSmall;
		} else if (!state->sending.current() && dependencies.amountNano > 0) {
			prepareFee({});
		}
		if (state->sending.current() && !state->submitted) {
			scheduleContinueSend();
		}
	};
	// The last counted fee stands while the next one is counted.
	draft->quote.value() | rpl::on_next([=](
			const std::optional<SendQuote> &quote) {
		if (quote) {
			state->fee = quote->feeNano;
		}
	}, box->lifetime());
	draft->preparing.changes() | rpl::on_next([=](bool preparing) {
		if (preparing || !base::take(state->feeRefreshQueued)) {
			return;
		}
		Ui::PostponeCall(box, refreshFee);
	}, box->lifetime());
	state->amount.value() | rpl::on_next(refreshFee, box->lifetime());
	draft->comment.changes() | rpl::on_next(refreshFee, box->lifetime());
	state->loading.changes() | rpl::on_next(refreshFee, box->lifetime());
	state->unfunded.changes() | rpl::on_next(refreshFee, box->lifetime());
	state->loadError.changes() | rpl::on_next(refreshFee, box->lifetime());
	session->appConfig().refreshed() | rpl::on_next([=] {
		state->minTransfer = TransferMinNanos(session);
	}, box->lifetime());
	state->minTransfer.changes() | rpl::on_next(refreshFee, box->lifetime());
	wallet->gaslessTermsValue() | rpl::on_next(refreshFee, box->lifetime());
	wallet->transferWalletIdentityChanges() | rpl::on_next([=] {
		if (!state->senderIdentity && !user && sessionValid()) {
			state->senderIdentity = wallet->transferWalletIdentity();
		}
		refreshFee();
	}, box->lifetime());
	wallet->signingReadyValue() | rpl::skip(1) | rpl::on_next([=](bool ready) {
		if (ready) {
			state->signingWait.cancel();
			state->signingTimedOut = false;
		}
		state->previewDependencies.reset();
		refreshFee();
	}, box->lifetime());
	if (!user) {
		wallet->custodyUpdates() | rpl::on_next(refreshFee, box->lifetime());
		wallet->balanceNanoValue() | rpl::on_next(refreshFee, box->lifetime());
		wallet->stateKnownValue() | rpl::on_next(refreshFee, box->lifetime());
		wallet->presenceValue() | rpl::on_next(refreshFee, box->lifetime());
	}
	state->insufficient = rpl::combine(
		state->amount.value(),
		state->fee.value(),
		state->previewInsufficient.value(),
		wallet->balanceNanoValue(),
		wallet->stateKnownValue(),
		wallet->gaslessTermsValue(),
		state->unfunded.value()
	) | rpl::map([=](
			int64 amount,
			int64 fee,
			bool preview,
			int64 balance,
			bool known,
			const GaslessTerms &terms,
			bool unfunded) {
		// A fee-free transfer keeps nothing back for the fee, so the whole
		// balance is sendable when the offer covers this one.
		const auto destination = state->flow
			? state->flow->destination
			: QString();
		const auto reserve = terms.eligible(amount, destination) ? 0 : fee;
		return unfunded
			|| ((amount > 0)
				&& (preview || (known && amount > balance - reserve)));
	});
	state->canSend = rpl::combine(
		state->amount.value(),
		state->insufficient.value(),
		wallet->stateKnownValue(),
		state->previewError.value(),
		state->loading.value(),
		state->loadError.value()
	) | rpl::map([](
			int64 amount,
			bool insufficient,
			bool known,
			SendError error,
			bool loading,
			const QString &loadError) {
		return known
			&& (amount > 0)
			&& !insufficient
			&& !loading
			&& loadError.isEmpty()
			&& (error != SendError::AmountTooSmall)
			&& (error != SendError::CommentTooLong)
			&& (error != SendError::AlreadySending)
			&& (error != SendError::PreviousUnresolved);
	});
	state->raisable = rpl::combine(
		state->amount.value(),
		state->minTransfer.value(),
		state->loading.value(),
		state->loadError.value(),
		draft->comment.value(),
		state->unfunded.value()
	) | rpl::map([](
			int64 amount,
			int64 minimum,
			bool loading,
			const QString &loadError,
			const SendComment &comment,
			bool unfunded) {
		return TransferAmountBelowMinimum(amount, minimum)
			&& !loading
			&& !unfunded
			&& loadError.isEmpty()
			&& CommentFits(comment.text);
	});

	const auto balance = inner->add(
		object_ptr<Ui::VerticalLayout>(inner),
		style::margins(
			st::walletSendFieldMargin.left(),
			0,
			st::walletSendFieldMargin.right(),
			st::walletDetailsAmountBottomSkip),
		style::al_justify);
	const auto balanceWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			balance,
			object_ptr<Ui::FlatLabel>(
				balance,
				rpl::combine(
					tr::lng_wallet_send_balance(
						lt_amount,
						wallet->balanceNanoValue() | rpl::map([](int64 nano) {
							return Ui::FormatTonAmount(nano).full;
						})),
					state->loading.value(),
					state->loadError.value()
				) | rpl::map([](QString text, bool loading, const QString &error) {
					return (loading || !error.isEmpty())
						? QString(QChar(0xA0))
						: text;
				}),
				st::walletSendUserBalanceLabel)),
		style::al_justify);
	balanceWrap->toggleOn(rpl::combine(
		state->insufficient.value(),
		state->previewError.value(),
		state->loading.value(),
		state->loadError.value()
	) | rpl::map([](
			bool insufficient,
			SendError error,
			bool loading,
			const QString &loadError) {
		return loading || (!insufficient
			&& (error == SendError::None)
			&& loadError.isEmpty());
	}));
	balanceWrap->finishAnimating();
	const auto feeWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			balance,
			object_ptr<Ui::FlatLabel>(
				balance,
				tr::lng_wallet_send_network_fee(
					lt_amount,
					state->fee.value() | rpl::map([](int64 nano) {
						return Ui::FormatTonAmount(nano).full;
					})),
				st::walletSendUserBalanceLabel)),
		style::al_justify);
	feeWrap->toggleOn(rpl::combine(
		state->amount.value(),
		state->fee.value(),
		wallet->gaslessTermsValue(),
		draft->quote.value(),
		state->insufficient.value(),
		state->previewError.value(),
		state->loading.value(),
		state->loadError.value()
	) | rpl::map([=](
			int64 amount,
			int64 fee,
			const GaslessTerms &terms,
			const std::optional<SendQuote> &,
			bool insufficient,
			SendError error,
			bool loading,
			const QString &loadError) {
		const auto destination = state->flow
			? state->flow->destination
			: QString();
		return (amount > 0)
			&& (fee > 0)
			&& !terms.eligible(amount, destination)
			&& !insufficient
			&& (error == SendError::None)
			&& !loading
			&& loadError.isEmpty();
	}));
	feeWrap->finishAnimating();
	auto showInsufficient = rpl::combine(
		state->insufficient.value(),
		state->previewError.value(),
		state->loading.value(),
		state->loadError.value()
	) | rpl::map([](
			bool insufficient,
			SendError error,
			bool loading,
			const QString &loadError) {
		return insufficient
			&& (error != SendError::AmountTooSmall)
			&& !loading
			&& loadError.isEmpty();
	});
	const auto insufficientWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			balance,
			object_ptr<Ui::FlatLabel>(
				balance,
				tr::lng_wallet_send_error_insufficient(),
				st::walletSendUserErrorLabel)),
		style::al_justify);
	// An error that does not fit one line reads better split evenly than
	// with a full line above a single trailing word.
	insufficientWrap->entity()->setTryMakeSimilarLines(true);
	insufficientWrap->toggleOn(rpl::duplicate(showInsufficient));
	insufficientWrap->finishAnimating();
	const auto refusalValue = [=](bool insufficientStated) {
		return rpl::combine(
			state->previewError.value(),
			state->insufficient.value(),
			state->loading.value(),
			state->loadError.value(),
			state->silentFailure.value(),
			state->minTransfer.value(),
			rpl::single(rpl::empty) | rpl::then(Lang::Updated())
		) | rpl::map([insufficientStated](
				SendError error,
				bool insufficient,
				bool loading,
				QString loadError,
				bool silent,
				int64 minTransfer,
				rpl::empty_value) {
			if (loading || silent) {
				return QString();
			} else if (!loadError.isEmpty()) {
				return SendUserLoadErrorText(loadError);
			} else if (error != SendError::AmountTooSmall
				&& (insufficient
					|| error == SendError::InsufficientBalance
					|| error == SendError::InsufficientFees)) {
				return insufficientStated
					? SendErrorText(SendError::InsufficientFees, minTransfer)
					: QString();
			} else if (error == SendError::SigningUnavailable) {
				// A device without the key is not stated: the send press
				// acquires it, and the estimate is refused only while no
				// client can serve the preview at all.
				return QString();
			}
			return SendErrorText(error, minTransfer);
		});
	};
	auto refusalText = refusalValue(false);
	const auto refusalWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			balance,
			object_ptr<Ui::FlatLabel>(
				balance,
				rpl::duplicate(refusalText),
				st::walletSendUserErrorLabel)),
		style::al_justify);
	refusalWrap->entity()->setTryMakeSimilarLines(true);
	refusalWrap->toggleOn(std::move(refusalText) | rpl::map([](
			const QString &text) {
		return !text.isEmpty();
	}));
	refusalWrap->finishAnimating();
	const auto depositWrap = balance->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			balance,
			object_ptr<Ui::VerticalLayout>(balance)),
		style::al_justify);
	const auto depositInner = depositWrap->entity();
	const auto deposit = depositInner->add(
		object_ptr<Ui::RoundButton>(
			depositInner,
			tr::lng_wallet_add_funds(),
			st::defaultTableSmallButton),
		style::margins(0, st::walletDetailsAmountMinorSkip, 0, 0),
		style::al_top);
	deposit->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	deposit->setClickedCallback(receive);
	depositWrap->toggleOn(std::move(showInsufficient));
	depositWrap->finishAnimating();

	// This row swaps between a balance, a fee, an error and the deposit
	// button, and some of those swaps are instant while others slide, so
	// between two of them the row would have nothing in it and the whole box
	// would shrink and grow back. It keeps the height of one line whatever
	// it currently shows, which also spares the box the jump it made when
	// the rate and the balance arrived after it was already on screen.
	const auto reserve = balance->add(object_ptr<Ui::RpWidget>(balance));
	reserve->resize(reserve->width(), 0);
	const auto lineHeight = st::walletSendUserBalanceLabel.style.font->height;
	const auto errorHeight = st::walletSendUserErrorLabel.style.font->height;
	// The tallest this row goes: the balance and the fee together, or the
	// insufficient-funds line with the deposit button under it.
	const auto reserved = std::max(
		2 * lineHeight,
		errorHeight
			+ st::walletDetailsAmountMinorSkip
			+ st::defaultTableSmallButton.height);
	balance->heightValue() | rpl::on_next([=](int height) {
		const auto others = height - reserve->height();
		const auto add = std::max(reserved - others, 0);
		if (reserve->height() != add) {
			reserve->resize(reserve->width(), add);
		}
	}, reserve->lifetime());

	const auto requote = [=] {
		if (!draft->quote.current() && !draft->preparing.current()) {
			state->previewDependencies.reset();
		}
		refreshFee();
	};
	const auto refuse = [=](SendError error) {
		if (!weak || state->closed) {
			return;
		}
		stopSending();
		invalidateFee();
		state->previewError = error;
		if (error == SendError::InsufficientBalance
			|| error == SendError::InsufficientFees) {
			state->previewInsufficient = true;
		} else if (error == SendError::None) {
			requote();
		}
	};
	const auto checkQuote = [=] {
		if (!originValid() || !state->flow || draft->preparing.current()) {
			return SendError::InvalidRequest;
		} else if (state->previewError.current() != SendError::None) {
			return state->previewError.current();
		} else if (state->insufficient.current()) {
			return state->amount.current() > wallet->balanceNano()
				? SendError::InsufficientBalance
				: SendError::InsufficientFees;
		}
		const auto quote = draft->quote.current();
		const auto dependencies = quoteDependencies();
		if (!quote
			|| !quote->prepared
			|| quote != draft->quote.current()
			|| quote->revision != state->previewRevision
			|| quote->dependencies != dependencies
			|| quote->args.comment != draft->comment.current()) {
			return SendError::QuoteExpired;
		} else if (draft->privateEpoch
			&& (*draft->privateEpoch != wallet->vault().clearEpoch()
				|| !draft->authorization.valid()
				|| !wallet->vault().unlocked())) {
			return SendError::Locked;
		}
		return SendError::None;
	};
	// The send press acquires the key the way every key-requiring wallet
	// action does, through RunKeyRequiringAction: an unlock when this device
	// holds the key, the backup restore with its protection chooser when the
	// key is only in the cloud, the phrase import when there is no backup,
	// and the conflict resolution first when a different wallet is parked
	// here. The context owns every prompt of that ladder, so closing any of
	// them without a key ends the press quietly, and closing this box ends
	// it with them.
	const auto acquireSendKey = [=] {
		if (state->unlocking) {
			return;
		}
		state->unlocking = true;
		const auto context = std::make_shared<KeyContext>(
			show,
			nullptr,
			originValid,
			crl::guard(session, crl::guard(box, [=](KeyAuthorization auth) {
				state->unlocking = false;
				if (!state->sending.current() || state->submitted) {
					return;
				} else if (!auth.valid()) {
					// A declined key changes nothing the fee was quoted from.
					stopSending();
					requote();
					return;
				}
				state->sendAuthorization = std::move(auth);
				state->continueSend();
			})));
		state->keyContext = context;
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
			tr::lng_wallet_restore_text());
	};

	// One press of the send button is one request: the amount, recipient
	// and comment it was pressed with, and at most one unlock. A prepared
	// transfer is bound to the exact gasless terms and balance it was
	// estimated with, and those are refreshed on their own schedule, for
	// example while the unlock prompt is still open. None of that is a
	// change the user made and none of it is shown: the transfer is
	// prepared again under the same authorization and submitted. The same
	// holds when the session refuses a signed transfer as stale, which it
	// does only before anything leaves the device; that retry is bounded
	// so a refusal that keeps repeating cannot spin forever. When the
	// user edits what is being sent, the request just stops quietly.
	// WHY: the transfer now reports itself where the user is: in the wallet
	// window's list when the box is there, otherwise in the recipient's
	// chat, so the box closes instead of reporting anything.
	const auto handOver = [=](SendStarted started) {
		if (state->closed || !sessionValid()) {
			return;
		}
		const auto panel = wallet->panel();
		if (panel && box->window() == panel->window()) {
			state->handedOver = true;
			wallet->setWindowSend(started.operationId);
			if (const auto content = dynamic_cast<Content*>(panel->inner())) {
				content->flySendDiamond(started.operationId, amountField);
			}
			discardOrigin();
			show->hideLayer();
			return;
		} else if (!started.message) {
			return;
		}
		const auto window = MakeChatShow(show, true)->resolveWindow();
		if (!window) {
			return;
		}
		state->handedOver = true;
		discardOrigin();
		box->closeBox();
		window->showPeerHistory(started.message.peer);
		window->window().activate();
	};
	state->continueSend = [=] {
		if (!state->sending.current()
			|| state->submitted
			|| state->unlocking
			|| draft->preparing.current()) {
			return;
		} else if (!originValid() || !state->sendRequest) {
			refuse(SendError::InvalidRequest);
			return;
		}
		const auto request = *state->sendRequest;
		const auto expiresAt = state->sendExpiresAt;
		const auto now = quoteDependencies();
		if (!state->flow
			|| state->flow->expiresAt != expiresAt
			|| now.destination != request.destination
			|| now.bounce != request.bounce
			|| now.amountNano != request.amountNano
			|| now.comment != request.comment) {
			refuse(SendError::None);
			return;
		} else if (TransferLinkExpired(expiresAt)) {
			refuse(SendError::LinkExpired);
			return;
		}
		if (state->heldFee) {
			if (const auto quote = draft->quote.current()) {
				const auto shown = *base::take(state->heldFee);
				if (shown != QuoteConfirmFee(*quote)) {
					const auto held = state->sendAuthorization;
					stopSending();
					state->heldAuthorization = held;
					state->heldTimeout.callOnce(kHeldSendKeyTimeout);
					state->previewError = SendError::QuoteExpired;
					return;
				}
			}
		}
		const auto isPrivate = !request.comment.text.isEmpty()
			&& !request.comment.isPublic;
		const auto error = checkQuote();
		if (error == SendError::SigningUnavailable) {
			// No client could serve the preview: the key is still to be
			// acquired, or the client is being swapped for the signing one
			// right after it was, and the readiness change resumes the
			// press. A ready signing client that still refuses, or a swap
			// that outlasts the bound, is stated as a failure.
			if (!state->sendAuthorization.valid()) {
				acquireSendKey();
			} else if (wallet->signingReady() || state->signingTimedOut) {
				refuse(SendError::Failed);
			} else {
				awaitSigning();
			}
			return;
		} else if (error == SendError::QuoteExpired) {
			if (isPrivate && !state->sendAuthorization.valid()) {
				acquireSendKey();
			} else {
				prepareFee(state->sendAuthorization);
			}
			return;
		} else if (error != SendError::None) {
			refuse(error);
			return;
		} else if (!state->sendAuthorization.valid()) {
			acquireSendKey();
			return;
		} else if (!wallet->signingReady() && !state->signingTimedOut) {
			awaitSigning();
			return;
		}
		const auto accepted = *draft->quote.current();
		const auto authorization = state->sendAuthorization;
		const auto senderIdentity = state->senderIdentity;
		const auto displayForm = state->flow->displayForm;
		state->submitted = true;
		draft->quote = std::nullopt;
		draft->authorization = {};
		draft->privateEpoch.reset();
		const auto valid = [=] {
			return sessionValid()
				&& senderIdentity
				&& wallet->transferWalletIdentityCurrent(*senderIdentity);
		};
		wallet->send(
			authorization,
			accepted.prepared,
			crl::guard(session, [=](SendError error) {
				if (error == SendError::KeyChanged) {
					if (weak && !state->closed && !state->handedOver) {
						box->closeBox();
					}
					if (sessionValid()) {
						ShowWalletKeyChanged(show);
					}
					return;
				} else if (error == SendError::KeyMismatch
						&& (!weak || state->closed || state->handedOver)) {
					if (sessionValid()) {
						show->showToast(
							SendErrorText(error, TransferMinNanos(session)));
					}
					return;
				} else if (!weak || state->closed || !valid()) {
					return;
				} else if (state->handedOver) {
					return;
				} else if (error == SendError::QuoteExpired
					&& ++state->sendRefusals <= kSendRefusalRetries) {
					state->submitted = false;
					invalidateFee();
					scheduleContinueSend();
					return;
				} else if (error == SendError::QuoteExpired) {
					refuse(SendError::Failed);
					return;
				} else if (error != SendError::None
					&& error != SendError::SubmissionUnknown) {
					refuse(error);
					return;
				}
				const auto pending = wallet->pendingSend();
				auto item = pending
					? wallet->submittedTransaction(pending->operationId)
					: std::nullopt;
				if (!item && pending) {
					item = ItemFromPending(*pending);
				}
				const auto toast = (error == SendError::None)
					? tr::lng_wallet_sent_toast(
						tr::now,
						lt_address,
						ShortAddressForm(displayForm))
					: QString();
				discardOrigin();
				show->hideLayer();
				if (!valid()) {
					return;
				}
				if (!toast.isEmpty()) {
					show->showToast(toast);
				}
				if (sent && error == SendError::None) {
					sent();
				} else if (valid() && item) {
					ShowWalletTransactionBox(show, *item);
				}
			}),
			crl::guard(session, crl::guard(box, handOver)));
	};
	const auto startSend = [=] {
		if (!draft->preparing.current()
			&& (!draft->quote.current()
				|| state->previewDependencies != quoteDependencies())) {
			invalidateFee();
		}
		state->sendRequest = quoteDependencies();
		state->sendExpiresAt = state->flow->expiresAt;
		state->sendRefusals = 0;
		const auto held = state->heldAuthorization;
		dropHeldKey();
		if (held.grant && held.grant->valid() && wallet->vault().unlocked()) {
			state->sendAuthorization = held;
		}
		state->sending = true;
		state->continueSend();
	};
	const auto confirmSend = [=](const SendConfirmFee &shown) {
		if (!state->confirmBox
			|| !state->flow
			|| state->sending.current()
			|| !CommentFits(draft->comment.current().text)
			|| !state->canSend.current()) {
			return;
		}
		// WHY: Enter reaches here whatever the button shows, so a press made
		// while counting waits and goes on only at the fee shown; this press
		// answers the previous held press's "review the new fee" refusal.
		if (state->previewError.current() == SendError::QuoteExpired) {
			state->previewError = SendError::None;
		}
		if (draft->preparing.current()) {
			state->heldFee = shown;
		}
		startSend();
	};
	const auto confirmationClosed = [=](not_null<Ui::GenericBox*> raw) {
		if (!weak || state->closed || state->confirmBox.get() != raw.get()) {
			return;
		}
		state->confirmBox = nullptr;
		dropHeldKey();
		if (state->sending.current() && !state->submitted) {
			if (const auto context = base::take(state->keyContext)) {
				context->cancel();
			}
			stopSending();
		}
		invalidateFee();
		state->previewDependencies.reset();
		updateAmount();
		refreshFee();
	};
	const auto openConfirmation = [=] {
		if (state->confirmBox) {
			return;
		}
		auto fee = rpl::combine(
			draft->quote.value(),
			draft->preparing.value()
		) | rpl::map([](
				const std::optional<SendQuote> &quote,
				bool preparing) {
			return quote
				? QuoteConfirmFee(*quote)
				: SendConfirmFee{ .pending = preparing };
		}) | rpl::distinct_until_changed();
		auto confirm = Box(WalletSendConfirmBox, show, SendConfirmArgs{
			.draft = draft,
			.address = state->flow->displayForm,
			.amountNano = state->amount.current(),
			.fee = std::move(fee),
			.refusal = refusalValue(true),
			.busy = state->sending.value(),
			.canSend = state->canSend.value(),
			.send = crl::guard(box, confirmSend),
		});
		const auto raw = confirm.data();
		state->confirmBox = base::make_weak(raw);
		raw->boxClosing() | rpl::on_next(crl::guard(box, [=] {
			confirmationClosed(raw);
		}), raw->lifetime());
		box->uiShow()->showBox(std::move(confirm));
		requote();
	};
	const auto submit = [=] {
		if (state->unfunded.current()) {
			amountField->showError();
			return;
		} else if (!originValid() || !state->flow) {
			if (user && !state->loading.current()) {
				failLoading(userError());
			}
			return;
		} else if (state->sending.current()
			|| state->commentBox
			|| state->confirmBox
			|| !CommentFits(draft->comment.current().text)) {
			return;
		} else if (TransferAmountBelowMinimum(
				state->amount.current(),
				state->minTransfer.current())) {
			// A press below the minimum only corrects the amount.
			raiseToMinimum();
			return;
		} else if (!state->canSend.current()) {
			amountField->showError();
			return;
		}
		if (!user) {
			openConfirmation();
			return;
		}
		startSend();
	};
	auto buttonBusy = rpl::combine(
		state->loading.value(),
		state->sending.value()
	) | rpl::map([](bool loading, bool sending) {
		return loading || sending;
	});
	auto buttonText = BusyFooterLabel(
		rpl::combine(
			state->amount.value(),
			tr::lng_send_button(),
			tr::lng_wallet_send_amount(
				lt_amount,
				state->amount.value() | rpl::map([](int64 amount) {
					return Ui::FormatTonAmount(amount).full;
				}))
		) | rpl::map([](int64 amount, QString empty, QString full) {
			return amount > 0 ? full : empty;
		}),
		rpl::duplicate(buttonBusy));
	const auto button = box->addButton(std::move(buttonText), submit).data();
	rpl::combine(
		state->canSend.value(),
		state->raisable.value(),
		state->sending.value()
	) | rpl::on_next([=](bool canSend, bool raisable, bool sending) {
		SetButtonDisabledLook(button, !canSend && !raisable && !sending);
	}, button->lifetime());
	AddBusyFooterSpinner(button, std::move(buttonBusy));
	amountField->submits() | rpl::on_next(submit, amountField->lifetime());

	if (!user) {
		state->flow = initial;
		state->flow->draft = draft;
		const auto cached = wallet->userAddresses().publicKey(
			state->flow->destination);
		state->recipientKey = (self && cached.isEmpty())
			? state->senderIdentity->publicKey
			: cached;
	}
	refreshFee();
	const auto resolveRecipient = [=] {
		if (state->recipientRequested || !originValid() || !state->flow) {
			return;
		}
		state->recipientRequested = true;
		wallet->resolveCommentRecipient(
			state->flow->destination,
			state->flow->bounce,
			state->recipientKey,
			crl::guard(box, [=](CommentRecipient recipient) {
				if (recipient == CommentRecipient::PlainOnly
					&& !state->closed
					&& !state->terminal) {
					switchToPlain();
				}
			}));
	};
	state->loading.value() | rpl::on_next(resolveRecipient, box->lifetime());

	box->setFocusCallback([=] { amountField->setFocusFast(); });
	if (state->loading.current()) {
		const auto recompute = [=] {
			if (state->closed || state->terminal) {
				return;
			} else if (!sessionValid()) {
				failLoading(u"WALLET_NOT_READY"_q);
				return;
			}
			const auto presence = wallet->presenceCurrent();
			const auto error = user
				? userError()
				: (presence == Presence::Ready)
				? QString()
				: u"WALLET_NOT_READY"_q;
			if (notReady
				&& user
				&& !state->forceIssued
				&& !state->senderIdentity
				&& error == u"WALLET_NOT_READY"_q
				&& presence != Presence::Unknown
				&& presence != Presence::Unavailable) {
				const auto onstack = notReady;
				failLoading(error, true);
				onstack();
				return;
			} else if (error == u"WALLET_NOT_READY"_q
				&& !state->forceIssued
				&& (presence == Presence::Unknown
					|| presence == Presence::Provisioning)) {
				return;
			} else if (user
				&& !state->forceIssued
				&& error == u"WALLET_BALANCE_EMPTY"_q) {
				if (!state->unfunded.current()) {
					state->senderIdentity = wallet->transferWalletIdentity();
					if (!state->senderIdentity) {
						failLoading(u"WALLET_NOT_READY"_q);
						return;
					}
					state->loadDeadline.cancel();
					state->unfunded = true;
					state->loading = false;
				}
				return;
			} else if (!error.isEmpty()) {
				failLoading(presence == Presence::Unavailable
					? u"WALLET_UNAVAILABLE"_q
					: error);
				return;
			}
			if (state->unfunded.current()) {
				state->loading = true;
				state->unfunded = false;
				state->loadDeadline.callOnce(kSendUserLoadTimeout);
			} else if (!state->loading.current()) {
				refreshFee();
				return;
			} else if (state->forceIssued) {
				return;
			}
			state->senderIdentity = wallet->transferWalletIdentity();
			if (!state->senderIdentity) {
				failLoading(u"WALLET_NOT_READY"_q);
				return;
			} else if (!user) {
				state->loadDeadline.cancel();
				state->loading = false;
				return;
			}
			state->forceIssued = true;
			const auto revision = state->loadRevision;
			const auto current = [=] {
				return !state->closed
					&& !state->terminal
					&& revision == state->loadRevision;
			};
			wallet->userAddresses().forceResolve(
				userId,
				crl::guard(session, crl::guard(box, [=](QString address) {
					if (!current()) {
						return;
					}
					const auto error = userError();
					if (!error.isEmpty()) {
						failLoading(error);
						return;
					}
					auto flow = ParseRecipientFlow(FormatFriendly(address, false));
					if (!flow
						|| !user->gramAddress()
						|| *user->gramAddress() != flow->destination
						|| (initial
							&& initial->destination != flow->destination)) {
						failLoading(u"WALLET_ADDRESS_INVALID"_q);
						return;
					}
					flow->draft = draft;
					if (initial) {
						flow->expiresAt = initial->expiresAt;
					}
					state->flow = std::move(flow);
					state->recipientKey
						= wallet->userAddresses().publicKey(address);
					state->loadDeadline.cancel();
					state->loading = false;
				})),
				crl::guard(session, crl::guard(box, [=](ForceResolveError error) {
					if (!current()) {
						return;
					} else if (error.silent) {
						failLoading(error.type, true);
						return;
					}
					const auto changed = userError();
					failLoading(changed.isEmpty() ? error.type : changed);
				})));
		};
		const auto schedule = [=] {
			if (state->closed || state->terminal || state->recomputeQueued) {
				return;
			}
			state->recomputeQueued = true;
			const auto revision = state->loadRevision;
			Ui::PostponeCall(box, [=] {
				state->recomputeQueued = false;
				if (revision == state->loadRevision) {
					recompute();
				}
			});
		};
		state->loadDeadline.setCallback([=] {
			failLoading(state->forceIssued
				? u"WALLET_ADDRESS_INVALID"_q
				: u"WALLET_NOT_READY"_q);
		});
		state->loadDeadline.callOnce(kSendUserLoadTimeout);
		wallet->stateKnownValue() | rpl::on_next(schedule, box->lifetime());
		wallet->balanceNanoValue() | rpl::on_next(schedule, box->lifetime());
		wallet->custodyUpdates() | rpl::on_next(schedule, box->lifetime());
		if (user) {
			wallet->userAddresses().unavailableValue(
			) | rpl::on_next(schedule, box->lifetime());
			user->flagsValue() | rpl::on_next(schedule, box->lifetime());
			session->changes().peerUpdates(
				user,
				Data::PeerUpdate::Flag::FullInfo
					| Data::PeerUpdate::Flag::Name
					| Data::PeerUpdate::Flag::SupportInfo
			) | rpl::on_next(schedule, box->lifetime());
			const auto error = userError();
			if (!error.isEmpty()
				&& error != u"WALLET_NOT_READY"_q
				&& error != u"WALLET_BALANCE_EMPTY"_q) {
				failLoading(error);
				return;
			}
		}
		wallet->presenceValue() | rpl::on_next(schedule, box->lifetime());
		wallet->transferWalletIdentityChanges(
		) | rpl::on_next(schedule, box->lifetime());
		wallet->refreshState();
	}
}

} // namespace ContentDetails

} // namespace Wallet
