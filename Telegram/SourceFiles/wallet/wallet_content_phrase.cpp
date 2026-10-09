#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

[[nodiscard]] QString PhraseBoxLottie(int wordsCount) {
	return (wordsCount > kImportWordCountShort)
		? QString()
		: u"wallet/paper"_q;
}

void AddPhraseBoxHeader(
		not_null<Ui::GenericBox*> box,
		const QString &lottieName,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> text,
		const style::FlatLabel &textLabel,
		int lottieSize,
		const style::margins &lottieMargin,
		const style::margins &textMargin) {
	if (!lottieName.isEmpty()) {
		auto icon = Settings::CreateLottieIcon(
			box->verticalLayout(),
			{
				.name = lottieName,
				.sizeOverride = { lottieSize, lottieSize },
			},
			lottieMargin);
		box->verticalLayout()->add(std::move(icon.widget));
		box->showFinishes(
		) | rpl::on_next([animate = std::move(icon.animate)] {
			animate(anim::repeat::once);
		}, box->lifetime());
	}
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(title),
			st::walletPhraseTitleLabel),
		(lottieName.isEmpty()
			? st::walletPhraseTitlePadding
			: st::boxRowPadding),
		style::al_top);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			std::move(text),
			textLabel),
		textMargin,
		style::al_top
	)->setTryMakeSimilarLines(true);
}

void AddPhraseGrid(
		not_null<Ui::GenericBox*> box,
		const std::vector<QString> &words) {
	const auto count = int(words.size());
	const auto grid = box->addRow(
		object_ptr<Ui::RpWidget>(box),
		st::walletPhraseGridPadding);
	const auto rows = count / 2;
	auto numberLabels = std::vector<Ui::FlatLabel*>();
	auto wordLabels = std::vector<Ui::FlatLabel*>();
	for (auto i = 0; i != count; ++i) {
		numberLabels.push_back(Ui::CreateChild<Ui::FlatLabel>(
			grid,
			QString::number(i + 1) + QChar('.'),
			st::walletPhraseNumberLabel));
		wordLabels.push_back(Ui::CreateChild<Ui::FlatLabel>(
			grid,
			words[i],
			st::walletPhraseWordLabel));
	}
	const auto rowHeight = wordLabels.front()->height();
	grid->resize(
		grid->width(),
		rows * rowHeight + (rows - 1) * st::walletPhraseRowSkip);
	grid->widthValue(
	) | rpl::on_next([=](int width) {
		const auto column = (width - st::walletPhraseColumnSkip) / 2;
		for (auto i = 0; i != int(wordLabels.size()); ++i) {
			const auto x = (i < rows)
				? 0
				: (column + st::walletPhraseColumnSkip);
			const auto y = (i % rows)
				* (rowHeight + st::walletPhraseRowSkip);
			numberLabels[i]->moveToLeft(x, y, width);
			wordLabels[i]->moveToLeft(
				x + st::walletPhraseNumberWidth,
				y,
				width);
		}
	}, grid->lifetime());
}

void WalletPhraseBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::vector<QString> words) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	const auto count = int(words.size());
	AddPhraseBoxHeader(
		box,
		PhraseBoxLottie(count),
		tr::lng_wallet_phrase_title(),
		tr::lng_wallet_phrase_text(
			lt_count,
			rpl::single(count * 1.) | tr::to_count(),
			tr::marked),
		st::walletPhraseTextLabel,
		st::walletCoverLottieSize,
		st::walletPhraseLottieMargin,
		st::walletPhraseTextMargin);

	AddPhraseGrid(box, words);

	AddBoxCloseButton(box);
	box->addButton(tr::lng_about_done(), [=] { box->closeBox(); });
	SubmitBoxOnEnter(box, [=] { box->closeBox(); });
}

[[nodiscard]] TextWithEntities EnforcementCheckAbout(const QString &error) {
	auto result = tr::marked();
	const auto prefixes = {
		u"PASSWORD_TOO_FRESH_"_q,
		u"SESSION_TOO_FRESH_"_q,
	};
	for (const auto &prefix : prefixes) {
		if (!error.startsWith(prefix)) {
			continue;
		}
		const auto seconds = error.mid(prefix.size()).toInt();
		if (seconds > 0) {
			result.append(tr::lng_wallet_phrase_check_wait(
				tr::now,
				lt_duration,
				tr::marked(Ui::FormatResetCloudPasswordIn(seconds)),
				tr::marked)
			).append(QChar('\n')).append(QChar('\n'));
		}
		break;
	}
	result.append(tr::lng_bots_password_confirm_check_about(
		tr::now,
		tr::marked));
	return result;
}

void ShowPhraseError(
		std::shared_ptr<Main::SessionShow> show,
		PhraseOperation operation,
		const QString &error) {
	const auto text = [&] {
		switch (operation) {
		case PhraseOperation::Reveal:
			return tr::lng_wallet_phrase_error(tr::now);
		case PhraseOperation::Restore:
			return tr::lng_wallet_restore_error(tr::now);
		case PhraseOperation::DropParked:
			return tr::lng_wallet_conflict_switch_error(tr::now);
		}
		Unexpected("Operation in ShowPhraseError.");
	}();
	const auto &wallet = show->session().wallet();
	const auto identity = wallet.transferWalletIdentity();
	LOG(("Wallet Error: key access toast operation=%1 error=%2 "
		"address=%3 key=%4 revision=%5 presence=%6 device_mode=%7 "
		"conflict=%8."
		).arg((operation == PhraseOperation::Reveal)
			? u"reveal"_q
			: (operation == PhraseOperation::Restore)
			? u"restore"_q
			: u"drop_parked"_q
		).arg(error
		).arg(identity ? identity->address : u"(none)"_q
		).arg(QString::fromLatin1(wallet.publicKey().toHex())
		).arg(identity ? identity->revision : 0
		).arg(int(wallet.presenceCurrent())
		).arg(int(wallet.deviceCustodyState().mode)
		).arg(wallet.deviceCustodyState().conflict));
	show->showToast(ErrorWithType(text, error));
}

// ReadOnlyRestorable ends as soon as custody is installed, so an entry that
// reaches the cloud password box in that mode is by construction the first
// key use on this device: that box then carries the lock-and-key header
// explaining what the restore is about to do, while any other mode keeps
// the plain prompt with the given description.
[[nodiscard]] bool RestoreIsFirstKeyUse(not_null<Main::Session*> session) {
	const auto state = session->wallet().deviceCustodyState();
	return (state.mode == DeviceMode::ReadOnlyRestorable);
}

[[nodiscard]] PasscodeBox::CloudFields RestorePasswordFields(
		const Core::CloudPasswordState &state,
		bool firstKeyUse,
		const QString &description) {
	auto result = PasscodeBox::CloudFields::From(state);
	result.customSubmitButton = tr::lng_passcode_submit();
	if (firstKeyUse) {
		result.customHeader = PasscodeBox::CloudFields::CustomHeader{
			.lottie = u"cloud_password/intro"_q,
			.lottieSize = st::walletHowLottieSize,
			.lottieMargin = st::walletHowLottieMargin,
			.title = tr::lng_settings_cloud_password_check_subtitle(),
			.description = tr::lng_wallet_restore_explain_text(),
		};
	} else {
		result.customTitle = tr::lng_bots_password_confirm_title();
		result.customDescription = description;
	}
	return result;
}

// The backup export is sent without a password first, even when the account
// has one, because the server decides whether this export needs it. Only
// its PASSWORD_MISSING answer goes to onPasswordMissing, which asks for the
// cloud password and repeats the request with it; that repeat carries its
// password box here, so its password errors go back to that box.
void RequestPhraseReveal(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> warning,
		KeyAuthorization auth,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> unblock,
		std::optional<QByteArray> parkedKey,
		Fn<void(std::vector<QString>)> onWords,
		Fn<void()> onAuthorized,
		Fn<void(std::vector<QString>, CustodyOutcome)> onPrepared,
		Fn<void()> onPromptError,
		Fn<void()> onPasswordMissing) {
	auto &wallet = show->session().wallet();
	if (!parkedKey && passcode && wallet.revealsLocally()) {
		const auto box = base::take(passcode);
		password.reset();
		box->closeBox();
	}
	const auto done = crl::guard(warning, [=](
			std::vector<QString> words,
			CustodyOutcome outcome) {
		if (onPrepared) {
			onPrepared(std::move(words), outcome);
			return;
		}
		if (passcode) {
			passcode->closeBox();
		}
		if (onWords) {
			onWords(std::move(words));
		} else {
			warning->closeBox();
			show->showBox(Box(WalletPhraseBox, show, std::move(words)));
		}
		if (outcome != CustodyOutcome::Installed) {
			show->showToast(tr::lng_wallet_restore_not_saved(tr::now));
		}
	});
	const auto fail = crl::guard(warning, [=](const QString &error) {
		if (passcode && passcode->handleCustomCheckError(error)) {
			passcode->showLoading(false);
			if (onPromptError) {
				onPromptError();
			} else {
				unblock();
			}
			return;
		}
		if (onPasswordMissing && error == u"PASSWORD_MISSING"_q) {
			onPasswordMissing();
			return;
		}
		unblock();
		if (!onWords && !onPrepared) {
			warning->closeBox();
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
		ShowPhraseError(show, PhraseOperation::Reveal, error);
	});
	auto authorized = Fn<void()>();
	if (onAuthorized) {
		authorized = crl::guard(warning, [=] {
			onAuthorized();
			if (passcode) {
				passcode->closeBox();
			}
		});
	}
	if (parkedKey) {
		wallet.revealParked(std::move(auth), *parkedKey, done, fail);
	} else {
		wallet.revealPhrase(
			std::move(auth),
			std::move(password),
			done,
			fail,
			std::move(authorized));
	}
}

void StartPhraseReveal(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> warning,
		KeyAuthorization auth,
		Fn<void()> unblock,
		std::optional<QByteArray> parkedKey,
		Fn<void(std::vector<QString>)> onWords,
		Fn<void()> onAuthorized,
		Fn<void(std::vector<QString>, CustodyOutcome)> onPrepared,
		Fn<void()> onPromptError,
		Fn<void()> onPromptClosed,
		Fn<bool()> onPromptSubmit) {
	const auto session = &show->session();
	// A parked key or a key held on this device goes straight to the words:
	// neither asks the server, so neither can be asked for a password, and
	// neither reports the backup's answer or hands its words over prepared,
	// because only a key restored from Telegram's backup is stored.
	if (parkedKey || session->wallet().revealsLocally()) {
		RequestPhraseReveal(
			show,
			warning,
			std::move(auth),
			std::nullopt,
			nullptr,
			unblock,
			parkedKey,
			onWords);
		return;
	}
	// Only this branch restores the key to the device, so only its password
	// box carries the first-use explanation header.
	const auto firstKeyUse = RestoreIsFirstKeyUse(session);
	const auto askPassword = crl::guard(warning, [=] {
		const auto cached = session->api().cloudPassword().stateCurrent();
		LOG(("Wallet Info: phrase reveal requires password; "
			"cached_state=%1 cached_has_password=%2."
			).arg(cached.has_value()).arg(cached && cached->hasPassword));
		session->api().cloudPassword().reload();
		session->api().cloudPassword().state(
		) | rpl::take(
			1
		) | rpl::on_next([=](const Core::CloudPasswordState &state) {
			if (!state.hasPassword) {
				// The server asked for a password this account does not
				// have, so a repeat without one would only be refused again.
				unblock();
				if (!onWords && !onPrepared) {
					warning->closeBox();
				}
				ShowPhraseError(
					show,
					PhraseOperation::Reveal,
					u"PHRASE_PASSWORD_STATE_MISSING"_q);
				return;
			}
			auto fields = RestorePasswordFields(
				state,
				firstKeyUse,
				tr::lng_wallet_phrase_password_description(tr::now));
			fields.customCheckCallback = [=](
					const Core::CloudPasswordResult &result,
					base::weak_qptr<PasscodeBox> passcode) {
				if (onPromptSubmit && !onPromptSubmit()) {
					return;
				}
				if (passcode) {
					passcode->showLoading(true);
				}
				RequestPhraseReveal(
					show,
					warning,
					auth,
					result,
					passcode,
					unblock,
					std::nullopt,
					onWords,
					onAuthorized,
					onPrepared,
					onPromptError);
			};
			const auto passcode = show->show(
				Box<PasscodeBox>(session, fields));
			if (passcode) {
				passcode->boxClosing(
				) | rpl::on_next([=] {
					if (onPromptClosed) {
						onPromptClosed();
					}
				}, warning->lifetime());
			}
		}, warning->lifetime());
	});
	RequestPhraseReveal(
		show,
		warning,
		std::move(auth),
		std::nullopt,
		nullptr,
		unblock,
		std::nullopt,
		onWords,
		onAuthorized,
		onPrepared,
		onPromptError,
		askPassword);
}

void WalletPhraseWarningBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::optional<QByteArray> parkedKey) {
	enum class Phase {
		Idle,
		Starting,
		Authorizing,
		Loading,
		Ready,
	};
	struct State {
		Phase phase = Phase::Idle;
		rpl::variable<bool> loading = false;
		bool armed = false;
		bool pointerDown = false;
		bool absorbEnter = false;
		std::optional<std::vector<QString>> words;
		CustodyOutcome outcome = CustodyOutcome::Installed;
	};
	const auto state = box->lifetime().make_state<State>();
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::walletPillBox);
	box->setNoContentMargin(true);

	AddPhraseBoxHeader(
		box,
		u"wallet/paper"_q,
		tr::lng_wallet_phrase_intro_title(),
		tr::lng_wallet_phrase_intro_text(tr::marked),
		st::walletPhraseIntroTextLabel,
		st::walletCoverLottieSize,
		st::walletPhraseLottieMargin,
		st::walletPhraseTextMargin);

	const auto container = box->verticalLayout();
	const auto addWarning = [&](
			rpl::producer<TextWithEntities> text,
			const style::icon &icon) {
		const auto label = container->add(
			object_ptr<Ui::FlatLabel>(
				container,
				std::move(text),
				st::walletPhraseWarnLabel),
			st::walletPhraseWarnPadding);
		const auto left = Ui::CreateChild<Ui::RpWidget>(container);
		left->paintRequest(
		) | rpl::on_next([=] {
			auto p = Painter(left);
			icon.paint(p, 0, 0, left->width());
		}, left->lifetime());
		left->resize(icon.size());
		label->geometryValue(
		) | rpl::on_next([=](const QRect &g) {
			left->moveToLeft(
				(g.left() - left->width()) / 2,
				g.top() + st::walletPhraseWarnIconSkip);
		}, left->lifetime());
	};
	Ui::AddSkip(container);
	addWarning(
		tr::lng_wallet_phrase_warn_share(tr::rich),
		st::walletPhraseWarnShareIcon);
	Ui::AddSkip(container, st::walletPhraseWarnRowSkip);
	addWarning(
		tr::lng_wallet_phrase_warn_steal(tr::rich),
		st::walletPhraseWarnStealIcon);
	Ui::AddSkip(container, st::walletPhraseWarnRowSkip);
	addWarning(
		tr::lng_wallet_phrase_warn_support(tr::rich),
		st::walletPhraseWarnSupportIcon);
	Ui::AddSkip(container);

	AddBoxCloseButton(box);
	const auto idleFromPrompt = [=] {
		if (state->phase == Phase::Starting
			|| state->phase == Phase::Authorizing) {
			state->phase = Phase::Idle;
			state->loading = false;
			state->armed = false;
			state->absorbEnter = false;
			state->words.reset();
		}
	};
	const auto idleFromFail = [=] {
		if (state->phase == Phase::Ready) {
			return;
		}
		state->phase = Phase::Idle;
		state->loading = false;
		state->armed = false;
		state->absorbEnter = false;
		state->words.reset();
	};
	const auto button = box->addButton(BusyFooterLabel(
		tr::lng_wallet_keys_show_phrase(),
		state->loading.value()));
	button->setClickedCallback([=] {
		if (state->phase == Phase::Authorizing
			|| state->phase == Phase::Loading
			|| state->phase == Phase::Starting) {
			return;
		} else if (state->phase == Phase::Ready) {
			if (!state->armed || !state->words) {
				return;
			}
			auto words = *state->words;
			state->words.reset();
			box->closeBox();
			show->showBox(Box(WalletPhraseBox, show, std::move(words)));
			return;
		}
		state->phase = Phase::Starting;
		state->loading = true;
		const auto start = [=](KeyAuthorization auth) {
			StartPhraseReveal(
				show,
				box,
				std::move(auth),
				idleFromFail,
				parkedKey,
				nullptr,
				[=] {
					if (state->phase != Phase::Starting
						&& state->phase != Phase::Authorizing) {
						return;
					}
					state->phase = Phase::Loading;
					state->armed = false;
					state->loading = true;
				},
				[=](std::vector<QString> words, CustodyOutcome outcome) {
					if (state->phase != Phase::Loading) {
						return;
					}
					// A stored key always went through the protection
					// chooser, and its Save is the explicit activation that
					// shows the words right away: nothing typed into the
					// password box can carry past it. A store that was
					// cancelled or failed keeps the words behind a fresh
					// press of this button instead.
					if (outcome == CustodyOutcome::Installed) {
						state->phase = Phase::Idle;
						state->loading = false;
						box->closeBox();
						show->showBox(
							Box(WalletPhraseBox, show, std::move(words)));
						return;
					}
					state->words = std::move(words);
					state->outcome = outcome;
					state->phase = Phase::Ready;
					state->armed = !state->pointerDown;
					state->absorbEnter = true;
					state->loading = false;
					if (const auto strong = button.data()) {
						strong->clearState();
					}
					if (outcome != CustodyOutcome::Installed) {
						show->showToast(
							tr::lng_wallet_restore_not_saved(tr::now));
					}
				},
				[=] {
					if (state->phase == Phase::Authorizing) {
						state->phase = Phase::Starting;
					}
				},
				idleFromPrompt,
				[=] {
					if (state->phase == Phase::Authorizing
						|| state->phase == Phase::Loading
						|| state->phase == Phase::Ready) {
						return false;
					}
					state->phase = Phase::Authorizing;
					return true;
				});
		};
		// A key restored from Telegram's backup is stored through the install
		// ladder, whose chooser asks for the vault itself, so that path
		// carries no read grant and asks nothing before the backup is
		// fetched: the cloud password box comes only when the server asks
		// for it. A key held on this device is read right after its unlock,
		// whose submit is the explicit activation, as the chooser's Save is.
		if (!parkedKey && !show->session().wallet().revealsLocally()) {
			start(KeyAuthorization{ .install = MakeCustodyInstaller(show) });
			return;
		}
		AcquireVaultUnlock({
			.show = show,
			.done = crl::guard(box, [=](KeyAuthorization auth) {
				if (state->phase != Phase::Starting) {
					return;
				} else if (!auth.valid()) {
					idleFromPrompt();
					return;
				}
				start(std::move(auth));
			}),
		});
	});
	AddBusyFooterSpinner(button, state->loading.value());
	const auto isRevealKey = [](int key) {
		return key == Qt::Key_Return
			|| key == Qt::Key_Enter
			|| key == Qt::Key_Space;
	};
	const auto armAfterRelease = [=] {
		crl::on_main(box, [=] {
			if (state->phase == Phase::Ready && !state->pointerDown) {
				state->armed = true;
			}
		});
	};
	const auto filterRevealKey = [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type != QEvent::KeyPress && type != QEvent::KeyRelease) {
			return base::EventFilterResult::Continue;
		}
		const auto keyEvent = static_cast<QKeyEvent*>(e.get());
		if (!isRevealKey(keyEvent->key())) {
			return base::EventFilterResult::Continue;
		}
		if (type == QEvent::KeyRelease) {
			state->absorbEnter = false;
			if (state->phase == Phase::Ready) {
				armAfterRelease();
			}
			return base::EventFilterResult::Continue;
		}
		if (state->phase == Phase::Ready
			&& (state->absorbEnter || keyEvent->isAutoRepeat())) {
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	};
	base::install_event_filter(button.data(), [=](not_null<QEvent*> e) {
		const auto type = e->type();
		if (type == QEvent::MouseButtonPress) {
			state->pointerDown = true;
			if (state->phase == Phase::Ready) {
				state->absorbEnter = false;
				state->armed = true;
			}
		} else if (type == QEvent::MouseButtonRelease) {
			state->pointerDown = false;
			if (state->phase == Phase::Ready) {
				armAfterRelease();
			}
		}
		return filterRevealKey(e);
	});
	base::install_event_filter(box, [=](not_null<QEvent*> e) {
		return filterRevealKey(e);
	});
	SubmitBoxOnEnter(box, [=] {
		if (const auto strong = button.data()) {
			strong->clicked(Qt::KeyboardModifiers(), Qt::LeftButton);
		}
	});
	show->session().wallet().transferWalletIdentityChanges(
	) | rpl::on_next([=] {
		state->words.reset();
		state->armed = false;
		state->absorbEnter = false;
		if (state->phase != Phase::Idle) {
			state->phase = Phase::Idle;
			state->loading = false;
		}
	}, box->lifetime());
}

// The warning sheet comes first and its Show press acquires what the reveal
// needs: the vault unlock for a key held on this device, so the box order
// is warning, passcode, phrase, or warning, phrase for an open or retained
// vault; the install ladder for a key restored from Telegram's backup, with
// the cloud password box before it when the server asks for one, and the
// chooser at the store.
void WalletRevealFlow(
		std::shared_ptr<Main::SessionShow> show,
		std::optional<QByteArray> parkedKey) {
	show->showBox(Box(WalletPhraseWarningBox, show, parkedKey));
}

} // namespace ContentDetails

} // namespace Wallet
