#include "ui/boxes/dialog_layout.h"

#include "lang/lang_text_entity.h"
#include "ui/layers/box_content.h"
#include "ui/layers/layer_widget.h"
#include "ui/widgets/buttons.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"

namespace Ui {

bool IsDialogContent(not_null<QWidget*> widget) {
	for (auto current = widget.get(); current; current = current->parentWidget()) {
		if (dynamic_cast<BoxContent*>(current)
			|| dynamic_cast<LayerWidget*>(current)) {
			return true;
		}
	}
	return false;
}

const style::SettingsButton &DialogButtonStyle(
		not_null<RpWidget*> owner,
		const style::SettingsButton &base,
		bool overlay) {
	const auto result = owner->lifetime().make_state<style::SettingsButton>(base);
	result->style = st::boxTextStyle;
	result->height = st::boxTextStyle.font->height;
	result->padding = {
		st::boxRowPadding.left(),
		st::boxLittleSkip,
		st::boxRowPadding.right(),
		st::boxLittleSkip,
	};
	result->toggleSkip = st::boxRowPadding.right();
	result->textBg = overlay ? st::boxBg : st::transparent;
	result->textBgOver = st::windowBgOver;
	return *result;
}

not_null<FlatLabel*> AddDialogText(
		not_null<VerticalLayout*> container,
		rpl::producer<QString> text,
		const style::margins &margins,
		const style::DividerLabel &st) {
	return AddDialogText(
		container,
		std::move(text) | rpl::map(tr::marked),
		margins,
		st);
}

not_null<FlatLabel*> AddDialogText(
		not_null<VerticalLayout*> container,
		rpl::producer<TextWithEntities> text,
		const style::margins &margins,
		const style::DividerLabel &st) {
	return container->add(
		object_ptr<FlatLabel>(container, std::move(text), st.label),
		margins);
}

} // namespace Ui
