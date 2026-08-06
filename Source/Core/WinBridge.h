#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

// STL-only facade over midi::Core for the WinUI (C++/WinRT) front-end.
// JUCE and WinRT headers do not coexist well in one translation unit (heavy
// macro / type clashes), so the WinUI project includes ONLY this header — no
// JUCE. WinBridge.cpp is the single JUCE-side translation unit that converts
// between juce types and std::wstring and forwards to midi::Core.

namespace midi {

class Core;

struct WinPlugin {
  std::wstring id;
  std::wstring name;
  std::wstring manufacturer;
  std::wstring formatName;
};

struct WinState {
  // --- plugin / library ---
  bool pluginLoaded = false;
  bool loadInProgress = false;
  bool loadFinished = false;
  bool loadSucceeded = false;
  std::uint64_t loadSessionId = 0;
  std::wstring loadMessage;
  bool editorOpen = false;
  bool workerCrashed = false;
  bool scanning = false;
  bool scanFinished = false;
  bool scanSucceeded = false;
  std::uint64_t scanSessionId = 0;
  std::wstring scanMessage;
  std::wstring pluginName;
  std::wstring pluginError;
  std::vector<WinPlugin> plugins;

  // --- transport ---
  bool playing = false;
  bool hasSequence = false;
  double positionSamples = 0.0;
  double durationSamples = 0.0;
  int trackIndex = -1; // 正在播放的曲目（区别于 UI 选中行）
  std::wstring midiName;
  std::vector<std::wstring> tracks;
  std::vector<bool> trackAvailable;

  // --- playlist ---
  int playMode = 1; // 1=Sequential 2=LoopList 3=LoopSingle 4=Shuffle
  bool hasUnsavedChanges = false;
  std::wstring playlistError;

  // --- audio ---
  bool hasAudioDevice = false;
  bool firstRunAudio = false;
  bool deviceFallback = false;
  float volume = 0.8f;
  bool muted = false;

  // --- export ---
  bool exportActive = false;
  float exportProgress = 0.0f;
  bool exportFinished = false;
  bool exportSucceeded = false;
  std::uint64_t exportSessionId = 0;
  std::wstring exportMessage;
};

struct WinExportRequest {
  int trackIndex = -1;
  std::wstring targetPath;
  std::wstring formatName = L"WAV";
  double sampleRate = 96000.0;
  int bitDepth = 24;
  bool useFloatingPoint = false;
  bool autoTail = true;
  double fixedTailSeconds = 3.0;
  std::wstring title;
  int qualityIndex = 0;
};

class WinCore {
public:
  WinCore();
  ~WinCore();

  WinCore(const WinCore &) = delete;
  WinCore &operator=(const WinCore &) = delete;

  bool init(bool restoreSession = true);
  WinState state();

  // 插件库
  void scan();      // 同步（阻塞）
  void scanAsync(); // 后台线程扫描，期间 state().scanning=true
  bool load(const std::wstring &id);
  bool loadAsync(const std::wstring &id);
  void unload();
  bool openEditor();
  void terminateCrashedWorker();

  // 播放 / MIDI
  bool openMidi(const std::wstring &path); // 加入列表并尝试播放
  bool addToPlaylist(const std::wstring &path);
  int addFiles(const std::vector<std::wstring> &paths); // 返回新增数量
  void play();
  void pause();
  void togglePlay();
  void stop();
  void next();
  void prev();
  void playTrackAt(int index);
  void seek(double ratio);
  void volume(float value); // 任何手动音量更改都会解除静音
  void toggleMute();
  void setMuted(bool muted);
  void setPlayMode(int mode); // 1-4
  void tick(bool suppressTrackAdvance = false);

  // 播放列表编辑
  bool removeTrack(int index);
  bool moveTrack(int fromIndex, int toIndex);
  void clearPlaylist();
  void revealTrack(int index); // 在资源管理器中定位
  bool saveList(const std::wstring &path);
  bool loadList(const std::wstring &path);

  // 导出
  bool exportAudio(const WinExportRequest &request);
  void cancelExport();

  // 音频设备
  std::vector<std::wstring> audioOutputDevices();
  std::wstring currentAudioDevice();
  void setAudioDevice(const std::wstring &name);
  std::vector<int> sampleRates();
  int currentSampleRate();
  void setSampleRate(int sr);
  std::vector<int> bufferSizes();
  int currentBufferSize();
  void setBufferSize(int bs);
  void playTestSound();
  std::wstring audioStatus();

  // Windows shell integration
  bool isMidiFileAssociated();
  bool setMidiFileAssociated(bool associated);

  double sampleRate();

private:
  std::unique_ptr<struct JuceRuntime> juceRuntime; // JUCE MessageManager/GUI 初始化
  std::unique_ptr<Core> core;

  std::atomic<bool> scanning{false};
  std::atomic<bool> scanCancel{false};
  std::atomic<bool> loading{false};
  std::thread scanThread;
  std::thread loadThread;
  std::thread exportThread;
  std::atomic<bool> exportCancel{false};
  std::mutex scanMutex;
  bool scanFinished = false;
  bool scanSucceeded = false;
  std::uint64_t scanSessionId = 0;
  std::wstring scanMessage;
  std::mutex loadMutex;
  bool loadFinished = false;
  bool loadSucceeded = false;
  std::uint64_t loadSessionId = 0;
  std::wstring loadMessage;
  std::mutex exportMutex;
  bool exportFinished = false;
  bool exportSucceeded = false;
  std::uint64_t exportSessionId = 0;
  std::wstring exportMessage;
  bool muted = false;
  float volumeBeforeMute = 0.8f;
};

} // namespace midi
