#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

void ShowInvalidSecretWords(
		std::shared_ptr<Main::SessionShow> show,
		bool foreign,
		std::shared_ptr<KeyContext> context) {
	auto args = Ui::ConfirmBoxArgs{
		.confirmText = tr::lng_bot_download_retry(),
		.title = tr::lng_wallet_import_invalid_title(),
	};
	if (foreign) {
		args.text = tr::lng_wallet_import_invalid_spelling(tr::now)
			+ u"\n\n"_q
			+ tr::lng_wallet_import_invalid_scheme(tr::now);
	} else {
		args.text = tr::lng_wallet_import_invalid_spelling(tr::now);
	}
	const auto box = show->show(Ui::MakeInformBox(std::move(args)));
	if (context) {
		context->allowPromptRetry(box);
	}
}

int AddressGroupsWidth(
		const style::font &font,
		const QString &address,
		int groupsPerLine) {
	return groupsPerLine * font->width(address.left(kAddressGroup))
		+ (groupsPerLine - 1) * font->width(QChar(' '));
}

void PaintAddressGroups(
		QPainter &p,
		const style::font &font,
		const QString &address,
		QPoint origin,
		int groupsPerLine) {
	const auto groupWidth = font->width(address.left(kAddressGroup));
	const auto spaceWidth = font->width(QChar(' '));
	const auto groups = kAddressLength / kAddressGroup;
	p.setFont(font);
	for (auto i = 0; i != groups; ++i) {
		const auto line = i / groupsPerLine;
		const auto column = i % groupsPerLine;
		p.setPen((i % 2) ? st::windowSubTextFg : st::windowFg);
		p.drawText(
			origin.x() + column * (groupWidth + spaceWidth),
			origin.y() + line * font->height + font->ascent,
			address.mid(i * kAddressGroup, kAddressGroup));
	}
}

void AddAddressPlate(
		not_null<Ui::VerticalLayout*> container,
		const QString &address,
		const style::margins &margin,
		std::shared_ptr<Ui::Show> show) {
	const auto font = st::walletAddressPlateFont->monospace();
	const auto inner = st::walletAddressPlateInner;
	const auto groups = kAddressLength / kAddressGroup;
	const auto lines = groups / kAddressGroupsPerLine;
	const auto plate = container->add(
		object_ptr<Ui::FixedHeightWidget>(
			container,
			inner + lines * font->height + inner),
		margin);
	plate->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(plate);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBgOver);
		p.drawRoundedRect(
			plate->rect(),
			st::walletAddressPlateRadius,
			st::walletAddressPlateRadius);
		const auto lineWidth = AddressGroupsWidth(
			font,
			address,
			kAddressGroupsPerLine);
		const auto left = (plate->width() - lineWidth) / 2;
		PaintAddressGroups(
			p,
			font,
			address,
			{ left, inner },
			kAddressGroupsPerLine);
	}, plate->lifetime());
	if (show) {
		const auto copy = Ui::CreateChild<Ui::AbstractButton>(plate);
		copy->setClickedCallback(
			CopyAddressCallback(std::move(show), address));
		plate->sizeValue(
		) | rpl::on_next([=](QSize size) {
			copy->setGeometry(QRect(QPoint(), size));
		}, copy->lifetime());
	}
}

void ShowWrongSecretWords(
		std::shared_ptr<Main::SessionShow> show,
		bool outdated,
		std::shared_ptr<KeyContext> context) {
	const auto address = outdated
		? QString()
		: show->session().wallet().addressFriendly(false);
	const auto shown = show->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto &padding = st::boxPadding;
		const auto plate = (address.size() == kAddressLength);
		box->setTitle(tr::lng_wallet_restore_wrong_title());
		box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				(outdated
					? tr::lng_wallet_restore_outdated_text()
					: tr::lng_wallet_restore_other_text()),
				st::boxLabel),
			QMargins(
				padding.left(),
				0,
				padding.right(),
				plate ? 0 : padding.bottom()));
		if (plate) {
			AddAddressPlate(
				box->verticalLayout(),
				address,
				QMargins(
					padding.left(),
					st::walletAddressPlateSkip,
					padding.right(),
					padding.bottom()));
		}
		box->addButton(tr::lng_bot_download_retry(), [=] {
			box->closeBox();
		});
	}));
	if (context) {
		context->allowPromptRetry(shown);
	}
}

void ShowSendRecipientWallet(
		std::shared_ptr<Ui::Show> show,
		not_null<UserData*> user,
		const QString &address,
		Fn<void()> profile) {
	const auto shown = user->username().isEmpty()
		? user->name()
		: ('@' + user->username());
	show->show(Box([=](not_null<Ui::GenericBox*> box) {
		const auto &padding = st::boxPadding;
		box->setTitle(tr::lng_wallet_send_user_wallet_title(
			lt_name,
			rpl::single(user->shortName())));
		const auto about = box->addRow(
			object_ptr<Ui::FlatLabel>(
				box,
				tr::lng_wallet_send_user_wallet_about(
					lt_name,
					rpl::single(tr::link(shown)),
					tr::marked),
				st::boxLabel),
			QMargins(padding.left(), 0, padding.right(), 0));
		about->setClickHandlerFilter([=](const auto &...) {
			const auto onstack = profile;
			onstack();
			return false;
		});
		AddAddressPlate(
			box->verticalLayout(),
			address,
			QMargins(
				padding.left(),
				st::walletAddressPlateSkip,
				padding.right(),
				padding.bottom()),
			box->uiShow());
		box->addButton(tr::lng_box_ok(), [=] {
			box->closeBox();
		});
	}));
}

// A restore that lands a record swaps the public-key-only client for the
// signing one asynchronously, so a continuation that reads the signing
// client - the rotation offer of a backup disable, a send - would find it
// missing if it ran at once. It runs once the session reports the signing
// client ready. The wait is bounded: a recovery that never settles lets the
// continuation run and be refused typed by the session on its own.
void RunWhenSigningReady(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> action) {
	if (!action) {
		return;
	}
	auto &wallet = show->session().wallet();
	if (wallet.signingReady()) {
		action();
		return;
	}
	const auto weakSession = base::make_weak(&show->session());
	const auto lifetime = std::make_shared<rpl::lifetime>();
	const auto run = [=] {
		const auto owned = base::take(*lifetime);
		if (weakSession && show->valid()) {
			action();
		}
	};
	const auto timeout = lifetime->make_state<base::Timer>(run);
	timeout->callOnce(kSigningReadyTimeout);
	wallet.signingReadyValue() | rpl::filter([](bool ready) {
		return ready;
	}) | rpl::on_next(run, *lifetime);
}

void RequestCustodyRestore(
		std::shared_ptr<Main::SessionShow> show,
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> action,
		Fn<void()> unblock,
		std::shared_ptr<KeyContext> context,
		Fn<void()> onPasswordMissing) {
	if (context && !context->valid()) {
		context->cancel();
		return;
	}
	const auto purpose = !context
		? u"backup_management"_q
		: context->scope() ? u"decrypt_comment"_q : u"send"_q;
	LOG(("Wallet Info: key restore requested purpose=%1 password_supplied=%2."
		).arg(purpose).arg(password.has_value()));
	const auto done = [=] {
		if (passcode) {
			passcode->closeBox();
		}
		RunWhenSigningReady(show, action);
	};
	const auto fail = [=](const QString &error) {
		const auto contextValid = !context || context->valid();
		LOG(("Wallet Error: key restore result purpose=%1 error=%2 "
			"context_valid=%3 password_prompt=%4."
			).arg(purpose
			).arg(error
			).arg(contextValid
			).arg(bool(passcode)));
		if (context && (!contextValid
			|| error == u"PHRASE_ORIGIN_EXPIRED"_q
			|| error == u"PHRASE_SILENT_ERROR"_q)) {
			context->cancel();
			return;
		}
		if (onPasswordMissing && error == u"PASSWORD_MISSING"_q) {
			onPasswordMissing();
			return;
		}
		auto terminal = true;
		const auto finish = gsl::finally([&] {
			if (context && terminal) {
				context->cancel();
			}
		});
		if (!context && unblock) {
			unblock();
		}
		// A dismissed protection chooser restored nothing and has nothing to
		// state. The cloud password box goes with it, because the proof it
		// has already sent cannot be sent a second time.
		if (error == u"PHRASE_INSTALL_CANCELLED"_q) {
			if (passcode) {
				passcode->closeBox();
			}
			return;
		}
		if (error == u"PHRASE_INSTALL_FAILED"_q) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showToast(tr::lng_wallet_key_save_error(tr::now));
			return;
		}
		if (passcode && passcode->handleCustomCheckError(error)) {
			terminal = false;
			return;
		}
		if (error == u"PHRASE_VAULT_LOCKED"_q) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showToast(tr::lng_wallet_vault_locked(tr::now));
			return;
		}
		if (auto box = PrePasswordErrorBox(
				error,
				&show->session(),
				EnforcementCheckAbout(error),
				context)) {
			if (passcode) {
				if (context) {
					context->closePrompt(passcode);
				} else {
					passcode->closeBox();
				}
			}
			show->showBox(std::move(box));
			terminal = false;
			return;
		}
		ShowPhraseError(show, PhraseOperation::Restore, error);
	};
	auto &wallet = show->session().wallet();
	if (context) {
		wallet.restoreFromBackup(
			std::move(auth),
			std::move(password),
			context->scope(),
			[=](KeyAuthorization restored) {
				context->ready(std::move(restored));
			},
			fail);
	} else {
		wallet.restoreFromBackup(
			std::move(auth),
			std::move(password),
			done,
			fail);
	}
}

// The restore is requested without a password first, even when the account
// has one, because the server decides whether this backup needs it. Only
// its PASSWORD_MISSING answer loads the cloud password state, asks for the
// password and repeats the restore with it.
void StartCustodyRestore(
		std::shared_ptr<Main::SessionShow> show,
		KeyAuthorization auth,
		Fn<void()> action,
		Fn<void()> unblock,
		std::shared_ptr<KeyContext> context) {
	if (context && !context->valid()) {
		context->cancel();
		return;
	}
	const auto session = &show->session();
	const auto firstKeyUse = RestoreIsFirstKeyUse(session);
	const auto askPassword = [=] {
		if (context && !context->valid()) {
			context->cancel();
			return;
		}
		const auto cached = session->api().cloudPassword().stateCurrent();
		LOG(("Wallet Info: key restore requires password; "
			"cached_state=%1 cached_has_password=%2."
			).arg(cached.has_value()).arg(cached && cached->hasPassword));
		session->api().cloudPassword().reload();
		const auto lifetime = std::make_shared<rpl::lifetime>();
		if (context) {
			context->lifetime().add([=] { lifetime->destroy(); });
			const auto timeout = lifetime->make_state<base::Timer>([=] {
				const auto owned = base::take(*lifetime);
				if (context->valid()) {
					LOG(("Wallet Error: restore password state timed out "
						"after %1 ms; comment_scope=%2."
						).arg(kCommentPasswordStateTimeout
						).arg(bool(context->scope())));
					ShowPhraseError(
						show,
						PhraseOperation::Restore,
						u"PHRASE_PASSWORD_STATE_TIMEOUT"_q);
				}
				context->cancel();
			});
			timeout->callOnce(kCommentPasswordStateTimeout);
		}
		session->api().cloudPassword().state(
		) | rpl::take(
			1
		) | rpl::on_next([=](const Core::CloudPasswordState &state) {
			const auto owned = base::take(*lifetime);
			if (context && !context->valid()) {
				context->cancel();
				return;
			}
			if (!state.hasPassword) {
				// The server asked for a password this account does not
				// have, so a repeat without one would only be refused again.
				ShowPhraseError(
					show,
					PhraseOperation::Restore,
					u"PHRASE_PASSWORD_STATE_MISSING"_q);
				if (context) {
					context->cancel();
				} else if (unblock) {
					unblock();
				}
				return;
			}
			auto fields = RestorePasswordFields(
				state,
				firstKeyUse,
				tr::lng_bots_password_confirm_description(tr::now));
			fields.customShow = context;
			fields.customCheckCallback = [=](
					const Core::CloudPasswordResult &result,
					base::weak_qptr<PasscodeBox> passcode) {
				RequestCustodyRestore(
					show,
					auth,
					result,
					passcode,
					action,
					unblock,
					context);
			};
			const auto passcode = show->show(
				Box<PasscodeBox>(session, fields));
			if (context) {
				context->cancelOnClose(passcode, true);
			} else if (unblock) {
				unblock();
			}
		}, *lifetime);
	};
	RequestCustodyRestore(
		show,
		std::move(auth),
		std::nullopt,
		nullptr,
		action,
		unblock,
		context,
		askPassword);
}

// The device mode is unknown only while the wallet's state has not reached
// this client, which is the normal state of a freshly logged in account
// outside the Wallet window: nothing else asks for it. A press asks, waits
// for the answer and then climbs the ladder that answer names. The wait is
// bounded, and a wallet the server will not serve is stated once.
void ResolveDeviceCustody(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> resolved,
		std::shared_ptr<KeyContext> context) {
	const auto weakSession = base::make_weak(&show->session());
	auto &wallet = show->session().wallet();
	const auto lifetime = std::make_shared<rpl::lifetime>();
	wallet.startPolling();
	lifetime->add([weakSession] {
		if (weakSession) {
			weakSession->wallet().stopPolling();
		}
	});
	if (context) {
		context->lifetime().add([lifetime] { lifetime->destroy(); });
	}
	const auto settled = std::make_shared<bool>(false);
	const auto finish = [=](bool known) {
		if (*settled) {
			return;
		}
		*settled = true;
		const auto owned = base::take(*lifetime);
		if (!weakSession || !show->valid()) {
			if (context) {
				context->cancel();
			}
			return;
		} else if (known) {
			resolved();
			return;
		}
		show->showToast(tr::lng_wallet_unavailable(tr::now));
		if (context) {
			context->cancel();
		}
	};
	const auto check = [=] {
		if (*settled || !weakSession) {
			return;
		} else if (context && !context->valid()) {
			*settled = true;
			const auto owned = base::take(*lifetime);
			context->cancel();
			return;
		}
		auto &wallet = weakSession->wallet();
		const auto presence = wallet.presence();
		if (wallet.deviceCustodyState().mode != DeviceMode::Unknown) {
			finish(true);
		} else if (presence == Presence::Unavailable
			|| presence == Presence::Missing
			|| presence == Presence::AddressUnreadable) {
			finish(false);
		}
	};
	const auto timeout = lifetime->make_state<base::Timer>([=] {
		finish(false);
	});
	timeout->callOnce(kCustodyResolveTimeout);
	rpl::merge(
		wallet.transferWalletIdentityChanges(),
		wallet.custodyUpdates()
	) | rpl::on_next(check, *lifetime);
	check();
}

void RunKeyRequiringAction(
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> action,
		KeyActionKind kind,
		std::shared_ptr<KeyContext> context,
		rpl::producer<QString> importAbout) {
	if (context && !context->valid()) {
		context->cancel();
		return;
	}
	const auto state = show->session().wallet().deviceCustodyState();
	if (state.conflict) {
		// Resolving the conflict lands no key: it drops the parked record
		// or exports its phrase. The same press climbs this ladder again
		// once the parked wallet is switched away, into whatever the served
		// wallet then needs. The export runs over the plain show, because
		// the context reads a closed prompt of its own as the end of the
		// press, and the export box is closed on the way back to the list.
		const auto plain = context ? context->plain() : show;
		const auto retry = [=] {
			if (context) {
				context->acceptClosed();
			}
			RunKeyRequiringAction(
				show,
				action,
				kind,
				context,
				rpl::duplicate(importAbout));
		};
		show->showBox(Box(WalletConflictBox, plain, retry));
	} else if (state.mode == DeviceMode::Full) {
		action();
	} else if (state.mode == DeviceMode::ReadOnlyRestorable) {
		if (kind == KeyActionKind::Reveal) {
			action();
		} else {
			StartCustodyRestore(
				show,
				KeyAuthorization{ .install = context
					? context->installer()
					: MakeCustodyInstaller(show) },
				std::move(action),
				nullptr,
				context);
		}
	} else if (state.mode == DeviceMode::ReadOnlyNotRestorable) {
		show->showBox(Box(
			WalletImportBox,
			show,
			WalletImportMode::Restore,
			(kind == KeyActionKind::ResumeAfterRestore) ? action : nullptr,
			context,
			std::move(importAbout)));
	} else {
		ResolveDeviceCustody(show, [=] {
			RunKeyRequiringAction(
				show,
				action,
				kind,
				context,
				rpl::duplicate(importAbout));
		}, context);
	}
}

void RequestWalletReplace(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		std::optional<std::vector<QString>> words,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> unblock,
		Fn<void(const QString &text)> showError) {
	const auto imported = words.has_value();
	const auto done = crl::guard(origin, [=](CustodyOutcome outcome) {
		if (passcode) {
			passcode->closeBox();
		}
		// Only an imported replace carries a custody write - replaceWithNew
		// hands finishConfirmedReplace no new record - so the imported title
		// is the right one here. The replacement itself stands, so this
		// states the half that failed instead of a failure, and closes the
		// import box with the same layer operation rather than racing a hide.
		if (outcome == CustodyOutcome::WriteFailed) {
			show->showBox(
				Ui::MakeInformBox({
					.text = tr::lng_wallet_imported_not_stored(tr::now),
					.title = tr::lng_wallet_imported_title(),
				}),
				Ui::LayerOption::CloseOther);
			return;
		}
		show->hideLayer();
		show->showToast({
			.title = (imported
				? tr::lng_wallet_imported_title(tr::now)
				: tr::lng_wallet_created_title(tr::now)),
			.text = { imported
				? tr::lng_wallet_imported_text(tr::now)
				: tr::lng_wallet_created_text(tr::now) },
			.icon = &st::toastCheckIcon,
		});
	});
	const auto fail = crl::guard(origin, [=](const QString &error) {
		unblock();
		// A dismissed protection chooser imported nothing and has nothing to
		// state. The cloud password box goes with it, because the proof it
		// has already sent cannot be sent a second time.
		if (error == u"REPLACE_INSTALL_CANCELLED"_q) {
			if (passcode) {
				passcode->closeBox();
			}
			return;
		}
		if (passcode && passcode->handleCustomCheckError(error)) {
			return;
		}
		if (error == u"REPLACE_VAULT_LOCKED"_q) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showToast(tr::lng_wallet_vault_locked(tr::now));
			return;
		}
		if (auto box = PrePasswordErrorBox(
				error,
				&show->session(),
				EnforcementCheckAbout(error))) {
			if (passcode) {
				passcode->closeBox();
			}
			show->showBox(std::move(box));
			return;
		}
		if (passcode) {
			passcode->closeBox();
		}
		if (!imported) {
			show->showToast(ErrorWithType(
				tr::lng_wallet_create_error(tr::now),
				error));
		} else if (error == u"REPLACE_INVALID_PHRASE"_q
			|| error == u"REPLACE_FOREIGN_PHRASE"_q) {
			ShowInvalidSecretWords(
				show,
				error == u"REPLACE_FOREIGN_PHRASE"_q);
		} else if (error == u"REPLACE_OUTDATED_PHRASE"_q) {
			if (showError) {
				showError(QString());
			}
			ShowWrongSecretWords(show, true, nullptr);
		} else {
			// WHY: the server checks the proof against the key the chain
			// holds for the phrase's address, so a refused phrase stays
			// refused; a retry cannot help, only support can.
			const auto text = (error == u"REPLACE_KEY_CHANGING"_q)
				? tr::lng_wallet_import_key_changing(tr::now)
				: (error == u"WALLET_PROOF_INVALID"_q)
				? tr::lng_wallet_import_not_verified(tr::now)
				: ErrorWithType(tr::lng_wallet_import_failed(tr::now), error);
			if (showError) {
				showError(text);
			} else {
				show->showToast(text);
			}
		}
	});
	auto &wallet = show->session().wallet();
	if (imported) {
		wallet.replaceWithImported(
			KeyAuthorization{ .install = MakeCustodyInstaller(show) },
			std::move(*words),
			std::move(password),
			done,
			fail);
	} else {
		wallet.replaceWithNew(std::move(password), done, fail);
	}
}

void StartWalletReplace(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		std::optional<std::vector<QString>> words,
		Fn<void()> unblock,
		Fn<void(const QString &text)> showError) {
	const auto session = &show->session();
	session->api().cloudPassword().reload();
	session->api().cloudPassword().state(
	) | rpl::take(
		1
	) | rpl::on_next([=](const Core::CloudPasswordState &state) {
		if (!state.hasPassword) {
			RequestWalletReplace(
				show,
				origin,
				words,
				std::nullopt,
				nullptr,
				unblock,
				showError);
			return;
		}
		auto fields = PasscodeBox::CloudFields::From(state);
		fields.customTitle = tr::lng_bots_password_confirm_title();
		fields.customDescription = tr::lng_bots_password_confirm_description(
			tr::now);
		fields.customSubmitButton = tr::lng_passcode_submit();
		fields.customCheckCallback = [=](
				const Core::CloudPasswordResult &result,
				base::weak_qptr<PasscodeBox> passcode) {
			RequestWalletReplace(
				show,
				origin,
				words,
				result,
				passcode,
				unblock,
				showError);
		};
		show->showBox(Box<PasscodeBox>(session, fields));
		unblock();
	}, origin->lifetime());
}

} // namespace ContentDetails

} // namespace Wallet
