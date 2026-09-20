#pragma once

#include "../Playlist/PlaybackMode.h"

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
  bool operationInProgress = false;
  bool loaded = false;
  bool editorOpen = false;
  bool workerCrashed = false;
  std::wstring loadedName;
  std::wstring lastError;
};

struct TransportState {
  bool playing = false;
  bool hasSequence = false;
  double positionSamples = 0.0;
  double durationSamples = 0.0;
  int currentTrackIndex = -1;
  std::wstring currentMidiName;
};

struct PlaylistSummary {
  bool hasUnsavedChanges = false;
  std::wstring changeSummary;
  std::wstring currentListPath;
  std::wstring lastError;
  int playMode = 1;
};

// Track collections are read on list changes, outside the transport refresh.
struct PlaylistState : PlaylistSummary {
  std::vector<std::wstring> trackNames;
  std::vector<bool> trackAvailable;
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

/// Application state snapshot consumed by the desktop interface.
struct AppState {
  PluginState plugin;
  TransportState transport;
  PlaylistSummary playlist;
  AudioState audio;
  TaskState task;
};

} // namespace midi
