<p align="center">
  <img src="Resources/icon.png" width="128" alt="MIDI Player 图标" />
</p>

<h1 align="center">MIDI Player</h1>

<p align="center">Windows x64 MIDI 文件播放器与 VST3 乐器宿主</p>

<p align="center">
  <a href="https://github.com/PlutoFar/MIDIPlayer/releases/tag/v1.1.0">v1.1.0</a>
  ·
  <a href="https://github.com/PlutoFar/MIDIPlayer/releases">发布版本</a>
  ·
  <a href="https://github.com/PlutoFar/MIDIPlayer/issues">Issues</a>
</p>

<p align="center">
  <img src="https://img.shields.io/badge/Windows-10%2F11-0078D4" alt="Windows 10/11" />
  <img src="https://img.shields.io/badge/C%2B%2B-17-00599C" alt="C++17" />
  <img src="https://img.shields.io/badge/JUCE-8.0.15-F28C28" alt="JUCE 8.0.15" />
  <img src="https://img.shields.io/badge/x64-5C2D91" alt="x64" />
</p>

MIDI Player 将 MIDI 文件、VST3 乐器和 Windows 音频设备组合在一个桌面工作流中。播放器主进程负责界面与播放控制，`MidiWorker.exe` 负责插件实例和编辑器窗口。

## 发布版本

| 文件 | 用途 |
| --- | --- |
| `MIDIPlayer-v1.1.0-JUCE-x64.zip` | 当前维护版本，JUCE 桌面界面 |
| `MidiPlayer.exe` | 播放器主程序 |
| `MidiWorker.exe` | VST3 插件工作进程 |
| `portable.dat` | 普通便携运行标记 |
| `README.md`、`HELP.txt` | 使用说明 |

发布包位于 [GitHub Releases](https://github.com/PlutoFar/MIDIPlayer/releases/tag/v1.1.0)。完整解压后启动 `MidiPlayer.exe`。

## 三个常用工作流

### 播放 MIDI 文件

1. 启动播放器。
2. 扫描 VST3 插件。
3. 选择乐器。
4. 添加 `.mid` 或 `.midi` 文件。
5. 在音频设置中选择输出设备。
6. 使用播放控制开始播放。

### 配置本地音源

将 64 位 VST3 插件放入以下任一目录：

```text
C:\Program Files\Common Files\VST3\
程序目录\VST3\
```

启动播放器后执行插件扫描。授权、音色库和厂商专用安装目录按照插件发行方的要求配置。

### 导出音频

在播放列表中选择曲目，打开离线导出，设置输出格式、采样率、位深、质量和尾音，选择目标文件后开始渲染。

## 播放列表与界面设置

- 播放模式：连续播放、列表循环、单曲循环、随机播放
- 列表操作：添加、删除、排序、保存、加载
- 字体设置：界面字体、播放列表字体、字号
- 行间距：自动计算或手动调整
- 背景效果：图片、模糊、Aero、Acrylic、遮罩、主题色
- 窗口行为：置顶、窗口位置和大小记忆、MIDI 文件关联

## 程序目录中的文件

| 文件或目录 | 作用 |
| --- | --- |
| `MidiPlayer.exe` | 播放器主程序 |
| `MidiWorker.exe` | VST3 插件工作进程 |
| `portable.dat` | 与主程序同级时启用普通便携模式 |
| `portable_debug.dat` | 与主程序同级时启用诊断模式 |
| `Settings/` | 便携模式的配置和窗口状态 |
| `VST3/` | 程序目录下的本地插件目录 |
| `debug_log.txt` | 诊断模式生成的详细日志 |

日常运行使用 `portable.dat`。故障定位使用 `portable_debug.dat`，复现问题后提交 `debug_log.txt`。

## 系统兼容性

| 项目 | 要求 |
| --- | --- |
| Windows | Windows 10 1809 或更高版本 |
| 架构 | x64 |
| 系统运行库 | Microsoft Visual C++ 2015–2022 Redistributable x64 |
| 音频后端 | WASAPI、DirectSound 或 ASIO |
| 插件格式 | 64 位 VST3 |

大型音源的音色库、许可证和厂商运行环境按插件发行方文档配置。

## 源码工程

### 构建环境

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

### 构建 JUCE 桌面版本

```powershell
cmake --preset windows-vs2022
cmake --build build --config Release --target MidiLegacy MidiWorker --parallel
```

构建产物：

```text
build/MidiLegacy_artefacts/Release/MidiLegacy.exe
build/MidiLegacy_artefacts/Release/MidiWorker.exe
```

便携发布包中的主程序名称为 `MidiPlayer.exe`。

ASIO 构建：

```powershell
cmake --preset windows-vs2022 -DMIDIPLAYER_ENABLE_ASIO=ON
cmake --build build --config Release --target MidiLegacy MidiWorker --parallel
```

ASIO SDK 的 `common/` 目录放置于 `JUCE/modules/juce_audio_devices/native/common/`。

## 维护策略

| 组件 | 状态 |
| --- | --- |
| JUCE 桌面界面 | 当前维护实现 |
| 播放核心与 VST3 工作进程 | 当前维护实现 |
| WinUI 3 界面 | 历史归档 |
| WinUI 桥接代码 | 迁移完成后从主分支移除 |

WinUI 3 实现归档至 `archive/winui-unmaintained` 分支。当前主分支的功能开发、问题修复和发布验证均以 JUCE 桌面界面为准。

## 问题反馈

请在 [Issues](https://github.com/PlutoFar/MIDIPlayer/issues) 提交以下信息：

- Windows 版本与系统缩放比例
- 程序版本或提交编号
- 音频后端、设备名称和缓冲区
- VST3 插件名称与版本
- 可重复的操作步骤
- 诊断模式生成的 `debug_log.txt`

日志提交前移除个人路径、许可证信息和其他敏感内容。
