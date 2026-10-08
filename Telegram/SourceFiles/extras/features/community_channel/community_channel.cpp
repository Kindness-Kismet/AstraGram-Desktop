#include "extras/features/community_channel/community_channel.h"

#include "data/components/promo_suggestions.h"
#include "extras/extras_settings.h"
#include "lang/lang_keys.h"
#include "main/main_session.h"
#include "ui/boxes/confirm_box.h"
#include "ui/text/text_utilities.h"
#include "ui/widgets/buttons.h"
#include "window/window_controller.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"
#include "crl/crl_on_main.h"

namespace Extras::CommunityChannel {
#ifndef _DEBUG
namespace {

// 每次启动最多提示一次；取消不改变下次启动的选择。
bool inviteShown = false;

} // namespace
#endif // !_DEBUG

void maybeShowInvite(not_null<Window::SessionController*> controller) {
#ifndef _DEBUG
	if (inviteShown || !controller->isPrimary()
		|| !ExtrasSettings::getInstance().showCommunityChannelInvite()) {
		return;
	}
	crl::on_main(controller, [=] {
		if (inviteShown || controller->window().locked()
			|| controller->session().promoSuggestions().setupEmailState()
				!= Data::SetupEmailState::None
			|| !ExtrasSettings::getInstance().showCommunityChannelInvite()) {
			return;
		}
		inviteShown = true;
		auto box = Box([=](not_null<Ui::GenericBox*> dialog) {
			dialog->setObjectName(u"communityChannel/invite"_q);
			const auto openChannel = crl::guard(controller, [=] {
				ExtrasSettings::getInstance().setShowCommunityChannelInvite(false);
				dialog->closeBox();
				controller->showPeerByLink(Window::PeerByLinkInfo{
					.usernameOrId = u"MaterialDesign3"_q,
				});
			});
			Ui::ConfirmBox(dialog, {
				.text = tr::extras_CommunityChannelInviteText(
					lt_link,
					rpl::single(Ui::Text::Link(
						u"@MaterialDesign3"_q,
						u"https://t.me/MaterialDesign3"_q)),
					tr::marked),
				.confirmed = openChannel,
				.confirmText = tr::lng_box_ok(),
				.cancelText = tr::lng_cancel(),
				.labelFilter = [=](const ClickHandlerPtr &, Qt::MouseButton button) {
					if (button == Qt::LeftButton || button == Qt::MiddleButton) {
						const auto callback = openChannel;
						callback();
					}
					return false;
				},
				.title = tr::extras_CommunityChannelInviteTitle(),
			});
			dialog->addLeftButton(tr::extras_CommunityChannelDontShow(), [=] {
				ExtrasSettings::getInstance().setShowCommunityChannelInvite(false);
				dialog->closeBox();
			})->setObjectName(u"communityChannel/dontShow"_q);
		});
		controller->show(std::move(box),
			Ui::LayerOption::KeepOther | Ui::LayerOption::ShowAfterOther);
	});
#endif // !_DEBUG
}

} // namespace Extras::CommunityChannel
