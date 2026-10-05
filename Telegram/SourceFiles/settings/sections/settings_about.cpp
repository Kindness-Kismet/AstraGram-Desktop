#include "settings/sections/settings_about.h"

#include "api/api_credits.h"
#include "boxes/about_box.h"
#include "boxes/star_gift_box.h"
#include "core/application.h"
#include "core/file_utilities.h"
#include "data/components/credits.h"
#include "extras/extras_settings.h"
#include "extras/ui/extras_logo.h"
#include "lang/lang_keys.h"
#include "lang/lang_tag.h"
#include "main/main_session.h"
#include "settings/sections/settings_business.h"
#include "settings/sections/settings_credits.h"
#include "settings/sections/settings_premium.h"
#include "settings/sections/settings_main.h"
#include "settings/sections/settings_update.h"
#include "settings/settings_builder.h"
#include "settings/settings_card_layout.h"
#include "settings/settings_common_session.h"
#include "ui/painter.h"
#include "ui/widgets/labels.h"
#include "ui/wrap/slide_wrap.h"
#include "ui/wrap/vertical_layout.h"
#include "window/window_session_controller.h"
#include "styles/style_menu_icons.h"
#include "styles/style_settings.h"

namespace Settings {
namespace {

class About final : public Section<About> {
public:
	About(QWidget *parent, not_null<Window::SessionController*> controller);

	rpl::producer<QString> title() override {
		return tr::lng_menu_about();
	}
};

void buildServiceButtons(Builder::SectionBuilder &builder) {
	const auto session = builder.session();
	const auto controller = builder.controller();
	const auto showOther = builder.showOther();

	builder.addButton({
		.id = u"main/premium"_q,
		.title = tr::lng_premium_summary_title(),
		.icon = { &st::menuIconPremium },
		.onClick = [=] {
			controller->setPremiumRef("settings");
			showOther(PremiumId());
		},
		.keywords = { u"subscription"_q },
	});

	session->credits().load();
	builder.addButton({
		.id = u"main/credits"_q,
		.title = tr::lng_settings_credits(),
		.icon = { &st::menuIconStar },
		.label = session->credits().balanceValue(
		) | rpl::map([](CreditsAmount c) {
			return c
				? Lang::FormatCreditsAmountToShort(c).string
				: QString();
		}),
		.onClick = [=] {
			controller->setPremiumRef("settings");
			showOther(CreditsId());
		},
		.keywords = { u"stars"_q, u"balance"_q },
	});

	session->credits().tonLoad();
	builder.addButton({
		.id = u"main/currency"_q,
		.title = tr::lng_settings_currency(),
		.icon = { &st::menuIconTon },
		.label = session->credits().tonBalanceValue(
		) | rpl::map([](CreditsAmount c) {
			return c ? Lang::FormatCreditsAmountToShort(c).string : u""_q;
		}),
		.onClick = [=] {
			controller->setPremiumRef("settings");
			showOther(CurrencyId());
		},
		.keywords = { u"ton"_q, u"crypto"_q, u"wallet"_q },
		.shown = session->credits().tonBalanceValue(
		) | rpl::map([](CreditsAmount c) { return !c.empty(); }),
	});

	builder.addButton({
		.id = u"main/business"_q,
		.title = tr::lng_business_title(),
		.icon = { .icon = &st::menuIconShop },
		.onClick = [=] { showOther(BusinessId()); },
		.keywords = { u"work"_q, u"company"_q },
	});

	builder.addButton({
		.id = u"main/send-gift"_q,
		.title = tr::lng_settings_gift_premium(),
		.icon = { .icon = &st::menuIconGiftPremium, .newBadge = true },
		.onClick = [=] { Ui::ChooseStarGiftRecipient(controller); },
		.keywords = { u"present"_q, u"send"_q },
		.shown = session->premiumPossibleValue() | rpl::map([=] {
			return session->premiumCanBuy();
		}),
	});
}

void buildServices(Builder::SectionBuilder &builder) {
	if (!builder.container()) {
		buildServiceButtons(builder);
		return;
	}
	builder.addPageContent([](const Builder::WidgetContext &ctx) {
		const auto services = ctx.container->add(
			object_ptr<Ui::SlideWrap<Ui::VerticalLayout>>(
				ctx.container,
				object_ptr<Ui::VerticalLayout>(ctx.container)));
		const auto content = services->entity();
		AddCardTitle(content, tr::extras_AboutServices());
		auto context = ctx;
		context.container = AddCardGroup(content);
		context.cardLayout = false;
		auto builder = Builder::SectionBuilder(std::move(context));
		buildServiceButtons(builder);
		services->toggleOn(ctx.controller->session().premiumPossibleValue());
		services->finishAnimating();
	});
}

const auto kMeta = Builder::BuildHelper({
	.id = About::Id(),
	.parentId = MainId(),
	.title = &tr::lng_menu_about,
	.icon = &st::menuIconInfo,
}, [](Builder::SectionBuilder &builder) {
	builder.addPageContent([](const Builder::WidgetContext &ctx) {
		const auto header = ctx.container->add(
			object_ptr<Ui::VerticalLayout>(ctx.container),
			st::settingsAboutHeaderPadding);
		const auto logo = header->add(object_ptr<Ui::RpWidget>(header));
		logo->setObjectName(u"about-app-icon"_q);
		logo->resize(0, st::settingsAboutLogoSize);
		ExtrasSettings::getInstance().appIconChanges() | rpl::on_next([=] {
			logo->update();
		}, logo->lifetime());
		logo->paintRequest() | rpl::on_next([=] {
			auto p = Painter(logo);
			auto hq = PainterHighQualityEnabler(p);
			const auto size = st::settingsAboutLogoSize;
			const auto rect = QRect((logo->width() - size) / 2, 0, size, size);
			p.drawImage(rect, ExtrasAssets::currentAppLogo());
		}, logo->lifetime());
		header->add(object_ptr<Ui::FlatLabel>(
			header,
			rpl::single(u"AstraGram"_q),
			st::settingsAboutName), st::settingsAboutNamePadding, style::al_top);
		header->add(object_ptr<Ui::FlatLabel>(
			header,
			rpl::single(currentVersionText()),
			st::settingsAboutVersion), {}, style::al_top
		)->setObjectName(u"about-version-text"_q);
	});
	builder.addButton({
		.id = u"about/version"_q,
		.title = tr::extras_AboutChangelog(),
		.icon = { &st::menuIconInfo },
		.onClick = [] { File::OpenUrl(Core::App().changelogLink()); },
		.keywords = { u"version"_q, u"changelog"_q },
	});
	builder.addButton({
		.id = u"about/github"_q,
		.title = rpl::single(u"GitHub"_q),
		.icon = { &st::menuIconLink },
		.onClick = [] { File::OpenUrl(projectGithubLink()); },
		.keywords = { u"source"_q, u"project"_q },
	});

	if (HasUpdate()) {
		builder.addSubsectionTitle(tr::lng_settings_version_info());
		BuildUpdateSection(builder);
		builder.addDivider();
	}

	buildServices(builder);
	builder.addSubsectionTitle(tr::extras_AboutHelp());
	const auto controller = builder.controller();
	builder.addButton({
		.id = u"main/faq"_q,
		.title = tr::lng_settings_faq(),
		.icon = { &st::menuIconFaq },
		.onClick = [=] { OpenFaq(controller); },
		.keywords = { u"help"_q, u"support"_q, u"questions"_q },
	});
	builder.addButton({
		.id = u"main/ask-question"_q,
		.title = tr::lng_settings_ask_question(),
		.icon = { &st::menuIconDiscussion },
		.onClick = [=] { OpenAskQuestionConfirm(controller); },
		.keywords = { u"contact"_q, u"feedback"_q },
	});
	builder.addPageContent([](const Builder::WidgetContext &ctx) {
		const auto label = ctx.container->add(
			object_ptr<Ui::FlatLabel>(ctx.container, aboutText(), st::settingsAboutVersion),
			st::settingsCardHintPadding,
			style::al_top);
		label->setLinksTrusted();
	});
});

About::About(
	QWidget *parent,
	not_null<Window::SessionController*> controller)
: Section(parent, controller) {
	const auto content = Ui::CreateChild<Ui::VerticalLayout>(this);
	build(content, kMeta.build);
	Ui::ResizeFitChild(this, content);
}

} // namespace

Type AboutId() {
	return About::Id();
}

} // namespace Settings
