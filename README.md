# Modern MIDI Player

Modern MIDI Player 是一个基于 C++17 和 JUCE 的 Windows 桌面 MIDI 播放器。它可以加载 VST3 虚拟乐器，把 MIDI 文件作为播放源，并通过播放列表、音频设备设置和界面自定义功能提供一个独立的 MIDI 播放环境。

![Windows](https://img.shields.io/badge/Windows-10%2F11-0078D4)
![C++](https://img.shields.io/badge/C%2B%2B-17-blue)
![JUCE](https://img.shields.io/badge/JUCE-required-orange)

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
├── AudioEngine/      # Audio device, VST3 scanning, plugin loading, audio graph
├── Midi/             # MIDI sequencing and playback timing
├── Playlist/         # Playlist data model and JSON save/load logic
├── Resources/        # Application resources
├── UI/               # Main window, controls, playlist panel, settings dialogs
├── Utils/            # Settings, logging, and Windows helper utilities
├── CMakeLists.txt    # CMake build definition
├── HELP.txt          # End-user help text
└── Main.cpp          # JUCE application entry point
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
