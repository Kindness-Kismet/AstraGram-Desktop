#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

void ShowBackupChangeError(
		std::shared_ptr<Main::SessionShow> show,
		const QString &error) {
	if (error == u"BACKUP_VAULT_LOCKED"_q) {
		show->showToast(tr::lng_wallet_vault_locked(tr::now));
	} else if (auto box = PrePasswordErrorBox(
			error,
			&show->session(),
			EnforcementCheckAbout(error))) {
		show->showBox(std::move(box));
	} else if (error == u"BACKUP_PHRASE_OUTDATED"_q) {
		ShowKeyChangedBox(show, tr::lng_wallet_send_key_changed_text());
	} else if (error == u"BACKUP_KEY_UNCONFIRMED"_q) {
		show->showToast(tr::lng_wallet_import_key_changing(tr::now));
	} else if (error == u"BACKUP_NOT_VERIFIED"_q) {
		show->showToast(tr::lng_wallet_import_not_verified(tr::now));
	} else {
		show->showToast((error == u"WALLET_BACKUP_NOT_AVAILABLE"_q)
			? tr::lng_wallet_backup_about_unavailable(tr::now)
			: ErrorWithType(tr::lng_wallet_backup_error(tr::now), error));
	}
}

void RequestBackupChange(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		std::optional<Core::CloudPasswordResult> password,
		base::weak_qptr<PasscodeBox> passcode,
		Fn<void()> unblock,
		Fn<void()> done,
		std::optional<BackupDisableProof> proof) {
	const auto succeeded = crl::guard(origin, [=] {
		unblock();
		if (passcode) {
			passcode->closeBox();
		}
		done();
	});
	const auto fail = crl::guard(origin, [=](const QString &error) {
		unblock();
		if (passcode && passcode->handleCustomCheckError(error)) {
			return;
		} else if (passcode) {
			passcode->closeBox();
		}
		ShowBackupChangeError(show, error);
	});
	auto &wallet = show->session().wallet();
	if (proof) {
		wallet.disableBackupWithProof(
			std::move(proof->auth),
			std::move(proof->approved),
			succeeded,
			fail);
	} else {
		wallet.disableBackup(std::move(password), succeeded, fail);
	}
}

void StartBackupRequest(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		Fn<void()> unblock,
		Fn<void()> done) {
	const auto session = &show->session();
	session->api().cloudPassword().reload();
	session->api().cloudPassword().state(
	) | rpl::take(
		1
	) | rpl::on_next([=](const Core::CloudPasswordState &state) {
		if (!state.hasPassword) {
			RequestBackupChange(
				show,
				origin,
				std::nullopt,
				nullptr,
				unblock,
				done);
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
			RequestBackupChange(
				show,
				origin,
				result,
				passcode,
				unblock,
				done);
		};
		show->showBox(Box<PasscodeBox>(session, fields));
		unblock();
	}, origin->lifetime());
}

// A usable local copy of the served wallet's current key proves ownership,
// so no cloud password is asked. Without one (the served key moved since the
// phrase was shown, or the hardware wrap cannot be opened) no proof can be
// made and the cloud password route stays as before. A locked vault still
// has usable custody: the session reports it locked and asks no password.
void RequestBackupDisable(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		BackupDisableProof proof,
		Fn<void()> unblock,
		Fn<void()> done) {
	if (!show->session().wallet().revealsLocally()) {
		StartBackupRequest(
			show,
			origin,
			std::move(unblock),
			std::move(done));
		return;
	}
	RequestBackupChange(
		show,
		origin,
		std::nullopt,
		nullptr,
		std::move(unblock),
		std::move(done),
		std::move(proof));
}

void ShowBackupEnabledToast(std::shared_ptr<Main::SessionShow> show) {
	show->showToast({
		.title = tr::lng_wallet_backup_enabled_title(tr::now),
		.text = { tr::lng_wallet_backup_enabled_text(tr::now) },
		.icon = &st::toastCheckIcon,
	});
}

void StartBackupEnable(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy) {
	const auto upload = crl::guard(origin, [=](KeyAuthorization auth) {
		if (*busy) {
			return;
		}
		*busy = true;
		show->session().wallet().enableBackup(
			std::move(auth),
			crl::guard(origin, [=] {
				*busy = false;
				ShowBackupEnabledToast(show);
			}),
			crl::guard(origin, [=](const QString &error) {
				*busy = false;
				ShowBackupChangeError(show, error);
			}));
	});
	// The restorable and not-restorable arms install custody first and only
	// then run this, so the acquisition sits after them and every arm reaches
	// enableBackup with a live grant.
	RunKeyRequiringAction(show, crl::guard(origin, [=] {
		if (*busy) {
			return;
		}
		AcquireVaultUnlock({
			.show = show,
			.done = [=](KeyAuthorization auth) {
				if (auth.valid()) {
					upload(std::move(auth));
				}
			},
		});
	}), KeyActionKind::ResumeAfterRestore);
}

// WHY: dismissing the write-down or its quiz silently drops the disable,
// so while it still would, the dismissal asks first; no abandons = always.
void GuardBackupDisableDismiss(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		Fn<bool()> abandons) {
	struct State {
		base::weak_qptr<Ui::GenericBox> confirmation;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto abandoning = [=] {
		return !abandons || abandons();
	};
	const auto confirm = [=] {
		if (state->confirmation) {
			return;
		}
		state->confirmation = show->show(Ui::MakeConfirmBox({
			.text = tr::lng_wallet_backup_cancel_text(),
			.confirmed = crl::guard(box, [=](Fn<void()> close) {
				close();
				box->closeBox();
			}),
			.confirmText = tr::lng_box_yes(),
			.cancelText = tr::lng_box_no(),
			.title = tr::lng_wallet_backup_cancel_title(),
		}));
	};
	box->boxClosing() | rpl::on_next([=] {
		if (const auto strong = state->confirmation.get()) {
			strong->closeBox();
		}
	}, box->lifetime());
	AddBoxCloseButton(box, [=] {
		if (abandoning()) {
			confirm();
		} else {
			box->closeBox();
		}
	});
	const auto isEscape = [](not_null<QEvent*> e) {
		return (e->type() == QEvent::KeyPress)
			&& (static_cast<QKeyEvent*>(e.get())->key() == Qt::Key_Escape);
	};
	base::install_event_filter(box, [=](not_null<QEvent*> e) {
		if (!isEscape(e) || !abandoning()) {
			return base::EventFilterResult::Continue;
		}
		confirm();
		return base::EventFilterResult::Cancel;
	});
	box->showFinishes() | rpl::take(1) | rpl::on_next([=] {
		const auto layer = box->parentWidget();
		const auto stack = BoxLayerStack(box);
		if (!layer || !stack) {
			return;
		}
		base::install_event_filter(box, stack, [=](not_null<QEvent*> e) {
			const auto dismissal = (e->type() == QEvent::MouseButtonPress)
				|| isEscape(e);
			if (!dismissal || layer->isHidden() || !abandoning()) {
				return base::EventFilterResult::Continue;
			}
			Ui::PostponeCall(box, confirm);
			return base::EventFilterResult::Cancel;
		});
	}, box->lifetime());
}

void WalletBackupPhraseBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::vector<QString> words,
		Fn<void(std::vector<QString>)> next,
		rpl::producer<QString> title,
		rpl::producer<TextWithEntities> text) {
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	AddPhraseBoxHeader(
		box,
		PhraseBoxLottie(int(words.size())),
		std::move(title),
		std::move(text),
		st::walletPhraseTextLabel,
		st::walletCoverLottieSize,
		st::walletPhraseLottieMargin,
		st::walletPhraseTextMargin);

	AddPhraseGrid(box, words);

	GuardBackupDisableDismiss(box, show, nullptr);
	const auto shownAt = crl::now();
	box->addButton(tr::lng_continue(), [=] {
		if (crl::now() - shownAt < kBackupWriteDownDelay) {
			show->showBox(Ui::MakeInformBox({
				.text = tr::lng_wallet_backup_sure_text(tr::now),
				.confirmText = tr::lng_box_ok(),
				.title = tr::lng_wallet_backup_sure_title(),
			}));
			return;
		}
		next(words);
		box->closeBox();
	});
}

// WHY: a rotated phrase carries two independent keys - the first 12 words
// restore the anchor and the last 12 the signing key - so the quiz proves
// both halves were written down, not three words of whichever half.
[[nodiscard]] std::vector<int> BackupQuizIndices(int count) {
	auto result = std::vector<int>();
	result.reserve(kBackupQuizWordCount);
	if (count >= kImportWordCountLong) {
		const auto signing = count - kImportWordCountShort;
		const auto first = base::RandomIndex(signing);
		const auto second = base::RandomIndex(signing - 1);
		result.push_back(base::RandomIndex(kImportWordCountShort));
		result.push_back(kImportWordCountShort + first);
		result.push_back(kImportWordCountShort
			+ second
			+ ((second >= first) ? 1 : 0));
	} else {
		while (int(result.size()) < kBackupQuizWordCount) {
			const auto index = base::RandomIndex(count);
			if (!ranges::contains(result, index)) {
				result.push_back(index);
			}
		}
	}
	ranges::sort(result);
	return result;
}

[[nodiscard]] rpl::producer<TextWithEntities> BackupQuizText(
		const std::vector<int> &indices) {
	const auto number = [](int index) {
		return rpl::single(tr::bold(QString::number(index + 1)));
	};
	return tr::lng_wallet_backup_test_text(
		lt_index1,
		number(indices[0]),
		lt_index2,
		number(indices[1]),
		lt_index3,
		number(indices[2]),
		tr::marked);
}

[[nodiscard]] not_null<Ui::InputField*> AddBackupQuizField(
		not_null<Ui::VerticalLayout*> container,
		int index) {
	const auto field = container->add(
		object_ptr<Ui::InputField>(
			container,
			st::walletBackupQuizField,
			Ui::InputField::Mode::SingleLine),
		st::walletImportFieldMargin);
	const auto number = Ui::CreateChild<Ui::FlatLabel>(
		field,
		QString::number(index + 1) + QChar('.'),
		st::walletPhraseNumberLabel);
	number->setAttribute(Qt::WA_TransparentForMouseEvents);
	field->widthValue(
	) | rpl::on_next([=](int width) {
		number->moveToLeft(
			st::walletImportNumberLeft,
			st::walletImportNumberTop,
			width);
	}, field->lifetime());
	return field;
}

[[nodiscard]] bool BackupQuizAnswerMatches(
		not_null<Ui::InputField*> field,
		const QString &word) {
	const auto entered = field->getLastText().trimmed();
	return (entered.compare(word.trimmed(), Qt::CaseInsensitive) == 0);
}

void WalletBackupQuizBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		std::vector<QString> words,
		Fn<void()> passed,
		Fn<void(Fn<void()> lock)> publishLock,
		Fn<bool()> abandons) {
	Expects(int(words.size()) >= kBackupQuizWordCount);

	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	struct State {
		std::vector<Ui::InputField*> fields;
		std::vector<int> indices;
		std::vector<bool> wrong;
	};
	const auto state = box->lifetime().make_state<State>();
	state->indices = BackupQuizIndices(int(words.size()));
	state->wrong.resize(kBackupQuizWordCount, false);

	AddPhraseBoxHeader(
		box,
		u"wallet/test"_q,
		tr::lng_wallet_backup_test_title(),
		BackupQuizText(state->indices),
		st::walletPhraseTextLabel,
		st::walletPhraseGridLottieSize,
		st::walletPhraseGridLottieMargin,
		st::walletPhraseGridTextMargin);

	const auto container = box->verticalLayout();
	Ui::AddSkip(container, st::walletBackupQuizFieldsTopSkip);
	for (const auto index : state->indices) {
		state->fields.push_back(AddBackupQuizField(container, index));
	}
	Ui::AddSkip(container, st::walletBackupQuizFieldsBottomSkip);

	GuardBackupDisableDismiss(box, show, std::move(abandons));
	const auto button = box->addButton(tr::lng_continue());
	const auto allFilled = [=] {
		return ranges::all_of(state->fields, [](Ui::InputField *field) {
			return !field->getLastText().trimmed().isEmpty();
		});
	};
	const auto anyWrong = [=] {
		return ranges::contains(state->wrong, true);
	};
	const auto refreshButton = [=] {
		if (const auto raw = button.data()) {
			SetButtonDisabledLook(raw, !allFilled() || anyWrong());
		}
	};
	const auto submit = [=] {
		if (!allFilled() || anyWrong()) {
			return;
		}
		for (auto i = 0; i != kBackupQuizWordCount; ++i) {
			const auto field = state->fields[i];
			if (!BackupQuizAnswerMatches(field, words[state->indices[i]])) {
				field->showError();
				state->wrong[i] = true;
			}
		}
		if (anyWrong()) {
			refreshButton();
		} else {
			passed();
		}
	};
	button->setClickedCallback(submit);
	refreshButton();

	for (auto i = 0; i != kBackupQuizWordCount; ++i) {
		const auto field = state->fields[i];
		field->changes() | rpl::on_next([=] {
			state->wrong[i] = false;
			refreshButton();
		}, field->lifetime());
		field->submits() | rpl::on_next([=] {
			if (i + 1 < kBackupQuizWordCount) {
				state->fields[i + 1]->setFocus();
			} else {
				submit();
			}
		}, field->lifetime());
	}
	box->setFocusCallback([=] {
		state->fields.front()->setFocusFast();
	});
	if (publishLock) {
		publishLock([=] {
			for (const auto field : state->fields) {
				field->setDisabled(true);
			}
			// A disabled field takes no focus, and with the focus left on
			// the layer stack Escape closes the box past
			// setCloseByEscape(false), so the box holds it itself.
			box->setFocusCallback(nullptr);
			box->setInnerFocus();
		});
	}
}

void ShowBackupDisabledToast(std::shared_ptr<Main::SessionShow> show) {
	show->showToast({
		.title = tr::lng_wallet_backup_disabled_title(tr::now),
		.text = { tr::lng_wallet_backup_disabled_text(tr::now) },
		.icon = &st::toastCheckIcon,
	});
}

void CollectBackupPhrase(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		KeyAuthorization auth) {
	const auto approved = show->session().wallet().backupDisableApproval();
	const auto proof = BackupDisableProof{
		.auth = auth,
		.approved = approved.value_or(BackupDisableApproval()),
	};
	const auto showQuiz = [=](std::vector<QString> words) {
		const auto quiz = std::make_shared<base::weak_qptr<Ui::GenericBox>>();
		const auto requesting = std::make_shared<bool>(false);
		*quiz = show->show(Box(WalletBackupQuizBox, show, words, [=] {
			const auto strong = quiz->get();
			if (!strong || *requesting) {
				return;
			}
			*requesting = true;
			RequestBackupDisable(
				show,
				strong,
				proof,
				[=] { *requesting = false; },
				[=] {
					if (const auto strong = quiz->get()) {
						strong->closeBox();
					}
					ShowBackupDisabledToast(show);
				});
		}, nullptr, [=] {
			return !*requesting;
		}));
	};
	const auto showPhrase = [=](std::vector<QString> words) {
		const auto count = int(words.size());
		show->showBox(Box(
			WalletBackupPhraseBox,
			show,
			words,
			showQuiz,
			tr::lng_wallet_phrase_title(),
			tr::lng_wallet_phrase_text(
				lt_count,
				rpl::single(count * 1.) | tr::to_count(),
				tr::marked)));
	};
	StartPhraseReveal(
		show,
		origin,
		std::move(auth),
		[] {},
		std::nullopt,
		showPhrase);
}

[[nodiscard]] QString RotationFeeText(
		tr::phrase<lngtag_amount, lngtag_fiat> phrase,
		int64 feeNano,
		const FiatRate &rate) {
	return phrase(
		tr::now,
		lt_amount,
		Ui::FormatTonAmount(feeNano).full,
		lt_fiat,
		FormatFiat(feeNano, rate, kFeeFiatDecimals, true));
}

[[nodiscard]] QString RotationFailureReason(const QString &error) {
	if (error == u"ROTATION_FEES"_q) {
		return tr::lng_wallet_send_error_insufficient(tr::now);
	} else if (error == u"ROTATION_ALREADY_SENDING"_q) {
		return tr::lng_wallet_send_error_in_progress(tr::now);
	} else if (error == u"ROTATION_VAULT_LOCKED"_q) {
		return tr::lng_wallet_vault_locked(tr::now);
	}
	return tr::lng_wallet_backup_rotate_reason_failed(tr::now);
}

void ShowRotationFailedToast(
		std::shared_ptr<Main::SessionShow> show,
		const QString &error) {
	show->showToast({
		.title = tr::lng_wallet_backup_rotate_failed_title(tr::now),
		.text = { tr::lng_wallet_backup_rotate_failed_text(
			tr::now,
			lt_reason,
			RotationFailureReason(error)) },
		.icon = &st::toastCheckIcon,
	});
}

void SetBoxBusy(not_null<Ui::GenericBox*> box) {
	box->clearButtons();
	const auto button = box->addButton(rpl::single(QString()));
	SetButtonDisabledLook(button.data(), true);
	AddBusyFooterSpinner(button, rpl::single(true));
	box->setCloseByOutsideClick(false);
	box->setCloseByEscape(false);
}

void SubmitRotation(
		std::shared_ptr<Main::SessionShow> show,
		base::weak_qptr<Ui::GenericBox> origin,
		not_null<bool*> busy,
		std::shared_ptr<RotationState> state) {
	const auto quiz = state->quiz.get();
	if (!quiz || state->submitted) {
		return;
	}
	state->submitted = true;
	state->lockQuiz();
	SetBoxBusy(quiz);
	const auto closeQuiz = [=] {
		if (const auto quiz = state->quiz.get()) {
			quiz->closeBox();
		}
	};
	show->session().wallet().submitRotation(state->auth, [=] {
		closeQuiz();
		const auto strong = origin.get();
		if (!strong) {
			return;
		}
		*busy = true;
		const auto approved = show->session().wallet().backupDisableApproval();
		RequestBackupDisable(
			show,
			strong,
			BackupDisableProof{
				.auth = state->auth,
				.approved = approved.value_or(BackupDisableApproval()),
			},
			[=] { *busy = false; },
			[=] { ShowBackupDisabledToast(show); });
	}, [=](const QString &error) {
		closeQuiz();
		ShowRotationFailedToast(show, error);
	});
}

void ShowRotationPhrase(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		std::vector<QString> words,
		KeyAuthorization auth) {
	const auto state = std::make_shared<RotationState>();
	state->auth = std::move(auth);
	const auto wallet = &show->session().wallet();
	const auto weak = base::make_weak(origin);
	const auto showQuiz = [=](std::vector<QString> words) {
		const auto quiz = show->show(Box(WalletBackupQuizBox, show, words, [=] {
			SubmitRotation(show, weak, busy, state);
		}, [=](Fn<void()> lock) {
			state->lockQuiz = std::move(lock);
		}, [=] {
			return !state->submitted;
		}));
		state->quiz = quiz;
		quiz->boxClosing() | rpl::on_next([=] {
			if (!state->submitted) {
				wallet->abandonRotation();
			}
		}, quiz->lifetime());
	};
	const auto sheet = show->show(Box(
		WalletBackupPhraseBox,
		show,
		words,
		showQuiz,
		tr::lng_wallet_backup_new_phrase_title(),
		tr::lng_wallet_backup_new_phrase_text(tr::marked)));
	sheet->boxClosing() | rpl::on_next([=] {
		if (!state->quiz && !state->submitted) {
			wallet->abandonRotation();
		}
	}, sheet->lifetime());
}

void StartRotation(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		int64 feeNano,
		KeyAuthorization auth) {
	if (*busy) {
		return;
	}
	*busy = true;
	const auto weak = base::make_weak(origin);
	show->session().wallet().prepareRotation(auth, feeNano, [=](
			std::vector<QString> words) {
		const auto strong = weak.get();
		if (!strong) {
			show->session().wallet().abandonRotation();
			return;
		}
		*busy = false;
		ShowRotationPhrase(
			show,
			strong,
			busy,
			std::move(words),
			auth);
	}, crl::guard(origin, [=](const QString &error) {
		*busy = false;
		ShowRotationFailedToast(show, error);
	}));
}

void ShowBackupTopUpAlert(
		std::shared_ptr<Main::SessionShow> show,
		int64 feeNano,
		Fn<void()> topUp) {
	const auto rate = show->session().wallet().rates().current();
	show->showBox(Ui::MakeConfirmBox({
		.text = RotationFeeText(
			tr::lng_wallet_backup_topup_text,
			feeNano,
			rate),
		.confirmed = [=](Fn<void()> close) {
			close();
			topUp();
			ShowWalletReceiveBox(&show->session(), show);
		},
		.confirmText = tr::lng_credits_buy_button_short(),
		.cancelText = tr::lng_export_suggest_cancel(),
		.title = tr::lng_wallet_backup_topup_title(),
	}));
}

[[nodiscard]] bool RotationQuoteUsable(
		const std::optional<RotationQuote> &quote) {
	return quote
		&& (quote->error == SendError::None
			|| quote->error == SendError::InsufficientFees);
}

// What a phrase update does, then its network fee, or the top-up that fee
// needs when the balance does not cover it.
[[nodiscard]] rpl::producer<TextWithEntities> BackupUpdateNoteText(
		not_null<Main::Session*> session,
		rpl::producer<std::optional<RotationQuote>> quote) {
	return rpl::combine(
		std::move(quote),
		FiatRateValue(session)
	) | rpl::map([](
			const std::optional<RotationQuote> &quote,
			const FiatRate &rate) {
		auto result = tr::lng_wallet_backup_update_text(tr::now, tr::marked);
		if (RotationQuoteUsable(quote)) {
			const auto phrase = (quote->error == SendError::None)
				? tr::lng_wallet_backup_update_fee
				: tr::lng_wallet_backup_topup_text;
			result.append(u"\n\n"_q).append(tr::marked(
				RotationFeeText(phrase, quote->feeNano, rate)));
		}
		return result;
	});
}

void AddBackupUpdateNote(
		not_null<Ui::VerticalLayout*> container,
		rpl::producer<TextWithEntities> text,
		rpl::producer<bool> shown) {
	const auto wrap = container->add(
		object_ptr<Ui::SlideWrap<Ui::PaddingWrap<Ui::FlatLabel>>>(
			container,
			object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
				container,
				object_ptr<Ui::FlatLabel>(
					container,
					std::move(text),
					st::walletBackupNoteLabel),
				st::walletBackupNotePadding),
			st::walletBackupNoteMargin),
		st::boxRowPadding);
	// The plate is the note's own padding wrap: the slide wrap keeps one
	// more around it for the outer margins, and entity() would unwrap all
	// the way down to the label.
	const auto plate = wrap->wrapped()->wrapped();
	plate->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(plate);
		auto hq = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(st::walletBackupNoteBg);
		p.drawRoundedRect(
			plate->rect(),
			st::walletBackupNoteRadius,
			st::walletBackupNoteRadius);
	}, plate->lifetime());
	wrap->toggleOn(std::move(shown));
	wrap->finishAnimating();
}

// One confirmation for the whole disable: its checkbox decides whether the
// key is rotated on the way, and the fee for that is quoted in the
// background from the moment the box shows. The checkbox slides in only
// once the box has finished showing and the quote is usable, and a quote
// that failed leaves no update to offer. A press before the quote waits
// for it, because the reveal refuses to run beside it, and then goes to the
// write-down of the current words.
void WalletBackupDisableBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		base::weak_qptr<Ui::GenericBox> origin,
		not_null<bool*> busy,
		std::shared_ptr<BackupDisableState> state,
		bool updateOffered) {
	const auto session = &show->session();
	box->setTitle(tr::lng_wallet_backup_disable_title());
	const auto padding = st::boxPadding;
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_backup_disable_text(),
			st::boxLabel),
		QMargins(padding.left(), 0, padding.right(), padding.bottom()));
	auto update = (Ui::Checkbox*)nullptr;
	if (updateOffered) {
		const auto wrap = box->addRow(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				box,
				object_ptr<Ui::VerticalLayout>(box)),
			QMargins());
		const auto inner = wrap->entity();
		update = inner->add(
			object_ptr<Ui::Checkbox>(
				inner,
				tr::lng_wallet_backup_update_check(tr::now),
				false,
				st::defaultBoxCheckbox),
			st::walletBackupUpdateCheckboxMargin);
		AddBackupUpdateNote(
			inner,
			BackupUpdateNoteText(session, state->quote.value()),
			update->checkedValue());
		wrap->hide(anim::type::instant);
		box->showFinishes() | rpl::take(1) | rpl::map([=] {
			return state->quote.value();
		}) | rpl::flatten_latest(
		) | rpl::filter([=](const std::optional<RotationQuote> &quote) {
			return RotationQuoteUsable(quote);
		}) | rpl::take(1) | rpl::on_next([=] {
			if (!state->started && !state->loading.current()) {
				wrap->show(anim::type::normal);
			}
		}, box->lifetime());
	}
	// The continuation a press leaves for a quote still out must not own
	// the state that stores it, or a quote that never answers - the engine
	// drops its callbacks at logout - would leak the state and its grant.
	const auto weakState = std::weak_ptr(state);
	const auto proceed = [=] {
		const auto locked = weakState.lock();
		if (!locked) {
			return;
		}
		const auto quote = locked->quote.current();
		Expects(quote.has_value());

		locked->loading = false;
		if (update && RotationQuoteUsable(quote)) {
			update->setDisabled(false);
		}
		if (locked->started) {
			return;
		}
		const auto strong = origin.get();
		if (!locked->rotate) {
			locked->started = true;
			box->closeBox();
			if (strong) {
				CollectBackupPhrase(show, strong, locked->auth);
			}
		} else if (quote->error == SendError::None) {
			locked->started = true;
			box->closeBox();
			if (strong) {
				StartRotation(
					show,
					strong,
					busy,
					quote->feeNano,
					locked->auth);
			}
		} else if (quote->error == SendError::InsufficientFees) {
			ShowBackupTopUpAlert(
				show,
				quote->feeNano,
				crl::guard(box, [=] { box->closeBox(); }));
		}
	};
	const auto submit = [=] {
		if (state->started || state->loading.current()) {
			return;
		}
		state->rotate = update && update->checked();
		if (state->quote.current()) {
			proceed();
			return;
		}
		state->loading = true;
		if (update) {
			update->setDisabled(true);
		}
		state->onQuoted = crl::guard(box, proceed);
	};
	const auto button = box->addButton(
		BusyFooterLabel(
			tr::lng_screen_reader_confirm_disable(),
			state->loading.value()),
		submit,
		st::attentionBoxButton);
	AddBusyFooterSpinner(button, state->loading.value());
	box->addButton(tr::lng_cancel(), [=] { box->closeBox(); });
	box->events(
	) | rpl::on_next([=](not_null<QEvent*> e) {
		if (e->type() != QEvent::KeyPress) {
			return;
		}
		const auto k = static_cast<QKeyEvent*>(e.get());
		if (k->key() == Qt::Key_Enter || k->key() == Qt::Key_Return) {
			submit();
		}
	}, box->lifetime());
}

void ShowBackupDisableBox(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		KeyAuthorization auth) {
	auto &wallet = show->session().wallet();
	const auto state = std::make_shared<BackupDisableState>();
	state->auth = std::move(auth);
	const auto updateOffered = wallet.rotationOffered();
	if (updateOffered) {
		// The quote runs while the box is up, so the fee is most likely
		// known by the time the update checkbox is looked at, and lands in
		// its note the moment the quote answers otherwise. It holds the keys
		// box busy until then: a quote still out refuses every other custody
		// action, a fresh quote beside it first of all, so a confirmation
		// cancelled and reopened over it would only ever see failures.
		*busy = true;
		wallet.quoteRotationFee(state->auth, crl::guard(origin, [=](
				FeeResult fee) {
			*busy = false;
			state->quote = std::make_optional(RotationQuote{
				.feeNano = fee.feeNano,
				.error = fee.error,
			});
			if (const auto onQuoted = base::take(state->onQuoted)) {
				onQuoted();
			}
		}));
	} else {
		state->quote = std::make_optional(RotationQuote{
			.error = SendError::SigningUnavailable,
		});
	}
	show->showBox(Box(
		WalletBackupDisableBox,
		show,
		base::make_weak(origin),
		busy,
		state,
		updateOffered));
}

// The Disable press is where this flow acquires its one authorization: the
// quote, the reveal and the rotation store that follow all run under the
// same grant, and a vault emptied under them fails typed instead of asking
// again in the middle of the write-down. The confirmation comes after it,
// because the quote it starts needs that grant.
void StartBackupDisable(
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> origin,
		not_null<bool*> busy,
		not_null<rpl::variable<bool>*> restoring) {
	const auto weak = base::make_weak(origin);
	const auto confirm = [=] {
		AcquireVaultUnlock({
			.show = show,
			.done = [=](KeyAuthorization auth) {
				const auto strong = weak.get();
				if (!strong) {
					return;
				} else if (!auth.valid()) {
					*busy = false;
					return;
				}
				ShowBackupDisableBox(show, strong, busy, std::move(auth));
			},
		});
	};
	if (show->session().wallet().revealsLocally()) {
		confirm();
		return;
	}
	*busy = true;
	*restoring = true;
	StartCustodyRestore(
		show,
		KeyAuthorization{ .install = MakeCustodyInstaller(show) },
		[=] {
			if (weak) {
				*busy = false;
				*restoring = false;
				confirm();
			}
		},
		crl::guard(origin, [=] {
			*busy = false;
			*restoring = false;
		}));
}

} // namespace ContentDetails

} // namespace Wallet
