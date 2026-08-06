# MIDI Player

简体中文 | [English](README.md)

MIDI Player 是一款使用 C++17、JUCE 与 WinUI 3 开发的 Windows 桌面 MIDI 播放器和 VST3 乐器宿主。项目提供 WinUI 原生界面版与 JUCE Legacy 兼容版，两套界面共用同一套播放核心。

![Windows](https://img.shields.io/badge/Windows-10%2F11-0078D4)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C)
![JUCE](https://img.shields.io/badge/JUCE-8.0.12-F28C28)
![WinUI](https://img.shields.io/badge/WinUI-3-0078D4)

## 版本说明

| 版本 | 界面 | 适用场景 | 运行依赖 |
| --- | --- | --- | --- |
| WinUI | Windows App SDK / WinUI 3 | Windows 11 风格原生界面 | Windows App Runtime 2.3.1 x64 或兼容的更新 2.x 版本；VC++ x64 运行库 |
| Legacy | JUCE 桌面界面 | 广泛兼容；未安装 Windows App Runtime 的系统 | VC++ x64 运行库 |

两个版本都通过独立的 `MidiWorker.exe` 进程承载 VST3 乐器。`MidiWorker.exe` 必须与 `MidiPlayer.exe` 保持在同一目录。

## 功能

- VST3 乐器扫描、加载、插件界面托管与崩溃隔离
- 通过文件选择器、系统文件打开或拖放导入 MIDI 文件
- 支持编辑和持久化的播放列表
- 连续播放、列表循环、单曲循环和随机播放
- Windows 音频设备配置与可选 ASIO 支持
- 离线导出 WAV、FLAC、Ogg Vorbis，支持采样率、位深、质量和尾音设置
- 自定义背景、高斯模糊、Aero、Acrylic、遮罩、最近背景和主题配色提取
- 界面字体与播放列表字体设置
- 窗口置顶和 `.mid` / `.midi` 文件关联
- 通过 `portable.dat` 启用便携模式，设置与本地插件目录随程序保存

## 下载

从 [GitHub Releases](https://github.com/PlutoFar/MIDIPlayer/releases/latest) 下载当前便携包：

- `MIDIPlayer-v1.0.0-WinUI-x64.zip`
- `MIDIPlayer-v1.0.0-Legacy-x64.zip`

将压缩包完整解压到具有写入权限的目录，再运行 `MidiPlayer.exe`。不要直接在压缩包内运行程序。

### WinUI 运行依赖

WinUI 版启动前需要安装以下 x64 运行库：

- [Windows App Runtime 2.3.1 x64](https://aka.ms/windowsappsdk/2.3/2.3.1/windowsappruntimeinstall-x64.exe)，或兼容的更新 2.x 版本
- [Microsoft Visual C++ 2015–2022 Redistributable x64](https://aka.ms/vc14/vc_redist.x64.exe)

Legacy 版只需要 Visual C++ x64 运行库。

## 快速开始

1. 安装一个 64 位 VST3 乐器。
2. 启动 `MidiPlayer.exe`。
3. 扫描插件并选择乐器。
4. 向播放列表添加 `.mid` 或 `.midi` 文件。
5. 在设置中选择正确的音频输出设备。
6. 开始播放，或将曲目导出为音频。

插件扫描器会检查系统 VST3 目录和程序目录中的 `VST3/`。部分乐器需要单独完成授权或安装音色库。

## 从源码构建

### 环境要求

- Windows 10 1809 或更高版本，x64
- Visual Studio 2022，安装 MSVC v143 桌面 C++ 工作负载
- CMake 3.27 或更高版本
- Windows SDK 10.0.26100.0
- JUCE 提交 `501c07674e1ad693085a7e7c398f205c2677f5da`，源码版本为 JUCE 8.0.12
- 构建 WinUI 版时需要 Visual Studio 的 C++/WinRT 与 Windows App SDK C++ 项目支持
- 构建 ASIO 支持时需要 Steinberg ASIO SDK

克隆仓库与固定版本的 JUCE：

```powershell
git clone https://github.com/PlutoFar/MIDIPlayer.git
cd MIDIPlayer
git clone https://github.com/juce-framework/JUCE.git JUCE
git -C JUCE checkout 501c07674e1ad693085a7e7c398f205c2677f5da
git -C JUCE apply ../patches/juce-child-process-kill.patch
```

JUCE 补丁用于在协调进程关闭时终止无响应的插件工作进程，是当前工作进程生命周期的必要修正。

ASIO 默认关闭，因此未安装专有 ASIO SDK 时仍可构建。启用 ASIO 前，从 Steinberg 获取 SDK，并将其中的 `common/` 目录放到 `JUCE/modules/juce_audio_devices/native/common/`。

### 配置

```powershell
cmake --preset windows-vs2022
```

启用 ASIO：

```powershell
cmake --preset windows-vs2022 -DMIDIPLAYER_ENABLE_ASIO=ON
```

### Legacy 版

```powershell
cmake --build build --config Release --target MidiLegacy MidiWorker --parallel
```

输出文件：

```text
build/MidiLegacy_artefacts/Release/MidiLegacy.exe
build/MidiWorker_artefacts/Release/MidiWorker.exe
```

组装便携包时，将 `MidiLegacy.exe` 重命名为 `MidiPlayer.exe`。

### WinUI 版

构建共享原生库与工作进程：

```powershell
cmake --build build --config Release --target MidiCore MidiWinBridge MidiWorker --parallel
```

在 Visual Studio 2022 开发者命令行中构建 WinUI 前端：

```powershell
msbuild Source\WinUI\MidiPlayer.vcxproj -restore -p:Configuration=Release -p:Platform=x64 -v:minimal -nologo
```

依赖框架的 WinUI 发布文件位于 `Source/WinUI/x64/Release/MidiPlayer/`。

### 测试

```powershell
cmake --build build --config Release --target MidiTests
.\build\MidiTests_artefacts\Release\MidiTests.exe
```

本地批处理构建脚本不进入仓库。以上命令是公开源码支持的构建入口。

## 仓库结构

```text
.
|-- Source/
|   |-- AudioEngine/    # 音频图、设备管理和离线导出
|   |-- Core/           # 共享状态与 WinUI/Legacy 适配
|   |-- Midi/           # MIDI 时序与播放控制
|   |-- Playlist/       # 播放列表模型与持久化
|   |-- PluginBridge/   # 主进程/工作进程协议和共享音频缓冲区
|   |-- UI/             # JUCE Legacy 界面
|   |-- Utils/          # 设置、Shell 集成和 Windows 工具
|   |-- WinUI/          # WinUI 3 前端
|   `-- Worker/         # VST3 工作进程
|-- Tests/              # 原生回归测试
|-- Resources/          # 应用图标
|-- patches/            # 必需的第三方源码补丁
|-- CMakeLists.txt
`-- CMakePresets.json
```

## 运行数据

构建产物、下载的依赖、插件、设置、日志、播放列表和 WinUI 生成文件不会进入版本控制。本仓库不分发 VST3 插件和 Steinberg ASIO SDK。

## 问题反馈

问题报告应包含 Windows 版本、程序版本、音频后端、插件名称与版本、复现步骤。上传日志前应删除私人路径和授权信息。
