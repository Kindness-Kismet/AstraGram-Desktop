#include "extras/ui/settings/filters/edit_filter.h"

#include "lang_auto.h"
#include "extras/extras_settings.h"
#include "extras/data/extras_database.h"
#include "extras/features/filters/filters_cache_controller.h"
#include "base/event_filter.h"
#include "base/platform/base_platform_info.h"
#include "boxes/delete_messages_box.h"
#include "core/mime_type.h"
#include "lang/lang_text_entity.h"
#include "media/audio/media_audio.h"
#include "media/view/media_view_pip.h"
#include "styles/style_boxes.h"
#include "styles/style_layers.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"
#include "ui/ui_utility.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text.h"
#include "ui/toast/toast.h"
#include "ui/widgets/checkbox.h"
#include "ui/widgets/settings_toggle.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"

namespace Settings {

std::vector<char> generate_uuid_bytes() {
	// stolen somewhere from Internet
	std::random_device rd;
	std::mt19937 gen(rd());
	std::uniform_int_distribution<uint32_t> dist;

	std::vector<uint8_t> bytes(16);
	for (int i = 0; i < 16; i += 4) {
		uint32_t random_chunk = dist(gen);
		bytes[i] = random_chunk & 0xFF;
		bytes[i + 1] = (random_chunk >> 8) & 0xFF;
		bytes[i + 2] = (random_chunk >> 16) & 0xFF;
		bytes[i + 3] = (random_chunk >> 24) & 0xFF;
	}
	bytes[6] = (bytes[6] & 0x0F) | 0x40;
	bytes[8] = (bytes[8] & 0x3F) | 0x80;

	return std::vector<char>(bytes.begin(), bytes.end());
}

bool validateRegex(const icu::UnicodeString& pattern, std::string& errorMsg) {
	UErrorCode status = U_ZERO_ERROR;
	UParseError parseError;

	icu::RegexPattern* regexPattern = icu::RegexPattern::compile(
		pattern,
		0, // flags
		parseError,
		status
	);

	if (U_FAILURE(status)) {
		auto errorCodeNormalized = std::string(u_errorName(status));
		errorCodeNormalized = errorCodeNormalized.substr(8); // skip U_REGEX_
		std::ranges::transform(
			errorCodeNormalized,
			errorCodeNormalized.begin(),
			[](unsigned char c)
			{
				if (c == '_') {
					return std::tolower(' ');
				}
				return std::tolower(c);
			});
		errorCodeNormalized[0] = std::toupper(errorCodeNormalized[0]);
		errorMsg = errorCodeNormalized + " at " + std::to_string(parseError.offset);

		if (parseError.preContext[0] != 0 || parseError.postContext[0] != 0) {
			icu::UnicodeString pre(parseError.preContext);
			icu::UnicodeString post(parseError.postContext);
			std::string preStr, postStr;
			pre.toUTF8String(preStr);
			post.toUTF8String(postStr);
			errorMsg += " (near: '" + preStr + "' -> '" + postStr + "')";
		}

		delete regexPattern;
		return false;
	}

	delete regexPattern;
	return true;
}

not_null<Ui::SlideWrap<Ui::FlatLabel>*> AddError(
	not_null<Ui::VerticalLayout*> content,
	Ui::InputField *input) {

	std::string errorText;
	validateRegex(icu::UnicodeString::fromUTF8("("), errorText);

	const auto error = content->add(
		object_ptr<Ui::SlideWrap<Ui::FlatLabel>>(
			content,
			object_ptr<Ui::FlatLabel>(
				content,
				QString::fromStdString(errorText) + QString::fromStdString(errorText),
				st::settingLocalPasscodeError), st::settingsCheckboxPadding));
	error->hide(anim::type::instant);
	if (input) {
		input->changes() | rpl::on_next(
			[=]
			{
				error->hide(anim::type::normal);
			},
			input->lifetime());
	}
	return error;
};

void RegexEditBuilder(
	not_null<Ui::GenericBox*> box,
	RegexFilter *filter,
	const Fn<void(RegexFilter)> &onDone,
	std::optional<long long> dialogId,
	bool showToast
) {
	const auto isNew = !filter || filter->id.empty();
	const auto effectiveDialogId = dialogId.has_value()
		? dialogId
		: (filter ? filter->dialogId : std::nullopt);

	RegexFilter data;

	if (filter) {
		box->setTitle(isNew ? tr::extras_RegexFiltersAdd() : tr::extras_RegexFiltersEdit());
		data = *filter;
	} else {
		box->setTitle(tr::extras_RegexFiltersAdd());
		data.enabled = true;
		data.caseInsensitive = true;
		data.reversed = false;
	}

	const auto regexValue = box->addRow(
		object_ptr<Ui::InputField>(
			box->verticalLayout(),
			st::windowFilterNameInput,
			Ui::InputField::Mode::MultiLine,
			tr::extras_RegexFiltersPlaceholder()),
		st::markdownLinkFieldPadding);
	const auto errorText = AddError(box->verticalLayout(), regexValue);
	const auto enabled = box->addRow(
		object_ptr<Ui::SettingsToggle>(
			box,
			tr::extras_EnableExpression(tr::now),
			data.enabled,
			st::settingsButtonNoIcon), style::margins());
	const auto caseInsensitive = box->addRow(
		object_ptr<Ui::SettingsToggle>(
			box,
			tr::extras_CaseInsensitiveExpression(tr::now),
			data.caseInsensitive,
			st::settingsButtonNoIcon), style::margins());
	const auto reversed = box->addRow(
		object_ptr<Ui::SettingsToggle>(
			box,
			tr::extras_ReversedExpression(tr::now),
			data.reversed,
			st::settingsButtonNoIcon), style::margins());

	// 仅在存在指定会话上下文时允许配置是否共享，无会话时（全局设置）天然为共享过滤器。
	Ui::SettingsToggle *sharedToggle = nullptr;
	if (effectiveDialogId.has_value()) {
		const auto initialIsShared = isNew ? false : !data.dialogId.has_value();
		sharedToggle = box->addRow(
			object_ptr<Ui::SettingsToggle>(
				box,
				tr::extras_RegexFiltersShared(tr::now),
				initialIsShared,
				st::settingsButtonNoIcon), style::margins());
	}

	regexValue->setText(QString::fromStdString(data.text));

	auto saveAndClose = [=, id = data.id]
	{
		const auto text = regexValue->getTextWithTags().text;
		if (text.isEmpty()) {
			return;
		}

		std::string error;
		if (!validateRegex(icu::UnicodeString::fromUTF8(text.toStdString()), error)) {
			errorText->entity()->setText(QString::fromStdString(error));
			errorText->show(anim::type::normal);
			return;
		}

		RegexFilter newFilter;
		newFilter.text = regexValue->getTextWithTags().text.toStdString();
		newFilter.enabled = enabled->checked();
		newFilter.caseInsensitive = caseInsensitive->checked();
		newFilter.reversed = reversed->checked();

		// 根据开关显式状态赋值 dialogId，非共享且有上下文时绑定具体对话。
		const auto isShared = sharedToggle ? sharedToggle->checked() : true;
		if (!isShared && effectiveDialogId.has_value()) {
			newFilter.dialogId = effectiveDialogId;
		} else {
			newFilter.dialogId = std::nullopt;
		}

		if (!id.empty()) {
			newFilter.id = id;
		} else {
			newFilter.id = generate_uuid_bytes();
		}

		box->closeBox();

		// 异步持久化并通知刷新缓存，完全移除原有的 ShowToastWithAction 逆向撤销。
		crl::async([=]
		{
			Database::addRegexFilter(newFilter);
			FiltersCacheController::rebuildCache();

			crl::on_main([=]
			{
				if (onDone) {
					onDone(newFilter);
				}
				FiltersCacheController::fireUpdate();

				if (!showToast) {
					return;
				}
				auto config = Ui::Toast::Config{
					.text = newFilter.dialogId.has_value()
						? tr::extras_RegexFilters(tr::now, tr::rich)
						: tr::extras_RegexFilterBulletinText(tr::now, tr::rich),
					.adaptive = true,
				};
				Ui::Toast::Show(std::move(config));
			});
		});
	};

	regexValue->submits() | rpl::on_next(saveAndClose, regexValue->lifetime());
	box->addButton(tr::lng_settings_save(), saveAndClose);
	box->addButton(tr::lng_cancel(),
				   [=]
				   {
					   box->closeBox();
				   });
	box->setFocusCallback([=] {
		regexValue->setFocusFast();
	});

	errorText->entity()->resizeToWidth(box->width());
	errorText->resizeToWidth(box->width());
}

object_ptr<Ui::GenericBox> RegexEditBox(RegexFilter *filter,
										const Fn<void(RegexFilter)> &onDone,
										std::optional<long long> dialogId,
										bool showToast) {
	return Box(RegexEditBuilder, filter, onDone, dialogId, showToast);
}

} // namespace Settings
