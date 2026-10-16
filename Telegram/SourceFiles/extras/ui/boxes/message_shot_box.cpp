#include "extras/ui/boxes/message_shot_box.h"

#include "lang_auto.h"
#include "extras/extras_settings.h"
#include "extras/ui/boxes/theme_selector_box.h"
#include "extras/ui/components/image_view.h"
#include "boxes/abstract_box.h"
#include "data/data_chat.h"
#include "data/data_channel.h"
#include "data/data_session.h"
#include "data/data_todo_list.h"
#include "data/data_user.h"
#include "history/history.h"
#include "history/history_item_components.h"
#include "history/history_item.h"
#include "main/main_session.h"
#include "settings/settings_common.h"
#include "styles/style_extras_styles.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "ui/vertical_list.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/option_list.h"
#include "ui/wrap/vertical_layout.h"

#include <memory>
#include <QFileDialog>
#include <QGuiApplication>

namespace {

[[nodiscard]] bool hasSpoilerEntity(const TextWithEntities &text) {
	return ranges::any_of(text.entities, [](const EntityInText &entity) {
		return entity.type() == EntityType::Spoiler;
	});
}

[[nodiscard]] bool messageHasSpoilers(not_null<HistoryItem*> item) {
	if (hasSpoilerEntity(item->originalText())) {
		return true;
	}
	const auto media = item->media();
	if (!media) {
		return false;
	}
	if (media->hasSpoiler()) {
		return true;
	}
	const auto todoList = media->todolist();
	if (!todoList) {
		return false;
	}
	return hasSpoilerEntity(todoList->title)
		|| ranges::any_of(todoList->items, [](const auto &task) {
			return hasSpoilerEntity(task.text);
		});
}

// 与消息头部的频道标识一致：讨论帖总显示，带署名的群组匿名消息不显示。
[[nodiscard]] bool drawsChannelBadge(not_null<HistoryItem*> item) {
	if (item->isDiscussionPost()) {
		return true;
	}
	if (item->author()->isMegagroup()) {
		const auto signedInfo = item->Get<HistoryMessageSigned>();
		if (signedInfo && !signedInfo->viaBusinessBot) {
			return false;
		}
	}
	return item->history()->peer->isMegagroup()
		&& item->author()->isChannel()
		&& !item->out();
}

[[nodiscard]] QString megagroupBadgeText(
		not_null<ChannelData*> channel,
		not_null<UserData*> user,
		not_null<HistoryItem*> item) {
	const auto info = channel->mgInfo.get();
	const auto userId = peerToUser(user->id);
	const auto isCreator = info && (info->creator == user);
	const auto isAdmin = info && info->admins.contains(userId);
	if (!isCreator && !isAdmin) {
		return item->fromRank();
	}
	const auto rank = info->memberRanks.find(userId);
	if (rank != info->memberRanks.end() && !rank->second.isEmpty()) {
		return rank->second;
	}
	return isCreator
		? tr::lng_owner_badge(tr::now)
		: tr::lng_admin_badge(tr::now);
}

[[nodiscard]] QString headerBadgeText(
		not_null<HistoryItem*> item,
		bool channelBadge) {
	if (item->isDiscussionPost()) {
		return tr::lng_channel_badge(tr::now);
	}
	if (item->author()->isMegagroup()) {
		const auto signedInfo = item->Get<HistoryMessageSigned>();
		return (signedInfo && !signedInfo->viaBusinessBot)
			? signedInfo->author
			: QString();
	}
	if (channelBadge) {
		return tr::lng_channel_badge(tr::now);
	}
	const auto user = item->author()->asUser();
	if (!user) {
		return QString();
	}
	if (const auto chat = item->history()->peer->asChat()) {
		const auto rank = chat->memberRanks.find(peerToUser(user->id));
		if (rank == chat->memberRanks.end()) {
			return QString();
		}
		return rank->second;
	}
	if (const auto channel = item->history()->peer->asMegagroup()) {
		return megagroupBadgeText(channel, user, item);
	}
	return QString();
}

} // namespace

MessageShotBox::MessageShotBox(
	QWidget *parent,
	ExtrasFeatures::MessageShot::ShotConfig config)
	: _config(std::move(config)) {
	setObjectName(u"messageShot.preview"_q);
}

void MessageShotBox::prepare() {
	setupContent();
}

void MessageShotBox::setupContent() {
	_selectedPalette = ExtrasFeatures::MessageShot::getPersistedPalette();
	if (!_selectedPalette) {
		_selectedPalette = std::make_shared<style::palette>();
	}
	ExtrasFeatures::MessageShot::setPersistedPalette(_selectedPalette);

	ExtrasFeatures::MessageShot::ensureChatThemesRefreshed();

	auto &settings = ExtrasSettings::getInstance();
	auto &shotSettings = settings.messageShotSettings();
	const auto savedSimpleQuotesAndReplies = settings.simpleQuotesAndReplies();
	settings.setSimpleQuotesAndReplies(!shotSettings.showColorfulReplies());

	using namespace Settings;

	auto savedThemeApplyResult = ExtrasFeatures::MessageShot::SavedThemeApplyResult::Failed;
	const auto hasSavedTheme = shotSettings.embeddedThemeType() != -1
		|| shotSettings.cloudThemeId() != 0;
	if (hasSavedTheme) {
		savedThemeApplyResult = ExtrasFeatures::MessageShot::applySavedThemePalette(
			_selectedPalette,
			nullptr);
		if (savedThemeApplyResult != ExtrasFeatures::MessageShot::SavedThemeApplyResult::Failed) {
			_config.st = std::make_shared<Ui::ChatStyle>(_selectedPalette.get());
		} else {
			shotSettings.clearTheme();
			_config.st = std::make_shared<Ui::ChatStyle>(_config.controller->chatStyle());
		}
	}

	setTitle(tr::extras_MessageShotTopBarText());

	auto wrap = object_ptr<Ui::VerticalLayout>(this);
	const auto content = wrap.data();
	setInnerWidget(object_ptr<Ui::OverrideMargins>(this, std::move(wrap)));

	AddSubsectionTitle(content, tr::extras_MessageShotPreview());

	const auto imageView = content->add(object_ptr<ImageView>(content), st::imageViewPadding);
	imageView->setObjectName(u"messageShot.image"_q);

	AddSkip(content, st::boxMediumSkip);
	AddSubsectionTitle(content, tr::extras_MessageShotPreferences());

	auto hasReactions = false;
	auto hasReplies = false;
	auto hasHeaderDecorations = false;
	auto hasSpoilers = false;
	for (const auto &item : _config.controller->session().data().idsToItems(_config.messages)) {
		hasReactions = hasReactions || !item->reactions().empty();
		hasReplies = hasReplies
			|| item->replyTo().replying()
			|| (item->media() && item->media()->webpage());
		hasSpoilers = hasSpoilers || messageHasSpoilers(item);
		if (!hasHeaderDecorations) {
			const auto channelBadge = drawsChannelBadge(item);
			hasHeaderDecorations = channelBadge
				|| !headerBadgeText(item, channelBadge).isEmpty()
				|| (item->boostsApplied() > 0);
		}
		if (hasReactions && hasReplies && hasHeaderDecorations
			&& hasSpoilers) {
			break;
		}
	}

	const auto generation = content->lifetime().make_state<int>(0);
	const auto weak = base::make_weak(this);

	const auto updatePreview = [=]
	{
		const auto currentGeneration = ++(*generation);
		ExtrasFeatures::MessageShot::Make(this, _config, [=](const QImage &image, bool final)
		{
			if (!weak || currentGeneration != *generation) {
				return;
			}

			if (final || imageView->getImage().isNull()) {
				imageView->setImage(image);
			}
		});
	};

	if (savedThemeApplyResult == ExtrasFeatures::MessageShot::SavedThemeApplyResult::AwaitingAsync) {
		const auto weakBox = base::make_weak(this);
		ExtrasFeatures::MessageShot::subscribeToCloudThemeLoad(
			_config.controller,
			_selectedPalette,
			[=] {
				if (!weakBox) {
					return;
				}
				_config.st = std::make_shared<Ui::ChatStyle>(_selectedPalette.get());
				updatePreview();
			});
	}

	auto selectedTheme =
		content->lifetime().make_state<rpl::variable<QString>>(
			ExtrasFeatures::MessageShot::resolveThemeName());

	AddButtonWithLabel(
		content,
		tr::extras_MessageShotTheme(),
		selectedTheme->value(),
		st::settingsButtonNoIcon
	)->addClickHandler(
		[=]
		{
			ExtrasFeatures::MessageShot::setChoosingTheme(true);

			auto box = Box<ThemeSelectorBox>(_config.controller);
			box->paletteSelected() | rpl::on_next(
				[=](const style::palette &palette) mutable
				{
					_selectedPalette->reset();
					_selectedPalette->load(palette.save());

					_config.st = std::make_shared<Ui::ChatStyle>(_selectedPalette.get());

					auto &shot = ExtrasSettings::getInstance().messageShotSettings();
					const auto embedded = ExtrasFeatures::MessageShot::getSelectedFromDefault();
					const auto cloud = ExtrasFeatures::MessageShot::getSelectedFromCustom();
					if (cloud.has_value()) {
						const auto accountId = _config.controller->session().userId().bare;
						shot.setCloudTheme(accountId, cloud->id, cloud->accessHash, cloud->documentId, cloud->title);
					} else if (embedded != Window::Theme::EmbeddedType(-1)) {
						const auto color = ExtrasFeatures::MessageShot::getSelectedColorFromDefault();
						shot.setEmbeddedTheme(static_cast<int>(embedded), color ? color->rgb() : 0);
					} else {
						shot.clearTheme();
					}

					updatePreview();
				},
				content->lifetime());

			box->themeNameChanged() | rpl::on_next(
				[=](const QString &name)
				{
					selectedTheme->force_assign(name);
				},
				content->lifetime());

			box->boxClosing() | rpl::on_next(
				[=]
				{
					ExtrasFeatures::MessageShot::setChoosingTheme(false);
				},
				content->lifetime());

			Ui::show(std::move(box), Ui::LayerOption::KeepOther);
		});
	const auto options = content->add(
		object_ptr<Ui::OptionList>(content),
		st::boxRowPadding);
	options->addOption(object_ptr<Ui::Checkbox>(
		options,
		tr::extras_MessageShotShowBackground(),
		shotSettings.showBackground(),
		st::defaultBoxCheckbox
	))->checkedValue(
	) | rpl::skip(1) | on_next(
		[=](bool enabled)
		{
			ExtrasSettings::getInstance().messageShotSettings().setShowBackground(enabled);
			updatePreview();
		},
		content->lifetime());

	auto latestToggle = options->addOption(object_ptr<Ui::Checkbox>(
		options,
		tr::extras_MessageShotShowDate(),
		shotSettings.showDate(),
		st::defaultBoxCheckbox
	));
	latestToggle->setObjectName(u"messageShot.showDate"_q);
	latestToggle->checkedValue(
	) | rpl::skip(1) | on_next(
		[=](bool enabled)
		{
			ExtrasSettings::getInstance().messageShotSettings().setShowDate(enabled);
			updatePreview();
		},
		content->lifetime());

	if (hasReactions) {
		latestToggle = options->addOption(object_ptr<Ui::Checkbox>(
			options,
			tr::extras_MessageShotShowReactions(),
			shotSettings.showReactions(),
			st::defaultBoxCheckbox
		));
		latestToggle->checkedValue(
		) | rpl::skip(1) | on_next(
			[=](bool enabled)
			{
				ExtrasSettings::getInstance().messageShotSettings().setShowReactions(enabled);
				updatePreview();
			},
			content->lifetime());
	}

	if (hasHeaderDecorations) {
		latestToggle = options->addOption(object_ptr<Ui::Checkbox>(
			options,
			tr::extras_MessageShotShowHeaderDecorations(),
			shotSettings.showHeaderDecorations(),
			st::defaultBoxCheckbox
		));
		latestToggle->checkedValue(
		) | rpl::skip(1) | on_next(
			[=](bool enabled)
			{
				ExtrasSettings::getInstance().messageShotSettings().setShowHeaderDecorations(enabled);
				updatePreview();
			},
			content->lifetime());
	}

	if (hasReplies) {
		latestToggle = options->addOption(object_ptr<Ui::Checkbox>(
			options,
			tr::extras_MessageShotShowColorfulReplies(),
			shotSettings.showColorfulReplies(),
			st::defaultBoxCheckbox
		));
		latestToggle->checkedValue(
		) | rpl::skip(1) | on_next(
			[=](bool enabled)
			{
				auto &currentSettings = ExtrasSettings::getInstance();
				currentSettings.messageShotSettings().setShowColorfulReplies(enabled);
				currentSettings.setSimpleQuotesAndReplies(!enabled);

				_config.st = std::make_shared<Ui::ChatStyle>(_config.st.get());
				updatePreview();
			},
			content->lifetime());
	}

	if (hasSpoilers) {
		latestToggle = options->addOption(object_ptr<Ui::Checkbox>(
			options,
			tr::extras_MessageShotRevealSpoilers(),
			shotSettings.revealSpoilers(),
			st::defaultBoxCheckbox
		));
		latestToggle->checkedValue(
		) | rpl::skip(1) | on_next(
			[=](bool enabled)
			{
				ExtrasSettings::getInstance().messageShotSettings().setRevealSpoilers(enabled);
				updatePreview();
			},
			content->lifetime());
	}

	AddSkip(content);

	addButton(tr::extras_MessageShotSave(),
			  [=]
			  {
				  const auto image = imageView->getImage();
				  const auto path = QFileDialog::getSaveFileName(
					  this,
					  tr::lng_save_file(tr::now),
					  QString(),
					  "*.png");

				  if (!path.isEmpty()) {
					  image.save(path);
				  }

			  	  _tookShot = true;
				  closeBox();
			  });
	addButton(tr::extras_MessageShotCopy(),
			  [=]
			  {
				  QGuiApplication::clipboard()->setImage(imageView->getImage());

			  	  _tookShot = true;
				  closeBox();
			  });

	updatePreview();

	const auto boxWidth = imageView->getImage().width() / style::DevicePixelRatio() + (st::boxPadding.left() + st::boxPadding.right()) * 4;

	boxClosing() | rpl::on_next(
		[=]
		{
			ExtrasFeatures::MessageShot::resetCustomSelected();
			ExtrasFeatures::MessageShot::resetDefaultSelected();

			ExtrasSettings::getInstance().setSimpleQuotesAndReplies(savedSimpleQuotesAndReplies);
		},
		content->lifetime());

	setDimensionsToContent(boxWidth, content);

	scrollToWidget(latestToggle);
}
