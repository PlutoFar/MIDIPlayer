# Modern MIDI Player

一个符合 Windows 11 UI 规范的独立 VST3i MIDI 播放器，使用 C++ 和 JUCE 框架开发。

![Windows 11](https://img.shields.io/badge/Windows%2011-Fluent-0078D4)
![C++17](https://img.shields.io/badge/C%2B%2B-17-blue)
![JUCE](https://img.shields.io/badge/JUCE-7.0%2B-orange)

## ✨ 功能特性

- **VST3 宿主**：加载任意 VST3i 虚拟乐器插件
- **ASIO 支持**：低延迟音频输出
- **播放列表**：拖放 MIDI 文件，支持四种播放模式
- **自定义背景**：支持高斯模糊、Aero、Acrylic 效果
- **Monet 配色**：从背景图自动提取主题色
- **88键钢琴可视化**：实时音符高亮显示
- **便携模式**：支持 U 盘随身携带

## 📁 项目结构

```
Source/
├── Main.cpp                # 应用入口
├── AudioEngine/            # 音频处理与 VST3 管理
├── Midi/                   # MIDI 播放逻辑
├── Playlist/               # 播放列表管理
├── UI/                     # 界面组件
│   ├── MainContentComponent.h
│   ├── BackgroundComponent.h
│   ├── PlaylistPanel.h
│   └── MidiVisualizer.h
└── Utils/                  # 工具类
```

## 🛠️ 构建说明

### 前置条件
- CMake 3.22+
- Visual Studio 2022 (MSVC v143)
- JUCE Framework 7.0+ (已包含在项目中)

### 构建步骤

```bash
# 配置
cmake -B build -G "Visual Studio 17 2022"

# 编译
cmake --build build --config Release

# 运行
.\build\ModernMidiPlayer_artefacts\Release\"Modern MIDI Player.exe"
```

## 🎮 快速开始

1. 点击 **扫描插件** 扫描 VST3 乐器
2. 从下拉菜单选择虚拟乐器
3. 将 MIDI 文件拖入窗口
4. 点击 **播放**

### 快捷键

| 按键 | 功能 |
|------|------|
| Space | 播放/暂停 |
| ← / → | 上一曲/下一曲 |

## 📄 许可证

MIT License

---

*Developed with ❤️ using JUCE Framework*
