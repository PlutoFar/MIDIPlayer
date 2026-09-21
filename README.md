<p align="center">
  <img src="Resources/icon.png" alt="MIDI Player 图标" width="128" />
</p>

<h1 align="center">MIDI Player</h1>

<p align="center">Windows x64 MIDI 播放器与 VST3 乐器宿主</p>

<p align="center">
  <a href="https://github.com/PlutoFar/MIDIPlayer/releases/tag/v1.2.1">v1.2.1</a>
  ·
  <a href="https://github.com/PlutoFar/MIDIPlayer/releases">发布版本</a>
  ·
  <a href="https://github.com/PlutoFar/MIDIPlayer/issues">问题反馈</a>
</p>

<p align="center">
  <a href="https://github.com/PlutoFar/MIDIPlayer/releases/latest"><img src="https://img.shields.io/github/v/release/PlutoFar/MIDIPlayer" alt="最新版本" /></a>
  <a href="https://github.com/juce-framework/JUCE/releases/tag/8.0.15"><img src="https://img.shields.io/badge/JUCE-8.0.15-F28C28" alt="JUCE 8.0.15" /></a>
</p>

MIDI Player 是 Windows MIDI（乐器数字接口）文件播放器，支持通过 VST3（虚拟演播室技术 3）乐器发声、管理播放列表和导出音频。

## 核心功能

- 64 位 VST3 乐器扫描、加载、卸载和编辑器托管
- 插件独立进程运行与异常恢复
- 向插件提供 MIDI 速度、拍号和播放位置，用于节拍同步
- MIDI 文件选择、系统文件打开和拖放导入
- 播放列表增删、排序、保存、加载和持久化
- 连续播放、列表循环、单曲循环和随机播放
- Windows 音频输出设备、采样率、缓冲区和通道配置
- WASAPI、DirectSound 和 ASIO 音频输出
- WAV、FLAC、Ogg Vorbis 离线导出
- 导出采样率、位深、质量和尾音配置
- 线性音量控制与插件延迟补偿导出
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

播放使用 2 个设备周期的音频缓冲。较小的设备缓冲可降低操作延迟；出现断续时可增大缓冲。音频设置的状态提示显示附加播放延迟上限和缓冲欠载次数。

暂停会释放按键和踏板。恢复播放或跳转位置时，播放器恢复踏板和控制器状态，并从目标位置的后续音符继续播放；此前已开始的音符不重奏。

32 位浮点 WAV 保留超过满刻度的音频数据。整数 WAV、FLAC 和 Ogg Vorbis 的输出限制在满刻度范围内；降低音量可避免过载削波。

## 下载与快速开始

当前发布包：`MIDIPlayer-v1.2.1-Windows-x64.zip`，已包含便携模式标记。

下载地址：[GitHub Releases](https://github.com/PlutoFar/MIDIPlayer/releases/tag/v1.2.1)

1. 将压缩包解压到具有写入权限的目录。
2. 确认 `MidiPlayer.exe` 与 `MidiWorker.exe` 位于同一目录。
3. 启动 `MidiPlayer.exe`。
4. 扫描插件并选择 VST3 乐器。
5. 添加 `.mid` 或 `.midi` 文件。
6. 在设置中选择音频输出设备。
7. 开始播放，或打开导出功能生成音频文件。

## VST3 插件目录

点击“扫描插件”可搜索系统及便携目录中的插件。常用目录：

```text
C:\Program Files\Common Files\VST3\
%APPDATA%\VST3\
程序目录\VST3\
```

本地插件可放入程序目录下的 `VST3/`。插件授权、音色库和厂商专用安装目录按照插件发行方的安装流程配置。

## 便携运行与故障诊断

便携标记文件与 `MidiPlayer.exe` 放在同一目录。便携模式的配置数据保存在程序目录下的 `Settings/`。

| 标记文件 | 位置 | 模式 | 配置数据 | 调试日志 |
| --- | --- | --- | --- | --- |
| `portable.dat` | 与 `MidiPlayer.exe` 同级 | 普通便携模式 | `程序目录\Settings\` | 关闭 |
| `portable_debug.dat` | 与 `MidiPlayer.exe` 同级 | 诊断便携模式 | `程序目录\Settings\` | `程序目录\debug_log.txt` |

发布包默认使用 `portable.dat`。故障定位期间将标记改为 `portable_debug.dat`，复现问题后提交 `debug_log.txt`。

升级时退出程序，保留原目录的 `Settings/` 和 `VST3/`，再解压新版本。播放列表文件及其引用的 MIDI 文件保存在用户选择的位置。

## 从源码构建

### 环境

- Visual Studio 2022 与 MSVC v143
- CMake 3.27 或更高版本
- Windows SDK 10.0.26100.0
- JUCE 8.0.15，提交 `91ad83ae34a81e0833b1a2b0866f54846370ae53`

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

ASIO 构建使用 JUCE 8.0.15 随附的 Steinberg ASIO SDK。发布包已启用 ASIO 支持。

### 构建

```powershell
cmake --build build --config Release --target MidiPlayer --parallel
```

构建产物：

```text
build/
`-- MidiPlayer_artefacts/
    `-- Release/
        |-- MidiPlayer.exe
        `-- MidiWorker.exe
```

`MidiPlayer` 目标自动构建并复制配套的 `MidiWorker.exe`。

## 问题反馈

请在 [Issues](https://github.com/PlutoFar/MIDIPlayer/issues) 提交以下信息：

- Windows 版本与系统缩放比例
- 程序版本或提交编号
- 音频后端、设备名称和缓冲区
- VST3 插件名称与版本
- 可重复的操作步骤
- 诊断模式生成的 `debug_log.txt`

日志提交前移除个人路径、许可证信息和其他敏感内容。

## 贡献

贡献入口：

- 使用 [Issues](https://github.com/PlutoFar/MIDIPlayer/issues) 提交问题和功能建议
- 使用 Pull Request 提交代码、文档和测试修改
- 提交内容附带影响范围、复现步骤和验证结果
- 界面修改同步更新用户文档

代码修改遵循现有 C++17、JUCE 和 Windows 构建约定。提交前执行相关构建与定向测试。

## 版权与第三方许可

项目自有源代码、文档和素材允许个人或组织复制、修改和再分发，分发内容保留 MIDI Player 项目名称、作者署名、版权声明和本节许可说明。

仓库和发布包包含第三方组件及其补丁。第三方组件继续按照各自许可证执行，相关版权声明和许可证文本随对应组件保留。JUCE 8 模块采用 AGPLv3 与商业许可双重许可，商业分发前需要根据发行方式完成对应许可核验。参考：[JUCE 官方许可说明](https://juce.com/get-juce/)。

任一第三方组件对商业使用、再分发或源代码提供施加更严格限制时，该限制适用于包含该组件的分发包。VST3 SDK、ASIO SDK、VST3 乐器和音色库按照各自权利人的许可条款执行。
