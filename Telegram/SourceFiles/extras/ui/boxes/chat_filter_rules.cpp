#include "extras/ui/boxes/chat_filter_rules.h"

#include "lang/lang_keys.h"
#include "ui/boxes/dialog_layout.h"
#include "ui/vertical_list.h"
#include "ui/widgets/fields/input_field.h"
#include "ui/wrap/vertical_layout.h"
#include "styles/style_layers.h"
#include "styles/style_window.h"
#include "styles/style_widgets.h"

namespace ExtrasChatFilters {

Fn<Data::ChatFilterLocalRules()> setupRulesFields(
		not_null<Ui::VerticalLayout*> content,
		const Data::ChatFilterLocalRules &rules,
		Fn<void(Data::ChatFilterLocalRules)> changed) {
	Ui::AddSkip(content, st::boxMediumSkip);
	Ui::AddSubsectionTitle(content, tr::extras_ChatFilterLocalRules());
	const auto include = content->add(object_ptr<Ui::InputField>(
		content,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		tr::extras_ChatFilterLocalInclude()), st::boxRowPadding);
	Ui::AddSkip(content);
	const auto exclude = content->add(object_ptr<Ui::InputField>(
		content,
		st::defaultInputField,
		Ui::InputField::Mode::MultiLine,
		tr::extras_ChatFilterLocalExclude()), st::boxRowPadding);
	include->setObjectName(u"chatFilter.localInclude"_q);
	exclude->setObjectName(u"chatFilter.localExclude"_q);
	include->setSubmitSettings(Ui::InputField::SubmitSettings::None);
	exclude->setSubmitSettings(Ui::InputField::SubmitSettings::None);
	include->setText(rulesToText(rules.include));
	exclude->setText(rulesToText(rules.exclude));
	const auto collect = [=] {
		return Data::ChatFilterLocalRules{
			parseRules(include->getLastText()),
			parseRules(exclude->getLastText()),
		};
	};
	rpl::merge(include->changes(), exclude->changes())
	| rpl::on_next([=] { changed(collect()); }, content->lifetime());
	Ui::AddSkip(content);
	Ui::AddDialogText(content, tr::extras_ChatFilterLocalAbout());
	return collect;
}

} // namespace ExtrasChatFilters
