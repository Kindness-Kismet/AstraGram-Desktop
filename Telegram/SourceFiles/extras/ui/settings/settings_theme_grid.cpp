#include "extras/ui/settings/settings_theme_grid.h"

#include "extras/data/local_themes.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "ui/effects/ripple_animation.h"
#include "ui/painter.h"
#include "ui/widgets/checkbox.h"
#include "window/themes/window_theme.h"
#include "window/themes/window_themes_cloud_list.h"
#include "window/window_controller.h"
#include "styles/style_boxes.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"
#include "styles/style_window.h"

namespace Extras::ThemeGrid {
namespace {

class RemoveButton final : public Ui::RippleButton {
public:

	explicit RemoveButton(not_null<QWidget*> parent)
	: RippleButton(parent, st::defaultRippleAnimation) {
		const auto size = st::settingsThemePreviewSize.width() * 3 / 10;
		resize(size, size);
		setPointerCursor(true);
	}

private:

	void paintEvent(QPaintEvent *event) override {
		auto p = Painter(this);
		auto highQuality = PainterHighQualityEnabler(p);
		p.setPen(Qt::NoPen);
		p.setBrush(isOver() ? st::windowBgOver : st::windowBg);
		p.drawEllipse(rect());
		paintRipple(p, 0, 0);
		const auto &icon = st::menuIconDelete;
		const auto side = width() * 2 / 3;
		p.translate((width() - side) / 2., (height() - side) / 2.);
		p.scale(side / float64(icon.width()), side / float64(icon.height()));
		icon.paint(p, 0, 0, icon.width(), isOver()
			? st::attentionButtonFg->c
			: st::windowSubTextFg->c);
	}

	QImage prepareRippleMask() const override {
		return Ui::RippleAnimation::EllipseMask(size());
	}
};

void confirmRemoval(
		not_null<Window::Controller*> window,
		const QString &path) {
	using namespace Window::Theme;

	window->show(Ui::MakeConfirmBox({
		.text = tr::lng_theme_delete_sure(),
		.confirmed = [=](Fn<void()> &&close) {
			close();
			const auto active = Background()->themeObject().pathAbsolute == path;
			if (!LocalThemes::remove(path)) {
				window->show(Ui::MakeInformBox(tr::extras_LocalThemeRemoveError()));
				return;
			}
			if (!active) {
				return;
			}
			if (Background()->editingTheme()) {
				Background()->clearEditingTheme(ClearEditing::KeepChanges);
				window->showRightColumn(nullptr);
			}
			ResetToSomeDefault();
			KeepApplied();
		},
		.confirmText = tr::lng_theme_delete(),
	}));
}

struct LocalPreview {
	QString path;
	std::unique_ptr<Ui::Radiobutton> button;
};

[[nodiscard]] LocalPreview createPreview(
		not_null<Window::Controller*> window,
		not_null<Ui::RpWidget*> container,
		const std::shared_ptr<Ui::RadiobuttonGroup> &group,
		const Data::CloudTheme &theme,
		int index) {
	using namespace Window::Theme;

	const auto path = theme.slug;
	auto check = std::make_unique<CloudListCheck>(false);
	if (const auto colors = ColorsFromThemeFile(path)) {
		check->setColors(*colors);
	}
	auto button = std::make_unique<Ui::Radiobutton>(
		container, group, index, theme.title,
		st::settingsTheme, std::move(check));
	button->setObjectName(u"localTheme.%1"_q.arg(theme.id));
	button->setCheckAlignment(style::al_top);
	button->setAllowTextLines(2);
	button->setTextBreakEverywhere();
	button->addClickHandler([=] {
		if (!Apply(path)) {
			window->show(Ui::MakeInformBox(tr::extras_LocalThemeLoadError()));
			return;
		}
		KeepApplied();
	});
	const auto remove = Ui::CreateChild<RemoveButton>(button.get());
	remove->setObjectName(u"localThemeRemove.%1"_q.arg(theme.id));
	remove->setAccessibleName(tr::lng_theme_delete(tr::now));
	remove->addClickHandler([=] { confirmRemoval(window, path); });
	button->widthValue() | rpl::on_next([=](int width) {
		remove->moveToRight(st::settingsThemeMinSkip, st::settingsThemeMinSkip, width);
	}, remove->lifetime());
	button->show();
	remove->show();
	return { path, std::move(button) };
}

struct GridState {
	std::vector<LocalPreview> local;
	std::shared_ptr<Ui::RadiobuttonGroup> group
		= std::make_shared<Ui::RadiobuttonGroup>();
};

} // namespace

void setupThemeGrid(
		not_null<Window::Controller*> window,
		not_null<Ui::RpWidget*> container,
		std::vector<not_null<Ui::Radiobutton*>> embedded,
		bool includeLocal) {
	using namespace Window::Theme;

	const auto state = container->lifetime().make_state<GridState>();
	const auto layout = [=] {
		const auto padding = st::settingsButtonNoIcon.padding;
		const auto width = container->width() - padding.left() - padding.right();
		if (width <= 0) {
			return;
		}
		const auto skip = st::settingsThemeMinSkip;
		const auto single = std::min(width, st::settingsThemePreviewSize.width());
		const auto columns = std::max(1, (width + skip) / (single + skip));
		const auto step = columns > 1
			? (width - single) / float64(columns - 1)
			: 0.;
		auto buttons = embedded;
		for (const auto &preview : state->local) {
			buttons.push_back(preview.button.get());
		}
		auto top = 0;
		auto rowHeight = 0;
		for (auto i = 0; i != int(buttons.size()); ++i) {
			if (i && !(i % columns)) {
				top += rowHeight + st::themesSmallSkip;
				rowHeight = 0;
			}
			const auto button = buttons[i];
			button->resizeToWidth(single);
			button->moveToLeft(
				padding.left() + int(base::SafeRound((i % columns) * step)),
				top);
			accumulate_max(rowHeight, button->height());
		}
		container->resize(container->width(), top + rowHeight);
	};
	const auto refresh = [=] {
		const auto themes = includeLocal
			? LocalThemes::list()
			: std::vector<Data::CloudTheme>();
		const auto same = ranges::equal(
			themes,
			state->local,
			std::equal_to<>(),
			&Data::CloudTheme::slug,
			&LocalPreview::path);
		if (!same) {
			state->local.clear();
			for (const auto &theme : themes) {
				state->local.push_back(createPreview(
					window, container, state->group, theme, int(state->local.size())));
			}
			layout();
		}
		const auto &path = Background()->themeObject().pathAbsolute;
		const auto current = ranges::find(state->local, path, &LocalPreview::path);
		state->group->setValue(current == end(state->local)
			? -1
			: int(current - begin(state->local)));
	};
	state->group->setChangedCallback([=](int) { refresh(); });
	container->widthValue() | rpl::on_next([=] { layout(); }, container->lifetime());
	if (includeLocal) {
		LocalThemes::changes() | rpl::on_next(refresh, container->lifetime());
		Background()->updates() | rpl::filter([](const BackgroundUpdate &update) {
			return update.type == BackgroundUpdate::Type::ApplyingTheme;
		}) | rpl::on_next([=] { refresh(); }, container->lifetime());
	}
	refresh();
}

} // namespace Extras::ThemeGrid
