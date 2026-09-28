#include "ayu/ui/boxes/emoji_packs_box.h"

#include "ayu/features/emoji_packs/emoji_packs.h"
#include "core/file_utilities.h"
#include "lang/lang_keys.h"
#include "settings/settings_common.h"
#include "styles/style_settings.h"
#include "ui/emoji_config.h"
#include "ui/layers/box_content.h"
#include "ui/text/format_values.h"
#include "ui/toast/toast.h"
#include "ui/widgets/buttons.h"
#include "ui/wrap/vertical_layout.h"

namespace Ayu::EmojiPacks {
namespace {

QString errorText(ImportError error) {
	switch (error) {
	case ImportError::Busy: return tr::ayu_EmojiPackImporting(tr::now);
	case ImportError::Read: return tr::ayu_EmojiPackReadError(tr::now);
	case ImportError::Font: return tr::ayu_EmojiPackFontError(tr::now);
	case ImportError::Full: return tr::ayu_EmojiPackFull(tr::now);
	case ImportError::Write: return tr::ayu_EmojiPackWriteError(tr::now);
	case ImportError::Download: return tr::ayu_EmojiPackDownloadError(tr::now);
	case ImportError::Cancelled: return {};
	case ImportError::None: return {};
	}
	Unexpected("Invalid emoji import error");
}

// 预设行的状态：未安装显示下载大小，下载中显示进度，转换中显示导入提示。
struct PresetState {
	bool downloading = false;
	bool converting = false;
	DownloadProgress progress;
};

QString stateText(const PresetState &state, int64 size) {
	if (state.converting) {
		return tr::ayu_EmojiPackImporting(tr::now);
	}
	if (state.downloading) {
		const auto total = state.progress.total;
		const auto percent = (total > 0)
			? std::clamp((state.progress.already * 100) / float64(total), 0., 100.)
			: 0.;
		return tr::lng_emoji_set_loading(
			tr::now,
			lt_percent,
			QString::number(int(base::SafeRound(percent))) + '%',
			lt_progress,
			Ui::FormatDownloadText(state.progress.already, total));
	}
	return tr::lng_emoji_set_download(
		tr::now,
		lt_size,
		Ui::FormatSizeText(size));
}

} // namespace

void addPresetRows(not_null<Ui::VerticalLayout*> content, Fn<void()> refresh) {
	const auto packs = installed();
	for (const auto &preset : presets()) {
		const auto found = std::find_if(packs.begin(), packs.end(), [&](const Pack &pack) {
			return pack.hash == preset.hash;
		});
		// 已安装的预设由官方样式的行渲染，这里只处理尚未下载的。
		if (found != packs.end()) {
			continue;
		}
		const auto state = content->lifetime()
			.make_state<rpl::variable<PresetState>>();
		auto label = state->value() | rpl::map([=](const PresetState &value) {
			return stateText(value, preset.size);
		});
		// 品牌名不翻译，直接用清单登记的名称。
		const auto button = Settings::AddButtonWithLabel(content,
			rpl::single(preset.name), std::move(label), st::settingsButtonNoIcon);
		button->setObjectName(u"emoji/preset/"_q + preset.id);
		button->addClickHandler([=] {
			// 再次点击表示取消，下载中不重复发起。
			if (state->current().downloading) {
				cancelPreset(preset.id);
				*state = PresetState();
				return;
			}
			if (state->current().converting) {
				return;
			}
			*state = PresetState{ .downloading = true };
			installPreset(preset,
				crl::guard(content, [=](DownloadProgress progress) {
					if (!state->current().downloading) {
						return;
					}
					*state = PresetState{
						.downloading = true,
						.progress = progress,
					};
				}),
				crl::guard(content, [=](ImportResult result) {
					if (result.error != ImportError::None) {
						*state = PresetState();
						// 用户主动取消时不打扰，其余失败都要给出原因。
						if (result.error != ImportError::Cancelled) {
							Ui::Toast::Show(errorText(result.error));
						}
						return;
					}
					*state = PresetState{ .converting = true };
					Ui::Emoji::SwitchToSet(result.pack.id,
						crl::guard(content, [=](bool success) {
							*state = PresetState();
							if (success) {
								refresh();
							} else {
								Ui::Toast::Show(
									tr::ayu_EmojiPackSwitchError(tr::now));
							}
						}));
				}));
		});
	}
}

void addImportButton(not_null<Ui::BoxContent*> box, Fn<void()> refresh) {
	const auto busy = box->lifetime().make_state<rpl::variable<bool>>(false);
	const auto button = box->addLeftButton(tr::ayu_EmojiPackImport(), [=] {
		if (busy->current()) {
			return;
		}
		FileDialog::GetOpenPath(box.get(), tr::ayu_EmojiPackImport(tr::now),
			tr::ayu_EmojiPackFileFilter(tr::now),
			crl::guard(box, [=](FileDialog::OpenResult &&result) {
				if (result.paths.isEmpty()) {
					return;
				}
				*busy = true;
				importFont(result.paths.front(), crl::guard(box, [=](ImportResult result) {
					*busy = false;
					if (result.error != ImportError::None) {
						Ui::Toast::Show(errorText(result.error));
						return;
					}
					refresh();
					Ui::Emoji::SwitchToSet(result.pack.id, crl::guard(box, [](bool success) {
						if (!success) {
							Ui::Toast::Show(tr::ayu_EmojiPackSwitchError(tr::now));
						}
					}));
				}));
			}));
	});
	button->setObjectName(u"emoji/import"_q);
	busy->value() | rpl::on_next([=](bool importing) {
		button->setDisabled(importing);
		button->setText(importing
			? tr::ayu_EmojiPackImporting()
			: tr::ayu_EmojiPackImport());
	}, box->lifetime());
}

} // namespace Ayu::EmojiPacks
