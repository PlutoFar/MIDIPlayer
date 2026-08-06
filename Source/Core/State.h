#pragma once

#include <string>
#include <vector>

namespace midi {

/// Stable plugin identifier (AudioPluginFormat::createIdentifierString()).
using PluginId = std::wstring;

struct PluginInfo {
  PluginId id;
  std::wstring name;
  std::wstring manufacturer;
  std::wstring formatName;
};

struct PluginState {
  bool scanning = false;
  bool loadInProgress = false;
  bool loaded = false;
  bool editorOpen = false;
  bool workerCrashed = false;
  std::wstring loadedName;
  std::wstring lastError;
  std::vector<PluginInfo> available;
};

struct TransportState {
  bool playing = false;
  bool hasSequence = false;
  double positionSamples = 0.0;
  double durationSamples = 0.0;
  int currentTrackIndex = -1;
  std::wstring currentMidiName;
};

struct PlaylistState {
  std::vector<std::wstring> trackNames;
  std::vector<bool> trackAvailable;
  bool hasUnsavedChanges = false;
  std::wstring changeSummary;
  std::wstring currentListPath;
  std::wstring lastError;
  int playMode = 1;
};

struct AudioState {
  bool hasDevice = false;
  bool firstRunAudio = false;
  bool deviceFallback = false;
  float masterVolume = 0.8f;
  bool muted = false;
  std::wstring lastInitError;
};

struct TaskState {
  bool exportActive = false;
  float exportProgress = 0.0f;
  bool exportCancelled = false;
  std::wstring exportError;
};

/// UI-agnostic snapshot of the whole application, consumed by both the WinUI
/// and Legacy front-ends. See docs/winui3-refactor-plan.md (Core Interface).
struct AppState {
  PluginState plugin;
  TransportState transport;
  PlaylistState playlist;
  AudioState audio;
  TaskState task;
};

} // namespace midi
