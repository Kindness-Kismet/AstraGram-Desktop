#include "chat_helpers/field_autocomplete.h"

#include "lang/lang_keys.h"
#include "ui/widgets/labels.h"
#include "ui/widgets/tooltip.h"
#include "ui/wrap/padding_wrap.h"
#include "styles/style_chat_helpers.h"
#include "styles/style_dialogs.h"
#include "styles/style_widgets.h"

namespace ChatHelpers {

void FieldAutocomplete::createEphemeralHint(QRect rect) {
	const auto parent = parentWidget();
	_ephemeralHint = base::make_unique_q<Ui::ImportantTooltip>(
		parent,
		object_ptr<Ui::PaddingWrap<Ui::FlatLabel>>(
			parent,
			Ui::MakeNiceTooltipLabel(
				parent,
				tr::lng_ephemeral_command_tooltip(Ui::Text::WithEntities),
				st::dialogsStoriesTooltipMaxWidth,
				st::ttlMediaImportantTooltipLabel),
			st::defaultImportantTooltip.padding),
		st::dialogsStoriesTooltip);
	_ephemeralHint->pointAt(rect, RectPart::Top);
	_ephemeralHint->toggleAnimated(true);
}

void FieldAutocomplete::showPendingEphemeralHint() {
	if (_ephemeralHintRect.isEmpty() || isHidden() || _hiding) {
		return;
	}
	if (_ephemeralHint) {
		_ephemeralHint->pointAt(_ephemeralHintRect, RectPart::Top);
		_ephemeralHint->toggleAnimated(true);
	} else {
		createEphemeralHint(_ephemeralHintRect);
	}
}

void FieldAutocomplete::hideEphemeralHint() {
	_ephemeralHintRect = QRect();
	_ephemeralHintTimer.cancel();
	if (_ephemeralHint) {
		_ephemeralHint->toggleAnimated(false);
	}
}

} // namespace ChatHelpers
