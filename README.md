<p align="center">
  <img src="Resources/icon.png" alt="MIDI Player 图标" width="128" />
</p>

<h1 align="center">MIDI Player</h1>

<p align="center">Windows x64 MIDI 播放器与 VST3 乐器宿主</p>

<p align="center">
  <a href="https://github.com/PlutoFar/MIDIPlayer/releases/tag/v1.1.0">v1.1.0</a>
  ·
  <a href="https://github.com/PlutoFar/MIDIPlayer/releases">发布版本</a>
  ·
  <a href="https://github.com/PlutoFar/MIDIPlayer/issues">问题反馈</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Windows-10%2F11-0078D4" alt="Windows 10/11" />
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C" alt="C++17" />
  <img src="https://img.shields.io/badge/JUCE-8.0.15-F28C28" alt="JUCE 8.0.15" />
  <img src="https://img.shields.io/badge/x64-5C2D91" alt="x64" />
</p>

MIDI Player 使用 C++17 与 JUCE 构建，提供 MIDI 文件播放、VST3 乐器托管、播放列表管理、音频设备配置和离线音频导出。播放器主进程负责界面与播放控制，`MidiWorker.exe` 负责插件实例和编辑器窗口。

## 维护状态

| 模块 | 状态 | 说明 |
| --- | --- | --- |
| JUCE 桌面界面 | 当前维护 | 当前版本的功能开发、问题修复和发布验证入口 |
| 播放核心 | 当前维护 | 播放、列表、音频设备、导出和状态管理 |
| VST3 工作进程 | 当前维护 | 插件加载、编辑器托管和异常隔离 |
| WinUI 3 界面 | 历史归档 | 迁移至 `archive/winui-unmaintained` 分支 |
| WinUI 桥接代码 | 迁移阶段 | 迁移完成后从主分支移除 |

## 核心功能

- 64 位 VST3 乐器扫描、加载、卸载和编辑器托管
- 插件独立进程运行与异常恢复
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

大型音源的音色库、许可证和厂商运行环境按照插件发行方文档配置。

## 下载与快速开始

当前发布包：`MIDIPlayer-v1.1.0-JUCE-x64.zip`。

下载地址：[GitHub Releases](https://github.com/PlutoFar/MIDIPlayer/releases/tag/v1.1.0)

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

本地插件可放入程序目录下的 `VST3/`。插件授权、音色库和厂商专用安装目录按照插件发行方的安装流程配置。

## 便携运行与故障诊断

便携标记文件与 `MidiPlayer.exe` 放在同一目录。便携模式的配置数据保存在程序目录下的 `Settings/`。

| 标记文件 | 位置 | 模式 | 配置数据 | 调试日志 |
| --- | --- | --- | --- | --- |
| `portable.dat` | 与 `MidiPlayer.exe` 同级 | 普通便携模式 | `程序目录\Settings\` | 关闭 |
| `portable_debug.dat` | 与 `MidiPlayer.exe` 同级 | 诊断便携模式 | `程序目录\Settings\` | `程序目录\debug_log.txt` |

日常使用创建 `portable.dat`。故障定位期间使用 `portable_debug.dat`，复现问题后提交 `debug_log.txt`。

## 从源码构建

### 环境

- Visual Studio 2022 与 MSVC v143
- CMake 3.27 或更高版本
- Windows SDK 10.0.26100.0
- JUCE 8.0.15，提交 `91ad83ae34a81e0833b1a2b0866f54846370ae53`
- Steinberg ASIO SDK（可选）

### 获取源码

```powershell
git clone https://github.com/PlutoFar/MIDIPlayer.git
cd MIDIPlayer
git clone https://github.com/juce-framework/JUCE.git JUCE
git -C JUCE checkout 91ad83ae34a81e0833b1a2b0866f54846370ae53
git -C JUCE apply ../patches/juce-child-process-kill.patch
```

### 配置

```powershell
cmake --preset windows-vs2022
```

启用 ASIO：

```powershell
cmake --preset windows-vs2022 -DMIDIPLAYER_ENABLE_ASIO=ON
```

ASIO SDK 的 `common/` 目录放置于 `JUCE/modules/juce_audio_devices/native/common/`。

### 构建 JUCE 桌面版本

```powershell
cmake --build build --config Release --target MidiLegacy MidiWorker --parallel
```

构建产物：

```text
build/MidiLegacy_artefacts/Release/MidiLegacy.exe
build/MidiLegacy_artefacts/Release/MidiWorker.exe
```

便携发布包中的主程序名称为 `MidiPlayer.exe`。

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
|   |-- WinUI/          # 历史归档实现
|   `-- Worker/         # VST3 工作进程
|-- Tests/              # 原生回归测试
|-- Resources/          # 应用图标
|-- patches/            # JUCE 补丁
|-- CMakeLists.txt
`-- CMakePresets.json
```

## 运行数据

用户设置、日志和播放列表按运行模式写入用户配置目录或程序目录 `Settings/`。程序目录 `VST3/` 用于存放便携包专用插件。

## 问题反馈

请在 [Issues](https://github.com/PlutoFar/MIDIPlayer/issues) 提交以下信息：

- Windows 版本与系统缩放比例
- 程序版本或提交编号
- 音频后端、设备名称和缓冲区
- VST3 插件名称与版本
- 可重复的操作步骤
- 诊断模式生成的 `debug_log.txt`

日志提交前移除个人路径、许可证信息和其他敏感内容。
