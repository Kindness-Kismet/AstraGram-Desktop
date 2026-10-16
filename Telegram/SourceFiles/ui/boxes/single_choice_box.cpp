/*
This file is part of Telegram Desktop,
the official desktop application for the Telegram messaging service.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL
*/
#include "ui/boxes/single_choice_box.h"

#include "lang/lang_keys.h"
#include "ui/widgets/option_list.h"
#include "styles/style_layers.h"

void SingleChoiceBox(
		not_null<Ui::GenericBox*> box,
		SingleChoiceBoxArgs &&args) {
	box->setTitle(std::move(args.title));

	box->addButton(tr::lng_box_ok(), [=] { box->closeBox(); });

	const auto group = std::make_shared<Ui::RadiobuttonGroup>(
		args.initialSelection);

	const auto list = box->addRow(object_ptr<Ui::OptionList>(box));
	auto &&ints = ranges::views::ints(0, ranges::unreachable);
	for (const auto &[i, text] : ranges::views::zip(ints, args.options)) {
		list->addOption(object_ptr<Ui::Radiobutton>(
			list,
			group,
			i,
			text,
			args.st ? *args.st : st::defaultBoxCheckbox,
			args.radioSt ? *args.radioSt : st::defaultRadio));
	}
	const auto callback = args.callback.value();
	group->setChangedCallback([=](int value) {
		const auto weak = base::make_weak(box);
		callback(value);
		if (weak) {
			box->closeBox();
		}
	});
}
