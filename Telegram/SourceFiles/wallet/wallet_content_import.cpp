#include "wallet/wallet_content_internal.h"

namespace Wallet {
using namespace ContentDetails;

namespace ContentDetails {

[[nodiscard]] ImportCover SetupImportCover(
		not_null<Ui::GenericBox*> box,
		WalletImportMode mode,
		rpl::producer<QString> about) {
	const auto spacer = box->verticalLayout()->add(
		object_ptr<Ui::RpWidget>(box));
	const auto cover = Ui::CreateChild<Ui::RpWidget>(box.get());
	cover->show();

	struct State {
		std::unique_ptr<Lottie::Icon> icon;
		Ui::FlatLabel *about = nullptr;
		QPainterPath titlePath;
		int fullTitleTop = 0;
		int maxHeight = 0;
		float64 aboutOpacity = -1.;
	};
	const auto state = cover->lifetime().make_state<State>();
	state->icon = Lottie::MakeIcon({
		.name = u"wallet/paper"_q,
		.sizeOverride = QSize(
			st::walletCoverLottieSize,
			st::walletCoverLottieSize),
		.limitFps = true,
	});
	state->about = Ui::CreateChild<Ui::FlatLabel>(
		cover,
		(about
			? std::move(about)
			: (mode == WalletImportMode::Restore)
			? tr::lng_wallet_restore_text()
			: tr::lng_wallet_import_text()),
		st::walletPhraseTextLabel);
	state->about->setAttribute(Qt::WA_TransparentForMouseEvents);

	auto title = (mode == WalletImportMode::Restore)
		? tr::lng_wallet_restore_title()
		: tr::lng_wallet_import_title();
	std::move(title) | rpl::on_next([=](const QString &text) {
		state->titlePath = QPainterPath();
		state->titlePath.addText(
			0,
			st::boxTitle.style.font->ascent,
			st::boxTitle.style.font,
			text);
		cover->update();
	}, cover->lifetime());

	const auto countProgress = [=] {
		return (state->maxHeight > st::boxTitleHeight)
			? std::clamp(
				(cover->height() - st::boxTitleHeight)
					/ float64(state->maxHeight - st::boxTitleHeight),
				0.,
				1.)
			: 1.;
	};
	const auto countBodyOpacity = [](float64 progress) {
		return 1. - std::clamp((1. - progress) / kCoverBodyPart, 0., 1.);
	};
	const auto countArtRect = [=](float64 opacity) {
		const auto side = st::walletCoverLottieSize * opacity;
		return QRectF(
			(cover->width() - side) / 2.,
			st::walletCoverLottieMargin.top() * opacity,
			side,
			side);
	};
	const auto updateScroll = [=] {
		if (state->maxHeight <= st::boxTitleHeight) {
			return;
		}
		const auto height = std::clamp(
			state->maxHeight - box->scrollTop(),
			st::boxTitleHeight,
			state->maxHeight);
		cover->setGeometry(0, 0, box->width(), height);
		const auto opacity = countBodyOpacity(countProgress());
		if (state->aboutOpacity != opacity) {
			state->aboutOpacity = opacity;
			state->about->setOpacity(opacity);
			state->about->moveToLeft(
				st::boxRowPadding.left(),
				int(countArtRect(opacity).bottom())
					+ st::walletCoverLottieMargin.bottom()
					+ st::boxTitleFont->height
					+ st::walletPhraseTextMargin.top());
		}
	};

	const auto relayout = [=] {
		const auto width = box->width();
		if (width <= 0) {
			return;
		}
		state->about->resizeToWidth(width
			- st::boxRowPadding.left()
			- st::boxRowPadding.right());
		state->fullTitleTop = st::walletCoverLottieMargin.top()
			+ st::walletCoverLottieSize
			+ st::walletCoverLottieMargin.bottom();
		state->maxHeight = state->fullTitleTop
			+ st::boxTitleFont->height
			+ st::walletPhraseTextMargin.top()
			+ state->about->height()
			+ st::walletPhraseTextMargin.bottom();
		spacer->resize(width, state->maxHeight);
		updateScroll();
	};
	rpl::combine(
		box->widthValue(),
		state->about->heightValue()
	) | rpl::on_next(relayout, cover->lifetime());

	cover->paintRequest() | rpl::on_next([=] {
		auto p = QPainter(cover);
		p.fillRect(cover->rect(), st::boxBg);
		auto hq = PainterHighQualityEnabler(p);
		const auto progress = countProgress();
		const auto opacity = countBodyOpacity(progress);
		if (opacity > 0.) {
			const auto artRect = countArtRect(opacity);
			const auto frame = state->icon->frame(
				QSize(int(artRect.width()), int(artRect.height())),
				[=] { cover->update(); });
			p.setOpacity(opacity);
			p.drawImage(artRect, frame.image);
		}
		p.setOpacity(1.);
		const auto titleRect = state->titlePath.boundingRect();
		p.translate(
			anim::interpolate(
				(cover->width() - titleRect.width()) / 2,
				st::boxTitlePosition.x(),
				1. - progress),
			anim::interpolate(
				state->fullTitleTop,
				st::boxTitlePosition.y(),
				1. - progress));
		p.translate(titleRect.center());
		const auto scale = 1. + kCoverTitleScale * progress;
		p.scale(scale, scale);
		p.translate(-titleRect.center());
		p.fillPath(state->titlePath, st::boxTitleFg);
	}, cover->lifetime());

	base::install_event_filter(cover, [=](not_null<QEvent*> event) {
		if (event->type() == QEvent::Wheel) {
			box->sendScrollViewportEvent(event);
			return base::EventFilterResult::Cancel;
		}
		return base::EventFilterResult::Continue;
	});

	box->showFinishes() | rpl::on_next([=] {
		const auto icon = state->icon.get();
		const auto update = [=] { cover->update(); };
		if (anim::Disabled()) {
			icon->jumpTo(icon->framesCount() - 1, update);
		} else {
			icon->animate(update, 0, icon->framesCount() - 1);
		}
	}, cover->lifetime());

	return {
		.widget = cover,
		.height = [=] { return cover->height(); },
		.updateScroll = updateScroll,
	};
}

void WalletImportBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		WalletImportMode mode,
		Fn<void()> restored,
		std::shared_ptr<KeyContext> context,
		rpl::producer<QString> about) {
	if (context) {
		context->cancelOnClose(box);
	}
	box->setWidth(st::boxWideWidth);
	box->setStyle(st::giveawayGiftCodeBox);
	box->setNoContentMargin(true);

	struct State {
		std::vector<Ui::InputField*> fields;
		std::vector<Ui::RoundButton*> pasteButtons;
		std::vector<Ui::CrossButton*> clearButtons;
		rpl::variable<int> count = kImportWordCountShort;
		rpl::variable<QString> error;
		std::vector<Ui::AbstractButton*> suggestionRows;
		std::vector<QString> suggestionWords;
		int suggestionField = -1;
		int suggestionSelected = 0;
		int lastFocusedField = -1;
		bool importing = false;
		bool focusRestored = false;
	};
	const auto state = box->lifetime().make_state<State>();

	const auto cover = SetupImportCover(box, mode, std::move(about));

	const auto toggle = box->addRow(
		object_ptr<Ui::SettingsSlider>(box, st::settingsSlider),
		st::walletImportToggleMargin);
	toggle->setSections({
		tr::lng_wallet_import_words(
			tr::now,
			lt_count,
			kImportWordCountShort),
		tr::lng_wallet_import_words(
			tr::now,
			lt_count,
			kImportWordCountLong),
	});
	toggle->setActiveSectionFast(0);

	const auto addWordField = [=](
			not_null<Ui::VerticalLayout*> container,
			int index) {
		const auto field = container->add(
			object_ptr<Ui::InputField>(
				container,
				st::walletImportField,
				Ui::InputField::Mode::SingleLine),
			st::walletImportFieldMargin);
		const auto number = Ui::CreateChild<Ui::FlatLabel>(
			field,
			QString::number(index + 1) + QChar('.'),
			st::walletPhraseNumberLabel);
		number->setAttribute(Qt::WA_TransparentForMouseEvents);
		const auto paste = Ui::CreateChild<Ui::RoundButton>(
			field,
			tr::lng_mac_menu_paste(),
			st::walletImportPaste);
		paste->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
		paste->hide();
		const auto clear = Ui::CreateChild<Ui::CrossButton>(
			field,
			st::walletImportClear);
		field->widthValue(
		) | rpl::on_next([=](int width) {
			number->moveToLeft(
				st::walletImportNumberLeft,
				st::walletImportNumberTop,
				width);
			paste->moveToRight(0, st::walletImportPasteTop);
			clear->moveToRight(
				st::walletImportClearPosition.x(),
				st::walletImportClearPosition.y(),
				width);
		}, field->lifetime());
		state->fields.push_back(field);
		state->pasteButtons.push_back(paste);
		state->clearButtons.push_back(clear);
	};
	for (auto i = 0; i != kImportWordCountShort; ++i) {
		addWordField(box->verticalLayout(), i);
	}
	const auto extraWrap = box->verticalLayout()->add(
		object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
			box->verticalLayout(),
			object_ptr<Ui::VerticalLayout>(box->verticalLayout())));
	const auto extra = extraWrap->entity();
	for (auto i = kImportWordCountShort; i != kImportWordCountLong; ++i) {
		addWordField(extra, i);
	}
	extraWrap->toggleOn(state->count.value(
	) | rpl::map([](int count) { return count == kImportWordCountLong; }));
	extraWrap->finishAnimating();

	const auto error = box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			state->error.value(),
			st::walletImportErrorLabel),
		style::margins(
			st::boxRowPadding.left(),
			st::walletImportErrorSkip,
			st::boxRowPadding.right(),
			0),
		style::al_top);
	error->setVisible(false);
	state->error.value() | rpl::on_next([=](const QString &text) {
		error->setVisible(!text.isEmpty());
	}, error->lifetime());

	AddBoxCloseButton(box);

	const auto wordAt = [=](int index) {
		return state->fields[index]->getLastText().trimmed().toLower();
	};
	const auto requiredCount = [=] {
		const auto count = state->count.current();
		for (auto i = kImportWordCountShort; i != count; ++i) {
			if (!wordAt(i).isEmpty()) {
				return count;
			}
		}
		return kImportWordCountShort;
	};
	const auto markWord = [=](int index, bool typing) {
		const auto field = state->fields[index];
		const auto word = wordAt(index);
		if (word.isEmpty()
			|| (typing
				? !WordlistSuggestions(word, 1).empty()
				: IsWordlistWord(word))) {
			return;
		}
		field->showErrorNoFocus();
		if (typing) {
			field->finishAnimating();
		}
	};
	const auto revealField = [=](not_null<Ui::InputField*> field) {
		const auto top = field->mapTo(box, QPoint()).y() + box->scrollTop();
		box->scrollToY(top - st::boxTitleHeight, top + field->height());
	};
	const auto refreshAccessories = [=](int index) {
		const auto field = state->fields[index];
		const auto focused = field->hasFocus();
		const auto empty = field->getLastText().isEmpty();
		state->pasteButtons[index]->setVisible(focused && empty);
		state->clearButtons[index]->toggle(
			focused && !empty,
			anim::type::instant);
	};
	const auto applyCount = [=](int count) {
		if (state->count.current() != count) {
			state->count = count;
		}
		const auto section = (count == kImportWordCountLong) ? 1 : 0;
		if (toggle->activeSection() != section) {
			toggle->setActiveSection(section);
		}
	};
	const auto distributePaste = [=](const QStringList &words) {
		const auto count = int(words.size());
		if (count != kImportWordCountShort
			&& count != kImportWordCountLong) {
			state->error = tr::lng_wallet_import_paste_error(tr::now);
			return;
		}
		applyCount(count);
		for (auto i = 0; i != count; ++i) {
			state->fields[i]->setText(words[i].toLower());
			state->fields[i]->forceProcessContentsChanges();
		}
		state->error = QString();
		const auto last = state->fields[count - 1];
		crl::on_main(last, [=] {
			last->setFocus();
			last->setCursorPosition(last->getLastText().size());
		});
	};
	const auto submit = [=] {
		if (context && !context->valid()) {
			context->cancel();
			return;
		}
		if (state->importing) {
			return;
		}
		const auto count = requiredCount();
		auto words = std::vector<QString>();
		words.reserve(count);
		for (auto i = 0; i != count; ++i) {
			words.push_back(wordAt(i));
		}
		const auto empty = ranges::find(words, QString());
		const auto wrong = (empty != end(words))
			? empty
			: ranges::find_if(words, [](const QString &word) {
				return !IsWordlistWord(word);
			});
		if (wrong != end(words)) {
			const auto field = state->fields[wrong - begin(words)];
			revealField(field);
			if (wrong->isEmpty()) {
				field->setFocus();
			} else {
				field->showError();
			}
			return;
		}
		const auto match = DetectPhraseMatch(words);
		if (match != PhraseMatch::Rotation) {
			state->error = QString();
			ShowInvalidSecretWords(show, match == PhraseMatch::Foreign, context);
			return;
		}
		state->importing = true;
		if (mode == WalletImportMode::Restore) {
			const auto done = crl::guard(box, [=] {
				if (restored) {
					box->closeBox();
					RunWhenSigningReady(show, restored);
				} else {
					show->hideLayer();
					show->showToast({
						.title = tr::lng_wallet_imported_title(tr::now),
						.text = { tr::lng_wallet_imported_text(tr::now) },
						.icon = &st::toastCheckIcon,
					});
				}
			});
			const auto fail = crl::guard(box, [=](const QString &error) {
				if (context && (!context->valid()
					|| error == u"PHRASE_ORIGIN_EXPIRED"_q
					|| error == u"PHRASE_SILENT_ERROR"_q
					|| error == u"PHRASE_INSTALL_CANCELLED"_q)) {
					context->cancel();
					return;
				}
				state->importing = false;
				if (error == u"PHRASE_INVALID_PHRASE"_q
					|| error == u"PHRASE_FOREIGN_PHRASE"_q) {
					state->error = QString();
					ShowInvalidSecretWords(
						show,
						error == u"PHRASE_FOREIGN_PHRASE"_q,
						context);
					return;
				} else if (error == u"PHRASE_OTHER_WALLET"_q
					|| error == u"PHRASE_OUTDATED"_q) {
					state->error = QString();
					ShowWrongSecretWords(
						show,
						error == u"PHRASE_OUTDATED"_q,
						context);
					return;
				}
				// A dismissed protection chooser restored nothing and has
				// nothing to state, so the form simply stays as it was.
				// A locked vault is stated on this label, not in the toast
				// its replace and backup-enable siblings use: unlocking
				// the vault leaves the typed words ready to resubmit.
				state->error = (error == u"PHRASE_INSTALL_CANCELLED"_q)
					? QString()
					: (error == u"PHRASE_INSTALL_FAILED"_q)
					? tr::lng_wallet_key_save_error(tr::now)
					: (error == u"PHRASE_KEY_CHANGING"_q)
					? tr::lng_wallet_import_key_changing(tr::now)
					: (error == u"PHRASE_VAULT_LOCKED"_q)
					? tr::lng_wallet_vault_locked(tr::now)
					: ErrorWithType(
						tr::lng_wallet_import_failed(tr::now),
						error);
				if (context) {
					show->showToast(state->error.current());
					context->cancel();
				}
			});
			if (context) {
				show->session().wallet().restoreFromPhrase(
					KeyAuthorization{ .install = context->installer() },
					std::move(words),
					context->scope(),
					[=](KeyAuthorization auth) {
						context->ready(std::move(auth));
					},
					fail);
			} else {
				show->session().wallet().restoreFromPhrase(
					KeyAuthorization{ .install = MakeCustodyInstaller(show) },
					std::move(words),
					done,
					fail);
			}
		} else {
			StartWalletReplace(show, box, std::move(words), [=] {
				state->importing = false;
			}, [=](const QString &text) {
				state->error = text;
			});
		}
	};
	box->addButton(tr::lng_wallet_import_button(), submit);

	box->setFocusCallback([=] {
		state->fields.front()->setFocusFast();
	});

	const auto suggestions = Ui::CreateChild<Ui::RpWidget>(box.get());
	const auto suggestionShadow = suggestions->lifetime().make_state<Ui::BoxShadow>(
		st::boxRoundShadow);
	suggestions->hide();
	suggestions->setFocusPolicy(Qt::NoFocus);
	suggestions->paintRequest(
	) | rpl::on_next([=] {
		auto p = QPainter(suggestions);
		auto hq = PainterHighQualityEnabler(p);
		const auto inner = suggestions->rect().marginsRemoved(
			suggestionShadow->extend());
		suggestionShadow->paint(p, inner, st::boxRadius);
		p.setPen(Qt::NoPen);
		p.setBrush(st::windowBg);
		p.drawRoundedRect(inner, st::boxRadius, st::boxRadius);
	}, suggestions->lifetime());

	const auto hideSuggestions = [=] {
		state->suggestionField = -1;
		state->suggestionWords.clear();
		suggestions->hide();
	};
	const auto repositionSuggestions = [=] {
		const auto index = state->suggestionField;
		if (index < 0) {
			return;
		}
		const auto field = state->fields[index];
		const auto extend = suggestionShadow->extend();
		const auto &padding = st::walletImportSuggestionsPadding;
		const auto &rowPadding = st::walletImportSuggestionRowPadding;
		auto textWidth = 0;
		for (const auto &word : state->suggestionWords) {
			textWidth = std::max(textWidth, st::normalFont->width(word));
		}
		const auto innerWidth = std::min(
			padding.left()
				+ rowPadding.left()
				+ textWidth
				+ rowPadding.right()
				+ padding.right(),
			field->width());
		const auto count = int(state->suggestionWords.size());
		const auto innerHeight = padding.top()
			+ count * st::walletImportSuggestionRowHeight
			+ padding.bottom();
		suggestions->resize(
			extend.left() + innerWidth + extend.right(),
			extend.top() + innerHeight + extend.bottom());
		for (auto i = 0; i != int(state->suggestionRows.size()); ++i) {
			const auto row = state->suggestionRows[i];
			row->setVisible(i < count);
			row->setGeometry(
				extend.left() + padding.left(),
				extend.top()
					+ padding.top()
					+ i * st::walletImportSuggestionRowHeight,
				innerWidth - padding.left() - padding.right(),
				st::walletImportSuggestionRowHeight);
		}
		const auto fieldTopLeft = field->mapTo(box, QPoint(0, 0));
		const auto fieldTop = fieldTopLeft.y();
		const auto below = fieldTop
			+ field->height()
			+ st::walletImportSuggestionsSkip;
		const auto flip = (below + innerHeight > box->height());
		const auto top = flip
			? (fieldTop
				- st::walletImportSuggestionsSkip
				- innerHeight
				- extend.top())
			: (below - extend.top());
		suggestions->move(fieldTopLeft.x() - extend.left(), top);
		if (fieldTop + field->height() <= cover.height()
			|| fieldTop >= box->height()) {
			suggestions->hide();
		} else {
			suggestions->show();
		}
	};
	const auto refreshSuggestions = [=](int index) {
		const auto field = state->fields[index];
		const auto typed = wordAt(index);
		auto words = typed.isEmpty()
			? std::vector<QString>()
			: WordlistSuggestions(typed, kImportSuggestionsLimit);
		if (!field->hasFocus()
			|| words.empty()
			|| (words.size() == 1 && words.front() == typed)) {
			hideSuggestions();
			return;
		}
		state->suggestionField = index;
		state->suggestionWords = std::move(words);
		state->suggestionSelected = 0;
		suggestions->raise();
		repositionSuggestions();
		suggestions->update();
	};
	const auto acceptSuggestion = [=] {
		const auto index = state->suggestionField;
		if (index < 0
			|| state->suggestionWords.empty()
			|| suggestions->isHidden()) {
			return false;
		}
		const auto selected = std::clamp(
			state->suggestionSelected,
			0,
			int(state->suggestionWords.size()) - 1);
		const auto word = state->suggestionWords[selected];
		const auto field = state->fields[index];
		const auto typed = wordAt(index);
		hideSuggestions();
		if (word == typed) {
			return false;
		}
		field->setText(word);
		field->forceProcessContentsChanges();
		if (index + 1 < state->count.current()) {
			state->fields[index + 1]->setFocus();
		} else {
			field->setCursorPosition(word.size());
		}
		return true;
	};
	const auto moveSuggestionSelection = [=](int delta) {
		const auto count = int(state->suggestionWords.size());
		if (!count) {
			return;
		}
		state->suggestionSelected = std::clamp(
			state->suggestionSelected + delta,
			0,
			count - 1);
		suggestions->update();
	};
	for (auto i = 0; i != kImportSuggestionsLimit; ++i) {
		const auto row = Ui::CreateChild<Ui::AbstractButton>(suggestions);
		row->setPointerCursor(true);
		row->setFocusPolicy(Qt::NoFocus);
		row->paintRequest(
		) | rpl::on_next([=] {
			auto p = QPainter(row);
			if (i == state->suggestionSelected) {
				p.fillRect(row->rect(), st::windowBgOver);
			}
			if (i >= int(state->suggestionWords.size())
				|| state->suggestionField < 0) {
				return;
			}
			const auto &word = state->suggestionWords[i];
			const auto typed = wordAt(state->suggestionField);
			const auto prefix = word.startsWith(typed)
				? typed
				: QString();
			const auto font = st::normalFont;
			p.setFont(font);
			const auto left = st::walletImportSuggestionRowPadding.left();
			const auto baseline = (row->height() - font->height) / 2
				+ font->ascent;
			p.setPen(st::windowFg);
			p.drawText(left, baseline, prefix);
			p.setPen(st::windowSubTextFg);
			p.drawText(
				left + font->width(prefix),
				baseline,
				word.mid(prefix.size()));
		}, row->lifetime());
		row->setClickedCallback([=] {
			state->suggestionSelected = i;
			acceptSuggestion();
		});
		state->suggestionRows.push_back(row);
	}

	toggle->sectionActivated(
	) | rpl::on_next([=](int section) {
		applyCount((section == 1)
			? kImportWordCountLong
			: kImportWordCountShort);
	}, toggle->lifetime());
	state->count.changes() | rpl::on_next([=] {
		state->error = QString();
		hideSuggestions();
	}, box->lifetime());

	for (auto i = 0; i != kImportWordCountLong; ++i) {
		const auto field = state->fields[i];
		state->pasteButtons[i]->setClickedCallback([=] {
			const auto text = QGuiApplication::clipboard()->text();
			const auto words = SplitPhraseWords(text);
			if (words.size() > 1) {
				distributePaste(words);
			} else {
				field->setText(text.trimmed().toLower());
				field->forceProcessContentsChanges();
				field->setFocusFast();
			}
		});
		state->clearButtons[i]->setClickedCallback([=] {
			field->setText(QString());
			field->forceProcessContentsChanges();
			field->setFocusFast();
		});
		field->setMimeDataHook([=](
				not_null<const QMimeData*> data,
				Ui::InputField::MimeAction action) {
			const auto text = data->hasText() ? data->text() : QString();
			const auto words = SplitPhraseWords(text);
			if (words.size() < 2) {
				return false;
			}
			if (action == Ui::InputField::MimeAction::Check) {
				return true;
			}
			distributePaste(words);
			return true;
		});
		field->submits() | rpl::on_next([=] {
			if (acceptSuggestion()) {
				return;
			}
			if (i + 1 < state->count.current()) {
				state->fields[i + 1]->setFocus();
			} else {
				submit();
			}
		}, field->lifetime());
		field->tabbed() | rpl::on_next([=](
				not_null<Ui::InputField::TabbedRequest*> request) {
			if (request->backward) {
				if (i > 0) {
					request->handled = true;
					state->fields[i - 1]->setFocus();
				}
				return;
			}
			request->handled = true;
			if (acceptSuggestion()) {
				return;
			}
			if (i + 1 < state->count.current()) {
				state->fields[i + 1]->setFocus();
			}
		}, field->lifetime());
		base::install_event_filter(field->rawTextEdit(), [=](
				not_null<QEvent*> event) {
			if (event->type() == QEvent::FocusIn) {
				const auto focus = static_cast<QFocusEvent*>(event.get());
				const auto reason = focus->reason();
				// Focus given back to the same field keeps a manual scroll.
				state->focusRestored = (state->lastFocusedField == i)
					&& (reason == Qt::ActiveWindowFocusReason
						|| reason == Qt::PopupFocusReason);
				state->lastFocusedField = i;
				return base::EventFilterResult::Continue;
			}
			if (event->type() != QEvent::KeyPress) {
				return base::EventFilterResult::Continue;
			}
			const auto key = static_cast<QKeyEvent*>(event.get())->key();
			const auto shown = (state->suggestionField == i)
				&& !suggestions->isHidden();
			if (shown && key == Qt::Key_Down) {
				moveSuggestionSelection(1);
				return base::EventFilterResult::Cancel;
			} else if (shown && key == Qt::Key_Up) {
				moveSuggestionSelection(-1);
				return base::EventFilterResult::Cancel;
			} else if (shown && key == Qt::Key_Escape) {
				hideSuggestions();
				return base::EventFilterResult::Cancel;
			} else if (key == Qt::Key_Backspace
				&& field->getLastText().isEmpty()
				&& i > 0) {
				state->fields[i - 1]->setFocus();
				return base::EventFilterResult::Cancel;
			}
			return base::EventFilterResult::Continue;
		});
		field->changes() | rpl::on_next([=] {
			state->error = QString();
			refreshAccessories(i);
			refreshSuggestions(i);
			if (field->hasFocus()) {
				revealField(field);
			}
			// WHY: every content-change pass starts by clearing the error,
			// including the one forceProcessContentsChanges() postpones after
			// setText(), so marking is postponed to land after it.
			Ui::PostponeCall(field, [=] {
				markWord(i, field->hasFocus());
			});
		}, field->lifetime());
		field->focusedChanges() | rpl::on_next([=](bool focused) {
			markWord(i, false);
			refreshAccessories(i);
			if (focused) {
				refreshSuggestions(i);
				if (!base::take(state->focusRestored)) {
					revealField(field);
				}
			} else if (state->suggestionField == i) {
				hideSuggestions();
			}
		}, field->lifetime());
		refreshAccessories(i);
	}

	// A pasted 24-word phrase focuses its last field while this still opens.
	extraWrap->heightValue(
	) | rpl::skip(1) | rpl::on_next([=] {
		if (!extraWrap->toggled()) {
			return;
		}
		for (auto i = kImportWordCountShort; i != kImportWordCountLong; ++i) {
			if (state->fields[i]->hasFocus()) {
				revealField(state->fields[i]);
				return;
			}
		}
	}, extraWrap->lifetime());

	box->widthValue() | rpl::skip(1) | rpl::on_next([=] {
		repositionSuggestions();
	}, box->lifetime());
	box->setInitScrollCallback([=] {
		cover.widget->raise();
		cover.updateScroll();
		box->scrolls() | rpl::on_next([=] {
			cover.updateScroll();
			repositionSuggestions();
		}, box->lifetime());
	});
}

void WalletReplaceBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_replace_title(),
			st::walletReplaceTitleLabel),
		st::walletReplaceTitleMargin);
	const auto create = box->addRow(
		object_ptr<Ui::RoundButton>(
			box,
			tr::lng_wallet_replace_create(),
			st::walletSendButton),
		st::walletReplaceButtonMargin,
		style::al_justify);
	create->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	const auto creating = box->lifetime().make_state<bool>(false);
	create->setClickedCallback([=] {
		if (*creating) {
			return;
		}
		*creating = true;
		StartWalletReplace(show, box, std::nullopt, [=] {
			*creating = false;
		});
	});
	const auto import = box->addRow(
		object_ptr<Ui::RoundButton>(
			box,
			tr::lng_wallet_replace_import(),
			st::walletSendButton),
		st::walletReplaceButtonMargin,
		style::al_justify);
	import->setTextTransform(Ui::RoundButtonTextTransform::NoTransform);
	import->setClickedCallback([=] {
		box->closeBox();
		show->showBox(Box(
			WalletImportBox,
			show,
			WalletImportMode::Replace,
			nullptr,
			nullptr,
			nullptr));
	});
	Ui::AddSkip(box->verticalLayout());
}

void WalletConflictBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show,
		Fn<void()> switched) {
	struct State {
		bool busy = false;
	};
	// The box closes on its own once a drop leaves no parked record, and
	// that close continues the action that hit the conflict. Every other
	// close, including one while a drop is still pending, is the user giving
	// up, and the drop's completion then continues nothing. The outcome
	// outlives the box, because the settling close can destroy it before
	// the drop's callback runs.
	struct Outcome {
		bool settled = false;
		bool cancelled = false;
	};
	const auto state = box->lifetime().make_state<State>();
	const auto outcome = std::make_shared<Outcome>();
	const auto closed = [=] {
		if (!outcome->settled) {
			outcome->cancelled = true;
		}
	};
	box->boxClosing() | rpl::on_next(closed, box->lifetime());
	box->lifetime().add(closed);
	const auto wallet = &show->session().wallet();

	box->setStyle(st::walletConflictBox);
	Ui::AddSkip(box->verticalLayout(), st::walletConflictBoxTopSkip);
	box->addRow(
		object_ptr<Ui::FlatLabel>(
			box,
			tr::lng_wallet_conflict_text(),
			st::boxLabel),
		st::boxRowPadding);

	const auto content = box->verticalLayout()->add(
		object_ptr<Ui::VerticalLayout>(box->verticalLayout()));

	const auto rebuild = [=] {
		const auto parked = wallet->parkedRecords();
		if (parked.empty()) {
			outcome->settled = true;
			box->closeBox();
			return;
		}
		content->clear();
		auto first = true;
		for (const auto &record : parked) {
			if (!first) {
				Ui::AddSkip(content);
				Ui::AddDivider(content);
			}
			first = false;
			const auto address = FormatFriendly(
				CanonicalAddress(record.address),
				false);
			if (address.size() == kAddressLength) {
				AddAddressPlate(
					content,
					address,
					QMargins(
						st::boxRowPadding.left(),
						st::walletAddressPlateSkip,
						st::boxRowPadding.right(),
						st::walletAddressPlateSkip));
			} else {
				Ui::AddSkip(content);
			}
			const auto key = record.publicKey;
			const auto showPhrase = content->add(
				object_ptr<Ui::RoundButton>(
					content,
					tr::lng_wallet_keys_show_phrase(),
					st::defaultLightButton),
				st::boxRowPadding,
				style::al_justify);
			showPhrase->setFullRadius(true);
			showPhrase->setClickedCallback([=] {
				WalletRevealFlow(show, key);
			});
			Ui::AddSkip(content);
			const auto switchNow = content->add(
				object_ptr<Ui::RoundButton>(
					content,
					tr::lng_wallet_conflict_switch(),
					st::attentionBoxButton),
				st::boxRowPadding,
				style::al_justify);
			switchNow->setFullRadius(true);
			switchNow->setClickedCallback([=] {
				if (state->busy) {
					return;
				}
				state->busy = true;
				// Dropping the last parked record fires the custody update
				// that closes this emptied box from rebuild() before this
				// callback runs, and with another box underneath that close
				// destroys it at once, so the continuation is not guarded
				// by the box. It runs only once no parked record is left,
				// with more of them listed the box stays for the next one,
				// and never after the user closed the box while the drop
				// was still pending.
				const auto weak = base::make_weak(box.get());
				wallet->dropParked(key, [=] {
					const auto resolved = !wallet->deviceCustodyState().conflict;
					if (const auto strong = weak.get()) {
						state->busy = false;
						if (resolved && switched && strong->hasDelegate()) {
							outcome->settled = true;
							strong->closeBox();
						}
					}
					if (resolved && switched && !outcome->cancelled) {
						switched();
					}
				}, crl::guard(box, [=](const QString &error) {
					state->busy = false;
					ShowPhraseError(show, PhraseOperation::DropParked, error);
				}));
			});
		}
		if (const auto width = content->width()) {
			content->resizeToWidth(width);
		}
	};
	rebuild();

	wallet->custodyUpdates(
	) | rpl::on_next(rebuild, box->lifetime());

	wallet->presenceValue(
	) | rpl::filter([](Presence presence) {
		return (presence != Presence::Ready);
	}) | rpl::on_next([=] {
		box->closeBox();
	}, box->lifetime());

	Ui::AddSkip(box->verticalLayout());
	const auto cancel = box->verticalLayout()->add(
		object_ptr<Ui::RoundButton>(
			box,
			tr::lng_cancel(),
			st::defaultLightButton),
		st::boxRowPadding,
		style::al_justify);
	cancel->setFullRadius(true);
	cancel->setClickedCallback([=] {
		box->closeBox();
	});
}

void AddBackupSection(
		not_null<Ui::VerticalLayout*> container,
		std::shared_ptr<Main::SessionShow> show,
		not_null<Ui::GenericBox*> box) {
	auto &wallet = show->session().wallet();
	const auto busy = box->lifetime().make_state<bool>(false);
	const auto restoring = box->lifetime().make_state<rpl::variable<bool>>(
		false);
	Ui::AddSubsectionTitle(container, tr::lng_wallet_backup_section());
	const auto disable = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			Settings::CreateButtonWithIcon(
				container,
				tr::lng_wallet_backup_disable(),
				st::settingsAttentionButton)));
	disable->toggleOn(wallet.capabilitiesValue(
	) | rpl::map([](const WalletCapabilities &capabilities) {
		return capabilities.backupEnabled;
	}));
	disable->finishAnimating();
	AddRowSpinner(disable->entity(), restoring->value());
	disable->entity()->addClickHandler([=] {
		if (*busy) {
			return;
		}
		RunKeyRequiringAction(show, crl::guard(box, [=] {
			StartBackupDisable(show, box, busy, restoring);
		}), KeyActionKind::Reveal);
	});
	const auto enable = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			Settings::CreateButtonWithIcon(
				container,
				tr::lng_wallet_backup_enable(),
				st::settingsButtonNoIcon)));
	enable->toggleOn(wallet.capabilitiesValue(
	) | rpl::map([](const WalletCapabilities &capabilities) {
		return capabilities.canEnableBackup && !capabilities.backupEnabled;
	}));
	enable->finishAnimating();
	enable->entity()->addClickHandler([=] {
		StartBackupEnable(show, box, busy);
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(container, wallet.capabilitiesValue(
	) | rpl::map([](const WalletCapabilities &capabilities) {
		return (capabilities.backupEnabled || capabilities.canEnableBackup)
			? tr::lng_wallet_backup_about_on()
			: tr::lng_wallet_backup_about_unavailable();
	}) | rpl::flatten_latest());
	Ui::AddSkip(container);
}

void WalletKeysBackupBox(
		not_null<Ui::GenericBox*> box,
		std::shared_ptr<Main::SessionShow> show) {
	auto &wallet = show->session().wallet();
	if (wallet.presence() != Presence::Ready) {
		box->closeBox();
		return;
	}
	box->setTitle(tr::lng_wallet_keys_title());
	const auto container = box->verticalLayout();
	Ui::AddSkip(container);
	Ui::AddSubsectionTitle(container, tr::lng_wallet_phrase_intro_title());
	const auto phrase = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			Settings::CreateButtonWithIcon(
				container,
				tr::lng_wallet_keys_show_phrase(),
				st::settingsButtonNoIcon)));
	phrase->toggleOn(rpl::combine(
		wallet.capabilitiesValue(),
		wallet.deviceCustodyStateValue()
	) | rpl::map([](
			const WalletCapabilities &capabilities,
			const DeviceCustodyState &custody) {
		return capabilities.canExportPhrase
			|| (custody.mode == DeviceMode::Full);
	}));
	phrase->finishAnimating();
	phrase->entity()->addClickHandler([=] {
		RunKeyRequiringAction(show, [=] {
			WalletRevealFlow(show);
		}, KeyActionKind::Reveal);
	});
	const auto restore = container->add(
		object_ptr<Ui::SlideWrap<Ui::SettingsButton>>(
			container,
			Settings::CreateButtonWithIcon(
				container,
				tr::lng_wallet_keys_restore(),
				st::settingsButtonNoIcon)));
	restore->toggleOn(wallet.deviceCustodyStateValue(
	) | rpl::map([](const DeviceCustodyState &custody) {
		return (custody.mode == DeviceMode::ReadOnlyNotRestorable);
	}));
	restore->finishAnimating();
	restore->entity()->addClickHandler([=] {
		show->showBox(Box(
			WalletImportBox,
			show,
			WalletImportMode::Restore,
			nullptr,
			nullptr,
			nullptr));
	});
	Ui::AddSkip(container);
	Ui::AddDividerText(container, tr::lng_wallet_keys_phrase_about());
	Ui::AddSkip(container);
	AddBackupSection(container, show, box);
	Settings::AddButtonWithIcon(
		container,
		tr::lng_wallet_keys_delete(),
		st::settingsAttentionButton
	)->addClickHandler([=] {
		show->showBox(Ui::MakeConfirmBox({
			.text = tr::lng_wallet_delete_text(tr::now),
			.confirmed = [=](Fn<void()> close) {
				close();
				show->showBox(Box(WalletReplaceBox, show));
			},
			.confirmText = tr::lng_suggest_warn_delete_anyway(),
			.confirmStyle = &st::attentionBoxButton,
			.title = tr::lng_wallet_delete_title(),
		}));
	});
	Ui::AddSkip(container);
	box->addButton(tr::lng_close(), [=] { box->closeBox(); });
}

} // namespace ContentDetails

} // namespace Wallet
