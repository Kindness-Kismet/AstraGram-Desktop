<div align="center">

# AstraGram

[English](README.md) | **简体中文**

<br>

[![C++20](https://img.shields.io/badge/C%2B%2B-20-00599C?logo=cplusplus&logoColor=white&style=flat-square)](https://en.cppreference.com/w/cpp/20)
[![Qt](https://img.shields.io/badge/Qt-41CD52?logo=qt&logoColor=white&style=flat-square)](https://www.qt.io)
[![Windows](https://img.shields.io/badge/Windows-0078D6?logo=windows&logoColor=white&style=flat-square)](#下载)
[![Linux](https://img.shields.io/badge/Linux-FCC624?logo=linux&logoColor=black&style=flat-square)](#下载)
[![macOS](https://img.shields.io/badge/macOS-000000?logo=apple&logoColor=white&style=flat-square)](#下载)
[![GPL v3](https://img.shields.io/badge/License-GPL%20v3-blue?style=flat-square)](https://www.gnu.org/licenses/gpl-3.0.html)

</div>

<br>

AstraGram 是适用于 Windows、Linux 和 macOS 的桌面客户端，在
[Telegram Desktop](https://github.com/telegramdesktop/tdesktop) 的基础上，沿用
[AyuGram](https://github.com/AyuGram/AyuGramDesktop) 和
[re-zero001/AyuGramDesktop](https://github.com/re-zero001/AyuGramDesktop) 的定制功能继续开发。

欢迎关注 [Telegram 频道](https://t.me/MaterialDesign3)，获取项目动态和版本更新。

你可以按自己的习惯调整圆角、消息气泡、输入区按钮、侧边菜单、表情显示和中英文间距。
夜间主题使用纯色背景，通过面板、控件和边框的颜色区分界面层次。

仓库也提供面向 AI 编程助手的项目规范、仅在调试构建中启用的调试服务，以及自动处理依赖和编译的脚本，方便修改后直接验证。

## 导航

- [自定义功能](#自定义功能)
- [下载](#下载)
- [从源码构建](#从源码构建)
  - [Windows](#windows)
  - [Linux（Docker）](#linuxdocker)
  - [macOS](#macos)
- [AI 辅助开发](#ai-辅助开发)
- [仓库结构](#仓库结构)
- [许可证](#许可证)
- [致谢](#致谢)
- [支持项目](#支持项目)

## 自定义功能

以下选项位于 **设置 → AstraGram 设置**。大多数设置即时生效；头像圆角、气泡圆角、宽消息倍率、动态、异常组合字符过滤和接收时自动加空格等需要重启的选项，会提示重启，无需手动修改配置文件。

### 外观

| 可调整的内容 | 说明 |
|---|---|
| 头像圆角 | 从圆形到方形自由调整，统一应用到各处头像 |
| 消息气泡 | 调整圆角、显示或隐藏尾巴，以及贴纸缩放 |
| 夜间主题 | 使用纯色背景，区分面板、控件和边框 |
| 应用图标 | 为桌面和任务栏选择图标 |
| 等宽字体 | 选择代码块和等宽文字使用的字体 |
| 窗口 | 调整默认窗口宽度和宽消息倍率 |

### 会话列表与侧边菜单

| 可调整的内容 | 说明 |
|---|---|
| 文件夹标签 | 隐藏“全部聊天”，显示或隐藏各标签的未读数量 |
| 任务栏角标 | 隐藏任务栏和托盘图标上的未读数量 |
| 侧边菜单 | 自选是否显示个人资料、收藏夹、归档、联系人、通话、机器人、新建群组、新建频道、捐赠详情和夜间模式 |

### 消息与输入区

| 可调整的内容 | 说明 |
|---|---|
| 输入区按钮 | 分别显示或隐藏附件、表情、命令、礼物和自动删除按钮 |
| 录制消息 | 从附件菜单开始录制语音或视频消息 |
| 悬停弹窗 | 控制附件和表情按钮是否在悬停时弹出面板 |
| 引用与回复 | 使用简洁样式或完整彩色样式 |
| 编辑标记 | 保留文字、改用图标，或自定义标记 |
| 消息底部信息 | 将文字信息改为紧凑图标 |
| 时间与编号 | 显示秒数、消息编号、会话编号和数据中心编号 |
| 右键菜单 | 添加消息详情、查看列表、回应面板、重复消息、添加到文件夹等项目 |
| 贴纸 | 调整最近使用数量、面板缩放，隐藏问候贴纸 |

### 文本处理

| 可调整的内容 | 说明 |
|---|---|
| 中日韩文字与拉丁文字间距 | 在发送、接收或编辑时，为中日韩文字与拉丁字母、数字之间补空格；保留提及、链接、邮箱和代码块原样 |
| 异常组合字符过滤 | 过滤接收文字中过度堆叠的组合字符 |
| 链接预览 | 改进预览处理，可在打开外部链接前提示 |
| 翻译 | 选择内置翻译器使用的服务 |

### 表情字体包

表情字体按需下载，不直接打进程序包，只需下载自己选择的字体：

| 字体包 | 大小 |
|---|---|
| Apple | 约 111 MB |
| JoyPixels | 约 13 MB |
| Samsung One UI | 约 20 MB |

下载过程中会按预设的 SHA-256 校验文件，避免安装不完整或被修改的字体。
也可以导入本地彩色表情字体。Telegram Desktop 原有的表情包仍可使用，默认使用内置表情。

### 界面语言

程序内置英文和简体中文，Telegram Desktop 的其他云端语言包仍可正常使用。

## 下载

前往 **[发布页面](https://github.com/Kindness-Kismet/AstraGram-Desktop/releases/latest)**，下载对应平台的压缩包：

| 平台 | 文件名格式 |
|---|---|
| Windows · x64 | `AstraGram-v<版本>-win-x64.zip` |
| Windows · arm64 | `AstraGram-v<版本>-win-arm64.zip` |
| Linux · x64 | `AstraGram-v<版本>-linux-x64.zip` |
| Linux · arm64 | `AstraGram-v<版本>-linux-arm64.zip` |
| macOS · Intel | `AstraGram-v<版本>-macos-x64.zip` |
| macOS · Apple Silicon | `AstraGram-v<版本>-macos-arm64.zip` |

压缩包解压后即可运行。发布页面同时提供内置更新器使用的更新包，已安装的客户端可以直接更新。

## 从源码构建

Windows 使用 `prebuild.py` 编译第三方依赖，使用 `build.py` 配置和构建应用。
Linux 和 macOS 使用下方各自的构建命令。

### Windows

**环境要求**

- Visual Studio 2026，社区版即可，安装“使用 C++ 的桌面开发”工作负载，包含 v145 工具集（MSVC 14.51）、ATL/MFC 头文件和 CMake；旧版 Visual Studio 不适用。
- Windows 10 SDK 10.0.26100.0 或更新版本。
- [Python](https://www.python.org/downloads/) 3.10+ 和 [Git](https://git-scm.com/download/win)。
- 约 60 GB 可用空间，其中依赖约占 10 GB，其余用于编译中间文件和产物。

```bash
git clone --recursive https://github.com/Kindness-Kismet/AstraGram-Desktop
cd AstraGram-Desktop

# 首次编译依赖，缓存保存在 build/tmp。
python scripts/prebuild.py
python scripts/prebuild.py --list

# 构建正式版或调试版，调试版会收集符号文件。
python scripts/build.py
python scripts/build.py --dev
python scripts/build.py --jobs 8
```

产物保存在 `build/AstraGram-v<版本>-win-x64-{release|dev}/`。
添加 `--pack` 可生成仅包含可执行文件的 `build/AstraGram-v<版本>-win-x64.zip`；
`--clean-pack` 会先清理产物目录里的运行数据，例如账号数据和日志。

- 依赖仅在子模块指针或 `Telegram/build/version` 变化时需要重建，日常改代码只需运行 `build.py`。
- 默认使用公开测试 API 凭据，可通过 `--api-id` 和 `--api-hash` 指定自己的凭据。
- 默认使用 32 路并发，`--jobs` 支持 1～128。内存占用受源码、其他程序和可用提交内存影响，可按实际情况降低并发。
- `cmake` 使用上游 `desktop-app/cmake_helpers` 子模块，定制补丁由 `scripts/build_support/cmake_patch.py` 在配置时应用；该子模块显示内容已修改是预期情况。

### Linux（Docker）

参考构建使用与持续集成相同的 Rocky Linux 8 容器，不依赖宿主机的发行版。

**环境要求**：[Docker Engine](https://docs.docker.com/engine/install/) 20+、
[Poetry](https://python-poetry.org/docs/#installation)、Python 3.8+ 和约 60 GB 可用空间，依赖镜像本身约占 20 GB。

```bash
git clone --recursive https://github.com/Kindness-Kismet/AstraGram-Desktop
cd AstraGram-Desktop/Telegram/build/docker/centos_env

# 生成正式版构建镜像，空值表示不启用 DEBUG 和 LTO。
poetry install
DEBUG= LTO= poetry run gen_dockerfile > Dockerfile

# 首次构建依赖镜像约需 1～2 小时。
docker build -t astragram:centos_env .

cd ../../../..

docker run --rm \
  -v "$PWD":/usr/src/tdesktop \
  -e CONFIG=Release \
  astragram:centos_env \
  /usr/src/tdesktop/Telegram/build/docker/centos_env/build.sh \
  -D CMAKE_CONFIGURATION_TYPES=Release \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627
```

可执行文件保存在挂载目录的 `out/Release/AstraGram`。
后续增量构建重复运行同一条 `docker run` 命令即可，编译中间文件保存在挂载的 `out/` 目录中。

### macOS

依赖在本机编译，使用 Qt 6.11.2 和正式版配置，保存在源码目录旁的 `Libraries/` 中。

**环境要求**：Xcode 14+、[Homebrew](https://brew.sh)、Python 3（由 `xcode-select --install` 提供）和约 40 GB 可用空间。

```bash
brew install automake libtool meson nasm ninja pkg-config
sudo xcode-select -s /Applications/Xcode.app/Contents/Developer

git clone --recursive https://github.com/Kindness-Kismet/AstraGram-Desktop
cd AstraGram-Desktop

# 首次编译依赖；去掉 silent 可查看详细输出。
./Telegram/build/prepare/mac.sh silent

cd Telegram
./configure.sh \
  -D CMAKE_CONFIGURATION_TYPES=Release \
  -D TDESKTOP_API_ID=2040 \
  -D TDESKTOP_API_HASH=b18441a1ff607e10a989891a5462e627 \
  -D CMAKE_XCODE_ATTRIBUTE_CODE_SIGNING_ALLOWED=NO
cmake --build ../out --config Release --parallel
```

应用包保存在 `out/Release/AstraGram.app`。
与 Windows 一样，子模块指针或 `Telegram/build/version` 变化时才需要重新编译依赖。

## AI 辅助开发

项目提供统一的开发规范和调试入口，方便开发者或编程助手从命令行完成修改与验证。

### 统一规范

先阅读 [`AGENTS.md`](AGENTS.md)。`CLAUDE.md` 直接引用该文件，规范只维护一份，包含代码放置位置、命名、响应式订阅、线程、构建和审查要求。

### 技能

`.claude/skills/` 与 `.codex/skills/` 保存相同的技能内容，供对应的编程助手使用：

| 技能 | 用途 |
|---|---|
| `app-debug` | 控制调试构建，读写设置、发送测试消息、截图、操作控件和进入本地假会话 |
| `upstream-sync` | 检查官方稳定版，生成主仓库和子模块的改动报告，并登记已完成的适配 |
| `version-bump` | 更新唯一的版本文件并编写更新说明 |
| `commit` | 按职责拆分提交，逐个路径暂存并复核提交信息 |
| `pull-request` | 整理合并请求、执行审查清单和控制改动范围 |

### 内置调试服务

调试构建通过 `_DEBUG` 启用服务，监听 `127.0.0.1:20100`；正式版不包含该服务。
每个 TCP 连接处理一条文本命令，成功返回 `OK` 和可选结果，失败返回 `ERR` 和原因，结构化结果使用 JSON。

- **应用生命周期**：启动、重启和停止应用；构建脚本在收集产物前停止占用目标程序的进程。
- **设置**：列出设置键、导出全部设置，沿用设置界面的处理流程读写值。
- **界面观察**：截取活动窗口、查看控件树，通过正常事件路径模拟点击。
- **诊断**：查询本地存储占用、消息渲染统计和更新通道信息；未处理的崩溃保存在工作目录的 `crash.log` 中。

### 修改与验证

```bash
python scripts/build.py --dev
python .claude/skills/app-debug/scripts/cli.py app.start
python .claude/skills/app-debug/scripts/cli.py settings.set avatarCorners 8
python .claude/skills/app-debug/scripts/cli.py screenshot.take
python .claude/skills/app-debug/scripts/cli.py app.stop
```

修改后可以直接编译并观察界面，具体耗时取决于变更范围和本机性能。

## 仓库结构

```text
scripts/                   依赖预编译、产品构建及辅助脚本
Telegram/
  SourceFiles/extras/       本项目的定制代码，功能按目录划分
  SourceFiles/…            上游 Telegram Desktop 源码
  lib_ui, lib_tl, codegen  定制子模块，其余 lib_* 通常作为只读依赖
.github/
  upstream.json            已适配的官方稳定版及同步跳过规则
  CHANGELOG.md             更新说明
  workflows/               构建与发布流程
AGENTS.md                  统一项目规范，CLAUDE.md 引用该文件
.claude/, .codex/          编程助手技能
```

## 许可证

与 Telegram Desktop 和 AyuGram 一样，本项目使用 [GNU GPL v3](https://www.gnu.org/licenses/gpl-3.0.html) 或更新版本。
完整条款见 [`LICENSE`](LICENSE)，第三方声明见 [`LEGAL`](LEGAL)。

## 致谢

- **[re-zero001/AyuGramDesktop](https://github.com/re-zero001/AyuGramDesktop)**：本仓库的直接上游，为后续开发提供基础。
- **[AyuGram](https://github.com/AyuGram/AyuGramDesktop)**：AyuGram 系列分支及其定制功能。
- **[Telegram Desktop](https://github.com/telegramdesktop/tdesktop)**：各分支共同基于的官方桌面客户端。

<br>

---

## 支持项目

如果 AstraGram 对你有帮助，欢迎通过爱发电支持项目开发和日常模型调用费用。
捐赠完全自愿，不会影响任何功能使用。

<div align="center">

<a href="https://github.com/KiritoXDone">
  <img src="https://avatars.githubusercontent.com/u/58523182?v=4&amp;s=192" width="96" height="96" alt="KiritoXDone">
</a>

**[KiritoXDone](https://github.com/KiritoXDone)**

**[通过爱发电支持](https://afdian.com/a/KiritoXD)**

</div>
