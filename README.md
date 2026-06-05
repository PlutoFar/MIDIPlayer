# Modern MIDI Player

中文 | [English](#english)

Modern MIDI Player 是一个基于 C++17 和 JUCE 的 Windows 桌面 MIDI 播放器。它可以加载 VST3 虚拟乐器，把 MIDI 文件作为播放源，并提供播放列表、音频设备设置、便携模式和界面自定义等功能。

Modern MIDI Player is a Windows desktop MIDI player built with C++17 and JUCE. It hosts VST3 instruments, plays MIDI files, and provides playlist management, audio device setup, portable mode, and UI customization.

![Windows](https://img.shields.io/badge/Windows-10%2F11-0078D4)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue)
![JUCE](https://img.shields.io/badge/JUCE-required-orange)

## 功能

- VST3 乐器插件宿主与插件扫描
- MIDI 文件播放与播放列表管理
- 顺序播放、列表循环、单曲循环、随机播放
- 支持拖放导入 MIDI 文件
- 通过 JUCE 配置 WASAPI/ASIO 等音频设备
- 通过 `portable.dat` 启用便携模式
- 保存音频设备、插件、窗口状态、字体和背景等本地设置
- 支持 Windows `.mid` / `.midi` 文件关联
- 支持自定义背景图、模糊、遮罩和主题色提取
- Windows 11 风格界面，包含侧边导航和播放列表面板

## 项目结构

```text
.
|-- AudioEngine/      # 音频设备、VST3 扫描、插件加载、音频图
|-- Midi/             # MIDI 事件时序与播放逻辑
|-- Playlist/         # 播放列表数据模型与 JSON 保存/加载
|-- Resources/        # 应用资源
|-- UI/               # 主界面、控件、播放列表、设置窗口
|-- Utils/            # 设置、日志、Windows 辅助工具
|-- CMakeLists.txt    # CMake 构建配置
|-- HELP.txt          # 用户帮助文档
`-- Main.cpp          # JUCE 应用入口
```

## 环境要求

- Windows 10 或 Windows 11
- CMake 3.15 或更新版本
- Visual Studio 2022，并安装 C++ 桌面开发工作负载
- JUCE 框架源码
- 用于发声的 VST3 乐器插件
- 可选：如果需要 ASIO，需安装 ASIO 驱动和 ASIO SDK

JUCE 没有提交到本仓库。构建前请在项目根目录克隆到 `JUCE/`：

```powershell
git clone https://github.com/juce-framework/JUCE.git JUCE
```

## 构建

在仓库根目录执行：

```powershell
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

生成的可执行文件位于：

```text
build/ModernMidiPlayer_artefacts/Release/
```

## 快速开始

1. 安装一个 VST3 乐器插件。
2. 启动 `Modern MIDI Player.exe`。
3. 扫描 VST3 插件，或从已发现插件列表中选择插件。
4. 通过播放列表按钮添加 MIDI 文件，或直接拖放到窗口中。
5. 如果没有声音，请在音频设置中选择正确的音频设备。
6. 点击播放。

## 便携模式

在可执行文件旁边创建 `portable.dat` 即可启用便携模式。

便携模式下，设置会保存到本地 `Settings/` 文件夹，程序也会优先检查可执行文件旁边的 `VST3/` 文件夹。

## 运行时数据

以下内容属于运行时数据或外部依赖，不应提交到仓库：

- `build/`
- `dist/`
- `Settings/`
- 播放列表 JSON 文件
- 下载或克隆的 `JUCE/`
- 打包后的可执行文件和调试产物

## 说明

本项目目前主要面向 Windows。部分代码使用了 Windows 专用 API，用于视觉效果、Shell 集成和文件关联。

---

## English

Modern MIDI Player is a Windows desktop MIDI player built with C++17 and JUCE. It hosts VST3 instruments, plays MIDI files, and provides playlist management, audio device setup, portable mode, and UI customization.

## Features

- VST3 instrument hosting and plugin scanning
- MIDI file playback with playlist management
- Sequential, list loop, single loop, and shuffle playback modes
- Drag-and-drop MIDI file import
- WASAPI/ASIO audio device configuration through JUCE
- Portable mode support through `portable.dat`
- Local settings storage for audio devices, plugins, window state, fonts, and background options
- Windows `.mid` / `.midi` file association support
- Custom background image, blur, overlay, and theme color extraction
- Windows 11 inspired UI with sidebar navigation and playlist panel

## Project Layout

```text
.
|-- AudioEngine/      # Audio device, VST3 scanning, plugin loading, audio graph
|-- Midi/             # MIDI sequencing and playback timing
|-- Playlist/         # Playlist data model and JSON save/load logic
|-- Resources/        # Application resources
|-- UI/               # Main window, controls, playlist panel, settings dialogs
|-- Utils/            # Settings, logging, and Windows helper utilities
|-- CMakeLists.txt    # CMake build definition
|-- HELP.txt          # End-user help text
`-- Main.cpp          # JUCE application entry point
```

## Requirements

- Windows 10 or Windows 11
- CMake 3.15 or newer
- Visual Studio 2022 with the C++ desktop workload
- JUCE framework source tree
- VST3 instrument plugins for sound output
- Optional: ASIO driver and ASIO SDK if ASIO support is needed

JUCE is intentionally not committed to this repository. Clone it into a `JUCE` folder at the project root before configuring CMake:

```powershell
git clone https://github.com/juce-framework/JUCE.git JUCE
```

## Build

From the repository root:

```powershell
cmake -B build -G "Visual Studio 17 2022"
cmake --build build --config Release
```

The executable will be generated under:

```text
build/ModernMidiPlayer_artefacts/Release/
```

## Quick Start

1. Install a VST3 instrument plugin.
2. Launch `Modern MIDI Player.exe`.
3. Scan for VST3 plugins or choose a plugin from the discovered list.
4. Add MIDI files by using the playlist controls or dragging files into the window.
5. Choose an audio device in the audio settings if playback is silent.
6. Press play.

## Portable Mode

Create a file named `portable.dat` next to the executable to enable portable mode.

In portable mode, settings are stored in a local `Settings/` folder, and the app also checks a local `VST3/` folder next to the executable.

## Runtime Data

Generated runtime data should not be committed. This includes:

- `build/`
- `dist/`
- `Settings/`
- playlist JSON files
- downloaded or cloned `JUCE/`
- packaged executables and debug artifacts

## Notes

This project is currently focused on Windows. Some code paths use Windows-specific APIs for visual effects, shell integration, and file association.
