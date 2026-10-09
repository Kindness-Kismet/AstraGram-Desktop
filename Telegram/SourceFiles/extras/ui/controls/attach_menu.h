#pragma once

class PeerData;

namespace Ui {
class DropdownMenu;
}

namespace Webrtc {
enum class RecordAvailability : uchar;
}

namespace ExtrasUi {

// 加号自身不触发操作：开启悬停弹出时悬停展开，关闭时点击只切换菜单。
void setupAttachMenu(
	not_null<QWidget*> button,
	not_null<Ui::DropdownMenu*> menu);

// 附件菜单任一项的显示开关变化时重建菜单。
[[nodiscard]] rpl::producer<> attachMenuChanges();

// shown 表示显示录制分组，voice / round 表示对应录制项可用，不可用时置灰。
struct RecordMenuOptions {
	bool shown = false;
	bool voice = false;
	bool round = false;
};

// 设置开关决定是否显示；聊天权限、录制设备、系统授权或通话占用不满足时置灰。
[[nodiscard]] RecordMenuOptions recordMenuOptions(
	not_null<PeerData*> peer,
	Webrtc::RecordAvailability availability);

// 系统授权、采集占用或通话状态变化时重建录音菜单。
[[nodiscard]] rpl::producer<> recordMenuChanges();

// 在菜单末尾追加可展开的“录制消息”分组，前面有其他项时先加分隔线。
void addRecordMenu(
	not_null<Ui::DropdownMenu*> menu,
	RecordMenuOptions options,
	Fn<void(bool round)> record);

}
