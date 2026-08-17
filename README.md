# MIDI Player

[简体中文](README_CN.md) | English

MIDI Player is a Windows desktop player and VST3 instrument host built with C++17, JUCE, and WinUI 3. It provides two front ends over the same playback core: a native WinUI edition and a JUCE-based Legacy edition.

![Windows](https://img.shields.io/badge/Windows-10%2F11-0078D4)
![C++](https://img.shields.io/badge/C%2B%2B-17-00599C)
![JUCE](https://img.shields.io/badge/JUCE-8.0.15-F28C28)
![WinUI](https://img.shields.io/badge/WinUI-3-0078D4)

## Editions

| Edition | UI | Recommended use | Runtime requirements |
| --- | --- | --- | --- |
| WinUI | Windows App SDK / WinUI 3 | Windows 11-style native interface | Windows App Runtime 2.3.1 x64 or a compatible newer 2.x release; VC++ x64 runtime |
| Legacy | JUCE desktop UI | Broad compatibility and systems without Windows App Runtime | VC++ x64 runtime |

Both editions use `MidiWorker.exe` to host VST3 instruments in a separate process. The worker must remain next to `MidiPlayer.exe`.

## Features

- VST3 instrument discovery, loading, editor hosting, and crash isolation
- MIDI file import by picker, shell open, or drag and drop
- Editable and persistent playlists with sequential, list loop, single loop, and shuffle modes
- Audio device configuration with Windows audio backends and optional ASIO support
- Offline export to WAV, FLAC, and Ogg Vorbis with format-aware sample-rate, bit-depth, quality, and tail options
- Custom backgrounds with Gaussian blur, Aero, Acrylic, overlay, recent-image history, and palette extraction
- Custom interface and playlist fonts
- Window pinning and `.mid` / `.midi` file association support
- Portable mode through `portable.dat`, with local `Settings/` and `VST3/` directories

## Download

Download the current portable packages from [GitHub Releases](https://github.com/PlutoFar/MIDIPlayer/releases/latest):

- `MIDIPlayer-v1.0.0-WinUI-x64.zip`
- `MIDIPlayer-v1.0.0-Legacy-x64.zip`

Extract one package to a writable directory and run `MidiPlayer.exe`. Do not run the executable directly from the ZIP archive.

### WinUI dependencies

Install these x64 runtimes before starting the WinUI edition:

- [Windows App Runtime 2.3.1 x64](https://aka.ms/windowsappsdk/2.3/2.3.1/windowsappruntimeinstall-x64.exe), or a compatible newer 2.x release
- [Microsoft Visual C++ 2015–2022 Redistributable x64](https://aka.ms/vc14/vc_redist.x64.exe)

The Legacy edition requires only the Visual C++ x64 runtime.

## Quick start

1. Install a 64-bit VST3 instrument.
2. Start `MidiPlayer.exe`.
3. Scan for plugins and select an instrument.
4. Add a `.mid` or `.midi` file to the playlist.
5. Select the required audio output in Settings.
6. Start playback or export the track to audio.

The scanner checks the system VST3 directory and the package-local `VST3/` directory. Some instruments require separate activation or sample-library installation.

## Build from source

### Requirements

- Windows 10 version 1809 or newer, x64
- Visual Studio 2022 with the MSVC v143 desktop C++ workload
- CMake 3.27 or newer
- Windows SDK 10.0.26100.0
- JUCE at commit `91ad83ae34a81e0833b1a2b0866f54846370ae53` (JUCE 8.0.15 source version)
- For WinUI: C++/WinRT and Windows App SDK C++ project support in Visual Studio
- Optional ASIO build: Steinberg ASIO SDK

Clone the repository and the pinned JUCE revision:

```powershell
git clone https://github.com/PlutoFar/MIDIPlayer.git
cd MIDIPlayer
git clone https://github.com/juce-framework/JUCE.git JUCE
git -C JUCE checkout 91ad83ae34a81e0833b1a2b0866f54846370ae53
git -C JUCE apply ../patches/juce-child-process-kill.patch
```

The JUCE patch ensures that an unresponsive plugin worker is terminated when the coordinator closes. It is required by the current worker lifecycle.

ASIO is disabled by default so the project builds without the proprietary ASIO SDK. To enable it, download the SDK from Steinberg and place its `common/` directory at `JUCE/modules/juce_audio_devices/native/common/`.

### Configure

```powershell
cmake --preset windows-vs2022
```

For an ASIO-enabled build:

```powershell
cmake --preset windows-vs2022 -DMIDIPLAYER_ENABLE_ASIO=ON
```

### Legacy edition

```powershell
cmake --build build --config Release --target MidiLegacy MidiWorker --parallel
```

Outputs:

```text
build/MidiLegacy_artefacts/Release/MidiLegacy.exe
build/MidiWorker_artefacts/Release/MidiWorker.exe
```

Rename `MidiLegacy.exe` to `MidiPlayer.exe` when assembling a portable package.

### WinUI edition

Build the shared native libraries and worker first:

```powershell
cmake --build build --config Release --target MidiCore MidiWinBridge MidiWorker --parallel
```

Then run the following command from a Visual Studio 2022 developer shell:

```powershell
msbuild Source\WinUI\MidiPlayer.vcxproj -restore -p:Configuration=Release -p:Platform=x64 -v:minimal -nologo
```

The framework-dependent WinUI payload is generated under `Source/WinUI/x64/Release/MidiPlayer/`.

### Tests

```powershell
cmake --build build --config Release --target MidiTests
.\build\MidiTests_artefacts\Release\MidiTests.exe
```

Local batch build helpers are intentionally excluded from the repository. The commands above are the supported source-build entry points.

## Repository layout

```text
.
|-- Source/
|   |-- AudioEngine/    # Audio graph, device management, and offline export
|   |-- Core/           # Shared application state and WinUI/Legacy bridges
|   |-- Midi/           # MIDI sequencing and transport
|   |-- Playlist/       # Playlist model and persistence
|   |-- PluginBridge/   # Host/worker protocol and shared audio buffers
|   |-- UI/             # JUCE Legacy interface
|   |-- Utils/          # Settings, shell integration, and Windows helpers
|   |-- WinUI/          # WinUI 3 front end
|   `-- Worker/         # VST3 worker executable
|-- Tests/              # Native regression tests
|-- Resources/          # Application icons
|-- patches/            # Required third-party source patch
|-- CMakeLists.txt
`-- CMakePresets.json
```

## Runtime data

Build outputs, downloaded dependencies, plugins, settings, logs, playlists, and generated WinUI files are excluded from version control. VST3 plugins and the Steinberg ASIO SDK are not distributed by this repository.

## Issue reports

Include the Windows version, selected edition, audio backend, plugin name and version, and reproducible steps. Remove private paths and licensing information from logs before attaching them.
