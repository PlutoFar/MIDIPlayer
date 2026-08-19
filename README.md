<p align="center">
  <img src="Resources/icon.png" alt="MIDI Player 图标" width="128" />
</p>

<h1 align="center">MIDI Player</h1>

<p align="center">Windows x64 MIDI 播放器与 VST3 乐器宿主</p>

<p align="center">
  <a href="https://github.com/PlutoFar/MIDIPlayer/releases/latest">下载最新版本</a>
  ·
  <a href="#快速开始">快速开始</a>
  ·
  <a href="#从源码构建">从源码构建</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Windows-10%2F11-0078D4" alt="Windows 10/11" />
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C" alt="C++17" />
  <img src="https://img.shields.io/badge/JUCE-8.0.15-F28C28" alt="JUCE 8.0.15" />
  <img src="https://img.shields.io/badge/架构-x64-5C2D91" alt="x64" />
</p>

MIDI Player 基于 C++17 与 JUCE 构建，面向 Windows x64 提供 MIDI 文件播放、VST3 乐器托管、播放列表管理、音频设备配置和离线音频导出。VST3 乐器运行在独立的 `MidiWorker.exe` 进程中，播放器核心与插件进程保持独立生命周期。

## 维护状态

| 模块 | 状态 | 当前边界 |
| --- | --- | --- |
| JUCE 桌面界面 | 持续维护 | 当前发布版本、功能开发和问题修复的唯一界面实现 |
| 播放核心 | 持续维护 | 播放、音频设备、导出、播放列表和状态管理 |
| VST3 工作进程 | 持续维护 | 插件加载、编辑器托管和插件异常隔离 |
| WinUI 3 界面 | 停止维护 | 历史归档，维护范围聚焦 JUCE 桌面界面 |
| WinUI 桥接代码 | 迁移阶段 | 暂存于当前主分支，后续版本移除 |

WinUI 3 实现计划迁移至 `archive/winui-unmaintained` 分支，作为历史版本保存。迁移完成后，主分支移除 `Source/WinUI/`、`MidiWinBridge`、WinUI 设置适配器和相关构建配置。新功能、问题修复和发布验证均以 JUCE 桌面界面为准。

## 核心功能

- 64 位 VST3 乐器扫描、加载、卸载和编辑器托管
- 独立插件工作进程与异常恢复
- MIDI 文件选择、系统文件打开和拖放导入
- 播放列表增删、排序、保存、加载和持久化
- 连续播放、列表循环、单曲循环和随机播放
- Windows 音频输出设备、采样率、缓冲区和通道配置
- WASAPI、DirectSound 和可选 ASIO 音频后端
- WAV、FLAC、Ogg Vorbis 离线导出
- 导出采样率、位深、质量和尾音配置
- 背景图片、高斯模糊、Aero、Acrylic、遮罩和主题色
- 界面字体、播放列表字体、字号和曲目行间距
- 窗口置顶、MIDI 文件关联和便携运行

## 系统要求

| 项目 | 要求 |
| --- | --- |
| 操作系统 | Windows 10 1809 或更高版本 |
| 系统架构 | x64 |
| 运行库 | Microsoft Visual C++ 2015–2022 Redistributable x64 |
| 音频设备 | WASAPI、DirectSound 或 ASIO 设备 |
| 乐器插件 | 64 位 VST3 插件 |

大型音源的音色库、许可证和厂商运行环境按插件发行方要求配置。

## 下载与快速开始

从 [GitHub Releases](https://github.com/PlutoFar/MIDIPlayer/releases/latest) 获取 x64 便携包。

1. 将压缩包解压到具有写入权限的目录。
2. 确认 `MidiPlayer.exe` 与 `MidiWorker.exe` 位于同一目录。
3. 启动 `MidiPlayer.exe`。
4. 扫描插件并选择 VST3 乐器。
5. 添加 `.mid` 或 `.midi` 文件。
6. 在设置中选择音频输出设备。
7. 开始播放，或打开导出功能生成音频文件。

## VST3 插件目录

程序启动时扫描以下目录：

```text
C:\Program Files\Common Files\VST3\
程序目录\VST3\
```

插件授权、音色库和厂商专用安装目录按照插件发行方的安装流程配置。

## 便携运行与故障诊断

程序通过程序目录中的标记文件选择运行模式。

| 标记文件 | 运行模式 | 设置位置 | 日志行为 |
| --- | --- | --- | --- |
| `portable.dat` | 普通便携模式 | 程序目录 `Settings/` | 保持正常运行日志级别 |
| `portable_debug.dat` | 诊断便携模式 | 程序目录 `Settings/` | 启用 `debug_log.txt` 详细日志记录 |

日常使用创建 `portable.dat`。故障定位期间创建 `portable_debug.dat`，复现问题后提交程序目录中的 `debug_log.txt`。诊断完成后保留普通便携模式标记 `portable.dat`。

本地 VST3 插件可放入程序目录的 `VST3/`。

## 从源码构建

### 环境

- Visual Studio 2022
- MSVC v143 桌面 C++ 工作负载
- CMake 3.27 或更高版本
- Windows SDK 10.0.26100.0
- JUCE 8.0.15，提交 `91ad83ae34a81e0833b1a2b0866f54846370ae53`
- 可选：Steinberg ASIO SDK

### 获取源码

```powershell
git clone https://github.com/PlutoFar/MIDIPlayer.git
cd MIDIPlayer
git clone https://github.com/juce-framework/JUCE.git JUCE
git -C JUCE checkout 91ad83ae34a81e0833b1a2b0866f54846370ae53
git -C JUCE apply ../patches/juce-child-process-kill.patch
```

JUCE 补丁用于在协调进程关闭时终止无响应的插件工作进程。

### 配置

```powershell
cmake --preset windows-vs2022
```

启用 ASIO：

```powershell
cmake --preset windows-vs2022 -DMIDIPLAYER_ENABLE_ASIO=ON
```

启用 ASIO 前，将 Steinberg ASIO SDK 中的 `common/` 目录放置到：

```text
JUCE/modules/juce_audio_devices/native/common/
```

### 构建 JUCE 桌面版本

```powershell
cmake --build build --config Release --target MidiLegacy MidiWorker --parallel
```

构建产物：

```text
build/MidiLegacy_artefacts/Release/MidiLegacy.exe
build/MidiLegacy_artefacts/Release/MidiWorker.exe
```

便携包组装阶段将 `MidiLegacy.exe` 重命名为 `MidiPlayer.exe`。

### 运行测试

```powershell
cmake --build build --config Release --target MidiTests
.\build\MidiTests_artefacts\Release\MidiTests.exe
```

## 仓库结构

```text
.
|-- Source/
|   |-- AudioEngine/    # 音频图、设备管理和离线导出
|   |-- Core/           # 应用核心与迁移中的 WinUI 桥接
|   |-- Midi/           # MIDI 时序与播放控制
|   |-- Playlist/       # 播放列表模型与持久化
|   |-- PluginBridge/   # 主进程与插件工作进程协议
|   |-- UI/             # 当前维护的 JUCE 桌面界面
|   |-- Utils/          # 设置、文件关联和 Windows 工具
|   |-- WinUI/          # 已停止维护，计划迁移至归档分支
|   `-- Worker/         # VST3 工作进程
|-- Tests/              # 原生回归测试
|-- Resources/          # 应用图标
|-- patches/            # JUCE 补丁
|-- CMakeLists.txt
`-- CMakePresets.json
```

## 问题反馈

提交问题时附带以下信息：

- Windows 版本与系统缩放比例
- 程序版本或提交编号
- 音频后端、设备名称和缓冲区
- VST3 插件名称与版本
- 可重复的操作步骤
- 诊断模式生成的 `debug_log.txt`

WinUI 3 相关功能进入历史归档，当前维护范围聚焦 JUCE 桌面版本。
