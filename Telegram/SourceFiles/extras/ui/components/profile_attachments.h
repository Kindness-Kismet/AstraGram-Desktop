#pragma once

namespace Ui {
class VerticalLayout;
} // namespace Ui

namespace Info {
class Controller;
} // namespace Info

namespace Extras::Profile {

// 收藏来源仅展示来源音乐，不混入当前用户的频道和风险提示。
void setupAttachments(
	not_null<::Ui::VerticalLayout*> layout,
	not_null<Info::Controller*> controller,
	not_null<PeerData*> peer,
	bool musicOnly = false);

} // namespace Extras::Profile
