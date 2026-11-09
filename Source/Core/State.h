#pragma once

#include "../Playlist/PlaybackMode.h"
#include "../Playlist/TrackId.h"

#include <string>
#include <cstdint>
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
  bool loaded = false;
  bool workerCrashed = false;
  std::wstring loadedName;
};

struct TransportState {
  bool playing = false;
  bool hasSequence = false;
  double positionSeconds = 0.0;
  double durationSeconds = 0.0;
  int currentTrackIndex = -1;
  std::wstring currentMidiName;
  std::wstring lastError;
};

struct PlaylistSummary {
  bool hasUnsavedChanges = false;
  std::wstring changeSummary;
  std::wstring currentListPath;
  std::wstring lastError;
  int playMode = 1;
};

struct PlaylistEntry {
  TrackId id;
  std::wstring name;
  std::wstring path;
  bool available;
};

// 列表变更后读取，避免在高频播放刷新中复制所有曲目。
struct PlaylistState : PlaylistSummary {
  std::vector<PlaylistEntry> tracks;
  int currentTrackIndex = -1;
};

struct AudioState {
  bool hasDevice = false;
  bool firstRunAudio = false;
  bool deviceFallback = false;
  float masterVolume = 0.8f;
  uint64_t underrunCount = 0;
  int renderLatencySamples = 0;
  std::wstring lastInitError;
  bool outputNeedsPreparation = false;
  std::wstring preparationError;
};

struct TaskState {
  bool commandActive = false;
  bool audioChangeInProgress = false;
  bool exportActive = false;
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
