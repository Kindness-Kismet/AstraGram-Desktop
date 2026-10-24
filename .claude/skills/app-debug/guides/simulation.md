# 模拟模式

`simulation.secret [1..2000]` 在独立模拟配置中创建并打开加密对话样本，默认 120 条已读消息；只改内存，不握手、不发送。返回对话编号和首尾本地消息编号，可用于验证消息定位。
`simulation.cache-chats <0..10000>` 给已打开的缓存管理页注入本地统计样本，返回更新耗时和控件数量；关闭页面恢复，不写入实际缓存。

## 进入和操作

仅开发版提供。登录页点击“模拟模式”或执行 `simulation.enter [userId]`，默认用户编号 `999999999`，带会员身份。
进入时自动构造 30 个会话、15 个话题和 3 个收藏来源，无需下载素材。
固定场景按原生会话标签分类；使用 `simulation.open` 定位，使用 `simulation.trigger` 触发临时提示。

```bash
python .codex/skills/app-debug/scripts/cli.py app.start --profile simulation + simulation.enter
python .codex/skills/app-debug/scripts/cli.py simulation.list
python .codex/skills/app-debug/scripts/cli.py simulation.open group + screenshot.take
```

`app.info` 的 `simulationMode: true` 确认身份。已有真实会话时拒绝进入；已在模拟模式中再次调用保持现场。
重启后身份和场景消息消失，重新进入即可重建。设置和草稿仍保存在独立配置中；模拟编辑、删除不写入消息留档。
后台授权请求不能成功，因此模拟模式用于本地数据、控件和布局验证；联网业务使用已授权的测试账号。

## 固定场景

原生会话分组按私聊、群组、频道、机器人、话题、收藏夹、归档排列，尊重“隐藏全部对话”设置。
`simulation.list` 返回每项的 `key`、`name`、`category`、`categoryName`、`features` 和 `peerId`。
进入后还返回 `state`，查询实际未读、静音、归档、置顶、草稿、最新消息是否发送中、分类归属、话题状态及收藏来源数量。
编号供 `message.list/inspect`、`chat.draft` 等查询使用，不手写内部会话编号。

| 分类 | 键名 | 内容 |
|---|---|---|
| 私聊 | `private` | 对话置顶、普通、引用、已编辑、已删除、一天自动删除、禁止转发、已读、富文本与链接 |
| 私聊 | `draft` | 未读、静音、多行草稿 |
| 私聊 | `contact` | 陌生人的添加和屏蔽提示 |
| 私聊 | `blocked` | 已屏蔽用户的底部动作 |
| 私聊 | `deleted-account` | 已注销账号 |
| 私聊 | `business` | 商业机器人管理提示 |
| 私聊 | `paid` | 付费消息提示 |
| 私聊 | `sending` | 停在发送中的文字和图片，气泡、图片角标与对话列表显示时钟 |
| 群组 | `group` | 讨论、置顶和消息状态，图片、相册、文件、联系人、投票、静态贴纸 |
| 群组 | `restricted` | 发送限制 |
| 群组 | `requests` | 管理权限、加入申请、置顶和视频聊天管理菜单 |
| 群组 | `slow` | 慢速模式；每次打开刷新倒计时 |
| 群组 | `protected-group` | 内容保护和置顶 |
| 群组 | `send-as` | 本人与频道两种发送身份，默认选中频道，可通过输入框按钮在本地切换 |
| 群组 | `voice-restricted` | 只禁止语音和视频消息，附件菜单的录制项置灰 |
| 频道 | `channel` | 公告、置顶、浏览次数、通知按钮，最新帖子停在发送中 |
| 频道 | `discussion` | 评论入口、关联讨论群和置顶 |
| 频道 | `join` | 未加入频道的加入按钮；对话置顶使其常驻固定列表 |
| 频道 | `translate` | 外语消息、翻译与置顶组合 |
| 频道 | `protected-channel` | 内容保护 |
| 频道 | `community-channel` | 关联社区与活动直播，验证通话角标和社区角标切换 |
| 机器人 | `bot` | 启动按钮；每次打开恢复启动参数 |
| 机器人 | `keyboard` | 回复键盘和 `/help` 命令补全 |
| 机器人 | `inline-bot` | 消息内联按钮 |
| 机器人 | `sponsored` | 本地广告样本 |
| 话题 | `topic` | 常规与置顶、未读与草稿、已关闭三个真实话题 |
| 话题 | `topic-tabs` | 标签布局论坛：常规话题与 11 个话题，每个话题 40 条消息可滚动，含多种颜色、超长标题、置顶、未读、已关闭、法语话题，以及管理权限、加入申请和翻译提示 |
| 收藏夹 | `saved` | 本人、私聊、频道来源，置顶、一小时后的提醒与非音视频内容 |
| 归档 | `archived-private` | 未读私聊 |
| 归档 | `archived-group` | 静音群组 |
| 归档 | `archived-channel` | 置顶频道 |

`simulation.open <key> [--view <view>] [--input keep|empty|reply|edit|forward]` 打开场景。
打开时切换到对应的会话分类，并退出上一场景的归档或话题导航。
默认 `main` 使用主聊天；`topic` 会先在左栏展开话题列表，再打开第一个话题；`topic-tabs` 打开标签论坛的“全部”消息页。
标签论坛的顶栏第二行是话题切换按钮 `topicSwitcher`，点击后展开带搜索的话题面板。

| 视图 | 适用范围 |
|---|---|
| `alternate` | 另一套聊天组件 |
| `scheduled` | 计划消息；`saved` 有 30 条可滚动的提醒样本 |
| `shortcuts` | 快捷回复 `/layout` 的编辑页，18 条可滚动的样本，低于 20 条上限以保留输入框；进入模拟模式时生成，也可从 Telegram Business 页进入 |
| `pinned` | 含置顶消息的场景 |
| `actions` | 有管理权限的 `requests`；没有服务器日志，只显示分区外框 |
| `topic-unread` / `topic-closed` | `topic` 与 `topic-tabs` 的未读或已关闭话题 |
| `topic-general` / `topic-pinned` / `topic-long` / `topic-french` | `topic-tabs` 的常规话题、含置顶的话题、超长标题话题、法语话题 |
| `source-self` / `source-private` / `source-channel` | `saved` 的三个收藏来源 |

`--input` 只用于私聊和话题的聊天视图：`keep` 保留草稿，`empty` 清空，`reply` 安装回复草稿，`edit` 安装编辑草稿，`forward` 安装隐藏发送者的固定转发草稿。
未读话题只有来信，不能安装编辑草稿；已关闭话题只允许保留状态。
归档分组用于分类浏览；`chat.open-archive` 打开真正的归档列表，返回时点击界面可见的返回按钮。

`send-as` 同时支持 `main` 与 `alternate`，使用 `showSendAsButtonInMessageField` 检查身份按钮的显示、隐藏及输入区占位。
`simulation.list` 中的 `sendAsCount` 和 `sendAsPeerId` 报告可选身份数量与实际选中身份；隐藏按钮应保留后者，切换身份不请求服务器。

## 临时触发

`simulation.trigger [list|key]` 查询清单或触发指定场景，每次调用替换上一组列表提示。
`simulation.clear` 或 `simulation.trigger none` 清除列表提示、恢复固定论坛状态。
提示按钮只显示本地反馈，更新提示不会重启或安装程序。

| 类型 | 键名 |
|---|---|
| 生日、头像 | `birthday-setup`、`birthday-contact`、`birthday-contacts`、`userpic` |
| 会员、星币 | `premium-annual`、`premium-upgrade`、`premium-restore`、`premium-grace`、`credits` |
| 推广、拍卖 | `custom`、`auction`、`auction-outbid` |
| 登录、社区 | `auth`、`auth-multiple`、`community-requests`、`community-add` |
| 搜索 | `search-id`、`search-global`、`search-loading`、`search-posts`、`search-messages` |
| 下载条 | `download`、`download-done` |
| 列表加载 | `more-chats`、`load-more`、`loading` |
| 更新、冻结 | `update`、`frozen` |
| 话题提示 | `forum-requests`、`forum-report`、`forum-unarchive`、`forum-all` |
| 组合提示 | `stack` |
| 自动删除 | `delete-countdown`：在 `private` 插入 15 秒后删除的消息，返回编号和到期时间 |

短消息倒计时独立运行，清除列表提示不会取消它。
切换材质、主题、强调色和窗口宽度后可继续查看。非默认材质的列表提示条沿用透明背景，无边框或阴影。

## 补充消息和下载

`simulation.music [path [protected|cancelled]]` 仅在独立模拟配置中，用不超过 64 MB 的本地音频作为分块来源，沿正式流式播放器播放；省略参数查询最近样本的缓存、保存路径及分块请求次数。
样本不联网，播放器取得的完整缓存仍走正式文件保存及下载登记；先把 `core.downloadPath` 指向验证附件目录，路径末尾保留 `/`。`protected` 使用内容保护群，`cancelled` 先启动再取消下载。
默认注入 90 × 90 预览缩略图，内嵌封面由解码器读取。`player.cover [new-png-path]` 查询可保存的原始尺寸；只有独立模拟配置允许写入新文件，不覆盖已有文件。

`simulation.remove-message <peerId> <messageId>` 销毁指定模拟消息，用于验证弹窗和延迟回调在消息消失后的行为；不发网络请求，不操作真实账号。
`simulation.archive <peerId> <on|off>` 只在本地把会话移入或移出归档，走正式的列表移入移出流程，用于验证会话列表的增删过渡；不发归档请求。

`simulation.message <text> [--peer <peerId>] [--from <userId>] [--blocked] [--shadow-ban]`
向指定会话插入本地消息，默认收藏夹、自己发送。屏蔽标记须配合 `--from`，自己发送的消息不进入隐藏链。
`--blocked` 配合 `filtersEnabled` 与 `hideFromBlocked`；`--shadow-ban` 配合 `filtersEnabled`。

图片参数：`--sticker <imagePath>` 生成不超过 512 像素的静态贴纸；`--photo <imagePath>` 生成不超过 2048 像素的照片。
`--video <videoPath>` 生成视频消息，首帧作缩略图，播放直接读取该文件，测试期间不要移动或删除。
三者互斥。照片和视频可配合 `--group <positiveId>` 构造相册，同一对话内使用相同编号。
图片只保存在内存中，不上传。`--rich-table` 生成富消息：`text` 作为正文段落，后接一张带表头的两列表格，与图片参数互斥。

文章媒体样本使用 `iv.open-media-sample <图片一> <图片二> <视频>`，仅接受独立模拟配置，返回本地消息编号后打开正式文章预览；包含单项媒体、拼图和幻灯片，图片保存在会话内存中，视频保留原文件路径。`iv.open-file` 只准备本地文字文章，不能用本地图片或视频链接替代此样本入口。

`simulation.download <path> <loading|done>` 将本地文件加入下载管理器；拒绝音视频素材。
`simulation.download-progress <peerId> <messageId> <bytes|done>` 推进模拟进度。
这类事件用于检查下载列表、分类和进度，真实下载验证见[业务指令](business.md)。

## 验证入口

`simulation.cache-history-check` 仅在独立模拟配置中运行固定历史缓存检查，先用 `app.start --profile cache-history-check + simulation.enter` 进入。
返回异步任务编号，用 `job.status <编号>` 查看完成结果；覆盖摘要删除、未知末条、请求乱序、话题与收藏来源隔离、发送确认、加载中的动态更新与选择保留。
同时覆盖未加载消息的反应、扩展媒体资料、待办追加与完成、开放投票答案增删；扩展媒体使用本地协议样本，不执行交易。
检查沿真实 `ConversationCache` 的读写接口构造有限本地响应，并调用最新请求准备、缓存解析和新消息列表快照；不建立联网视图，不发送消息或清全缓存。
结果只含编号、数量和通过状态，全部 `cases[].pass`、`latestRequestPass` 与总 `pass` 均应为 `true`；任务结束恢复缓存原启用状态，固定样本随模拟会话销毁。
这证明本地缓存和新列表边界；`cache.state` 的 `latestSliceMessages` 仅反映当前列表快照，不能单凭它证明新视图网络入口已执行。

`simulation.chat-folders <single|none|pending>` 仅在独立模拟配置中，从全部对话开始，沿正式加载通知保留首个自定义分组或清空自定义分组。
`pending` 保留已有会话数据并模拟分组尚未返回，用于检查隐藏全部对话后的启动等待状态；再用 `single` 或 `none` 完成加载。
返回加载状态、可见分组数量、当前与默认分组编号；配合 `hideAllChatsFolder` 检查没有标签栏时的启动选择。只改内存，重启模拟模式恢复完整分组。

`simulation.search <fixture-directory>` 在独立模拟配置中注入 6 个联系人、4 条最近搜索、3 个推荐频道及常用、热门应用各 3 个；六类媒体标签各放 3 条本地消息。
目录需包含 `search-video.mp4`、`search-voice.ogg`、`search-music.mp3`，均为 3 秒的本地素材；照片和文档自动生成。先执行再打开搜索，重复调用不重复添加。
全局媒体沿原有列表布局展示并支持按样本文字筛选；最近搜索可能保存到独立配置，其余列表随模拟会话销毁。下载标签另用 `simulation.download` 注入。

`simulation.language <en|zh-hans>` 仅在独立模拟配置中切换内置翻译，经正式语言更新通知刷新界面，返回表情页签的当前译文。
不下载云端语言包，未内置的官方文案仍显示英文。验证结束切回原语言；此指令不用于真实账号的语言设置。

`simulation.stickers <0..9999> [open]` 仅在独立模拟配置中设置热门贴纸徽标计数，首次创建已安装、热门、归档三类本地静态贴纸包。
加 `open` 打开贴纸管理弹窗，控件标识为 `simulation.stickers`；省略时保留当前界面。后续调用更新计数并补回缺失的样本列表项，不重复创建包，适合依次检查 `0`、`9`、`107`、`9999` 的尺寸变化。
包信息可能随原生缓存保存在独立配置中。样本不带未读标记，切页不会自动减少徽标计数；返回值包含实际计数、是否补充样本、是否打开弹窗及三类贴纸包数量。
新建的模拟贴纸包关联 `🙂`，在模拟聊天输入该表情即可验证贴纸联想，不点击贴纸发送。
模拟验证前保存 `core.suggestStickersByEmoji`，临时设为 `false`，只查询已安装贴纸，避免等待无法完成的联网推荐；完成后恢复。

`group`、`channel` 和 `topic-tabs` 含本地正在进行的群通话或频道直播；`simulation.list` 返回 `hasGroupCall`、`canManageGroupCall` 和 `groupCallParticipants`。
配合 `hideGroupCalls` 验证入口即时隐藏和恢复，不点击加入或发起通话。

`simulation.emoji-packs <on|off>` 仅在独立模拟配置中加入或移除三个固定的本地自定义表情包，保留其他表情包。
样本使用独立笑脸素材及自定义表情属性，只加入表情列表；贴纸使用各自的包编号，不与表情或动图共用列表。
样本复用内存中的静态图片，文档使用自定义表情属性并沿原生表情包更新通知刷新；重复执行不会重复添加。
打开 `compose.emoji`，用 `control.set compose.emoji.tabs 0` 切到表情页，再用 `control.list emoji.categories` 定位自绘底栏；通过 `control.mouse` 和 `control.pointer` 检查分类与表情包切换、悬停，状态随模拟会话销毁。

`simulation.wallet [keep|missing|ready|collectibles|close]` 仅在独立模拟配置中打开或关闭钱包窗口。
`missing` 显示未创建状态，`ready` 注入固定公开地址、余额和交易记录，`collectibles` 另加固定藏品以显示标签页。
列表沿正式响应处理入口更新；不安装私钥或提交交易，状态随模拟会话销毁。
打开后用 `control.window simulation.wallet` 选择该窗口，检查结束用 `simulation.wallet close` 关闭并恢复主窗口目标。

`dialog.list` 列出对话框样本的键名与布局类别；`dialog.open <key>` 仅在独立模拟配置中关闭现有图层后无动画打开样本，`dialog.close` 关闭全部图层。
样本覆盖确认、表单、选项、列表、选择器、深色、不可关闭和自绘顶部等布局。公共外壳的控件标识为 `dialog.shell`、`dialog.title` 和 `dialog.close`，
配合 `control.list dialog.` 与截图检查标题、关闭按钮和底部按钮。保存类按钮仍走原有业务路径，模拟模式下联网请求不会成功，不作为业务结果的证据。
新增搜索样本：`giveaway-countries`、`translate-language`、`official-font`、`dictionary`、`peer-lists`、`share`；只用于模拟模式布局与筛选验证，不执行保存或发送。
`background-preview` 用内置照片壁纸打开背景预览，无需联网即可检查预览中的日期与服务消息。

先用 `simulation.list`、`message.list/inspect`、`chat.draft` 核对状态，再截图观察布局。
`message.inspect` 返回实际删除、置顶、引用、话题、评论和 `ttlDestroyAt`；短倒计时到期后再次查询存在状态。
模拟删除和编辑不得新增 `storage.deleted/edits` 记录。留档数据库独立验证使用 `storage.verify-archive`。
固定场景的原生按钮仍保留业务路径，发送、加入、付费等联网动作不能作为本地模拟的成功证据。

## 资料页布局

`simulation.profile <music|combined|channel-only|long|warning|community|hidden|empty> [--side|--stories|--keep]` 仅用于独立模拟配置。
八种场景依次覆盖音乐、音乐与频道、仅频道、无封面长歌名、安全提示、频道所属社区、群组隐藏说明和空内容。
默认打开资料弹层；`--side` 使用侧栏，`--stories` 使用模拟账号的动态资料页，`--keep` 只更新数据并保留当前视图。
音乐、封面、消息和社区均在本地生成，不发送消息、不播放音频；返回实际资料编号、音乐数量、消息编号与提示状态。
音乐列表沿正式数据通知更新，重复调用替换样本；模拟账号名和资料只影响此独立配置，重启模拟模式恢复。
