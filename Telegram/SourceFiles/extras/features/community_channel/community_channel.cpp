#include "extras/features/community_channel/community_channel.h"

#include "extras/extras_settings.h"
#include "lang/lang_keys.h"
#include "ui/boxes/confirm_box.h"
#include "ui/widgets/buttons.h"
#include "window/window_session_controller.h"
#include "window/window_session_controller_link_info.h"
#include "crl/crl_on_main.h"

namespace Extras::CommunityChannel {
namespace {

// 每次启动最多提示一次；取消不改变下次启动的选择。
bool inviteShown = false;

} // namespace

void maybeShowInvite(not_null<Window::SessionController*> controller) {
	if (inviteShown || !controller->isPrimary()
		|| !ExtrasSettings::getInstance().showCommunityChannelInvite()) {
		return;
	}
	inviteShown = true;
	crl::on_main(controller, [=] {
		auto box = Box([=](not_null<Ui::GenericBox*> dialog) {
			dialog->setObjectName(u"communityChannel/invite"_q);
			Ui::ConfirmBox(dialog, {
				.text = tr::extras_CommunityChannelInviteText(),
				.confirmed = crl::guard(controller, [=](Fn<void()> close) {
					ExtrasSettings::getInstance().setShowCommunityChannelInvite(false);
					close();
					controller->showPeerByLink(Window::PeerByLinkInfo{
						.usernameOrId = u"MaterialDesign3"_q,
					});
				}),
				.confirmText = tr::lng_box_ok(),
				.cancelText = tr::lng_cancel(),
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
}

} // namespace Extras::CommunityChannel
