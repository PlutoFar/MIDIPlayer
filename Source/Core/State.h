#pragma once

#include "../Playlist/PlaybackMode.h"

#include <string>
#include <vector>

namespace midi {

/// Contract: 插件 ID 使用 `AudioPluginFormat::createIdentifierString()`，不以显示名称匹配实例。
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

// Invariant: 两个曲目数组按相同索引对应；列表变更后读取，禁止加入高频播放刷新。
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

/// Ownership: 界面持有的值快照；跨多次查询的结果不保证属于同一时刻。
struct AppState {
  PluginState plugin;
  TransportState transport;
  PlaylistSummary playlist;
  AudioState audio;
  TaskState task;
};

} // namespace midi
