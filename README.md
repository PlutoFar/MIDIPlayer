# MIDI Player

![Windows](https://img.shields.io/badge/Windows-10%2F11-0078D4)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C)
![JUCE](https://img.shields.io/badge/JUCE-8.0.15-F28C28)

MIDI Player 是一款面向 Windows x64 的 MIDI 播放器和 VST3 乐器宿主。当前维护版本采用 C++17 与 JUCE，VST3 乐器运行在独立工作进程中，插件异常不会直接终止播放器主进程。

## 维护状态

| 模块 | 状态 | 说明 |
| --- | --- | --- |
| JUCE 桌面界面 | 持续维护 | 当前功能开发、问题修复和发布均以此实现为准 |
| 播放核心与 VST3 工作进程 | 持续维护 | 负责播放、列表、导出、音频设备和插件隔离 |
| WinUI 3 界面 | 停止维护 | 不再接收功能开发和界面问题修复 |
| WinUI 桥接代码 | 待移除 | 当前主分支仅为迁移保留，后续版本将删除 |

WinUI 3 实现将分离到 `archive/winui-unmaintained` 分支，仅用于历史查阅。分支迁移完成后，主分支将移除 `Source/WinUI/`、`MidiWinBridge`、WinUI 设置适配器以及相关构建配置。新功能和问题反馈只覆盖 JUCE 桌面界面。

## 功能

- 扫描、加载和卸载 64 位 VST3 乐器
- 在独立 `MidiWorker.exe` 进程中运行插件
- 打开插件编辑器，并在插件进程异常后恢复播放器状态
- 通过文件选择器、系统文件打开和拖放导入 `.mid`、`.midi` 文件
- 编辑、排序、保存和加载播放列表
- 连续播放、列表循环、单曲循环和随机播放
- 配置 Windows 音频输出设备、采样率、缓冲区和通道
- 可选 ASIO 支持
- 导出 WAV、FLAC 和 Ogg Vorbis 音频
- 配置导出采样率、位深、质量和尾音
- 自定义背景、高斯模糊、Aero、Acrylic、遮罩和主题色
- 自定义界面字体、播放列表字体、字号和曲目行间距
- 支持窗口置顶和 `.mid`、`.midi` 文件关联
- 支持便携模式

## 系统要求

- Windows 10 1809 或更高版本
- x64 处理器和操作系统
- Microsoft Visual C++ 2015–2022 x64 运行库
- 至少一个 64 位 VST3 乐器
- 支持 WASAPI、DirectSound 或 ASIO 的音频设备

大型音源通常需要独立安装音色库、许可证管理程序和较大内存。

## 下载与运行

从 [GitHub Releases](https://github.com/PlutoFar/MIDIPlayer/releases/latest) 下载 JUCE 或 Legacy 标识的 x64 便携包。

1. 将压缩包完整解压到具有写入权限的目录。
2. 确认 `MidiPlayer.exe` 与 `MidiWorker.exe` 位于同一目录。
3. 运行 `MidiPlayer.exe`。
4. 扫描并选择 VST3 乐器。
5. 添加 MIDI 文件。
6. 在设置中选择音频输出设备。
7. 开始播放或导出音频。

不要直接在压缩包内运行程序。

## VST3 插件目录

程序会扫描以下位置：

```text
C:\Program Files\Common Files\VST3\
程序目录\VST3\
```

部分厂商会使用额外目录。扫描结果取决于插件安装方式和授权状态。

## 便携模式

在程序目录创建以下任意文件即可启用便携模式：

```text
portable.dat
portable_debug.dat
```

启用后，用户设置保存在程序目录的 `Settings/` 中，本地 VST3 插件可放入 `VST3/`。程序目录必须具有写入权限。

## 从源码构建

### 环境要求

- Visual Studio 2022
- MSVC v143 桌面 C++ 工作负载
- CMake 3.27 或更高版本
- Windows SDK 10.0.26100.0
- JUCE 8.0.15，对应提交 `91ad83ae34a81e0833b1a2b0866f54846370ae53`
- 可选：Steinberg ASIO SDK

### 获取源码与 JUCE

```powershell
git clone https://github.com/PlutoFar/MIDIPlayer.git
cd MIDIPlayer
git clone https://github.com/juce-framework/JUCE.git JUCE
git -C JUCE checkout 91ad83ae34a81e0833b1a2b0866f54846370ae53
git -C JUCE apply ../patches/juce-child-process-kill.patch
```

JUCE 补丁用于在协调进程关闭时终止无响应的插件工作进程。

### 配置工程

```powershell
cmake --preset windows-vs2022
```

启用 ASIO：

```powershell
cmake --preset windows-vs2022 -DMIDIPLAYER_ENABLE_ASIO=ON
```

ASIO 默认关闭。启用前需要从 Steinberg 获取 ASIO SDK，并将其中的 `common/` 目录放到 `JUCE/modules/juce_audio_devices/native/common/`。

### 构建播放器

```powershell
cmake --build build --config Release --target MidiLegacy MidiWorker --parallel
```

输出文件：

```text
build/MidiLegacy_artefacts/Release/MidiLegacy.exe
build/MidiLegacy_artefacts/Release/MidiWorker.exe
```

当前 CMake 目标仍使用历史名称 `MidiLegacy`。组装便携包时，将 `MidiLegacy.exe` 重命名为 `MidiPlayer.exe`。

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
|   |-- Core/           # 应用核心；暂时包含待移除的 WinUI 桥接
|   |-- Midi/           # MIDI 时序与播放控制
|   |-- Playlist/       # 播放列表模型与持久化
|   |-- PluginBridge/   # 主进程与插件工作进程协议
|   |-- UI/             # 当前维护的 JUCE 桌面界面
|   |-- Utils/          # 设置、文件关联和 Windows 工具
|   |-- WinUI/          # 已停止维护，待迁移至归档分支
|   `-- Worker/         # VST3 工作进程
|-- Tests/              # 原生回归测试
|-- Resources/          # 应用图标
|-- patches/            # JUCE 补丁
|-- CMakeLists.txt
`-- CMakePresets.json
```

## 运行数据

构建产物、JUCE 源码、插件、用户设置、日志、播放列表和本机开发配置不会进入版本控制。本仓库不分发 VST3 插件、音色库、许可证或 Steinberg ASIO SDK。

## 问题反馈

问题报告应包含：

- Windows 版本
- 程序版本或提交编号
- 音频后端与设备名称
- VST3 插件名称和版本
- 可重复的操作步骤
- 已删除私人路径和授权信息的错误日志

WinUI 3 界面已停止维护，相关功能请求和界面问题不再处理。
