#pragma once

#include "State.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace midi {

class LegacyCoreAdapter;

/**
    midi::Core —— 与界面框架无关的应用状态与命令入口。

    WinUI 与 Legacy 共用这一个门面：通过 state() 取一致状态快照、命令方法
    驱动业务。Core 内部持有唯一的 AudioEngine 与
    PlaylistManager，公共契约不暴露 AudioEngine&、PlaylistManager&、
    PluginBridgeClient& 或原始 worker 句柄，也不暴露任何 JUCE UI / WinUI 类型。

    Legacy 既有控件需要的底层对象绑定经 LegacyCoreAdapter 过渡；WinUI
    只使用 state() + 命令。

    见 docs/winui3-refactor-plan.md（Core Interface）。
*/
class Core {
public:
  struct ExportRequest {
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

  Core();
  ~Core();

  Core(const Core &) = delete;
  Core &operator=(const Core &) = delete;

  bool init();
  void shutdown();

  AppState state() const;

  // 每帧由 UI 调用，承载与界面无关的轮询业务：采样率同步、曲目结束推进、
  // seek 后延迟恢复、worker 崩溃后停止播放。uiSuppressTrackAdvance 在用户拖动
  // 进度条时为 true，抑制自动切到下一曲。
  void tick(bool uiSuppressTrackAdvance);

  // 插件库
  bool scan(std::function<bool()> shouldCancel = {});
  std::vector<PluginInfo> plugins() const;
  bool load(const PluginId &id);
  void unload();
  bool editor();
  void closeEditor();
  bool hasPluginLoaded() const;
  std::wstring loadedPluginName() const;
  bool workerCrashed() const;
  std::wstring pluginError() const;
  std::wstring lastPluginError() const;
  void terminateCrashedWorker();
  void cancelPendingPluginOperation();

  // 播放 / MIDI
  bool openMidi(const std::wstring &path);
  bool openMidiFromShell(const std::wstring &path,
                         std::function<void()> onPluginMissing);
  bool loadMidiFile(const std::wstring &path);
  void play();
  void pause();
  void togglePlay();
  void stop();
  void next();
  void prev();
  void playTrackAt(int index);
  void seek(double ratio);
  void volume(float value);
  int currentTrackIndex() const;
  double sampleRate() const;

  // 播放列表
  bool addToPlaylist(const std::wstring &path);
  int addFilesToPlaylist(const std::vector<std::wstring> &paths);
  bool removeTrack(int index);
  bool moveTrack(int fromIndex, int toIndex);
  bool refreshTrack(int index);
  int findTrackIndex(const std::wstring &path) const;
  void clearPlaylist();
  void setPlayMode(int mode);
  std::wstring trackFileAt(int index) const;
  bool saveList(const std::wstring &path);
  bool loadList(const std::wstring &path);
  std::wstring lastPlaylistError() const;
  void setCurrentPlaylistFile(const std::wstring &path);
  std::wstring currentPlaylistFile() const;

  // 音频设备
  bool hasAudioDevice() const;
  bool isFirstRunAudio() const;
  bool wasDeviceRestoredWithFallback() const;
  std::vector<std::wstring> audioOutputDevices();
  std::wstring currentAudioDevice();
  bool setAudioDevice(const std::wstring &name);
  std::vector<int> sampleRates();
  int currentSampleRate();
  bool setSampleRate(int sampleRate);
  std::vector<int> bufferSizes();
  int currentBufferSize();
  bool setBufferSize(int bufferSize);
  void playTestSound();
  std::wstring audioStatus();
  void saveAudioDeviceSettings();

  // 离线导出（同步执行；进度/取消由 UI 提供回调，模态窗口留在 UI 层）。
  enum class ExportResult { Succeeded, Cancelled, Failed };
  ExportResult runExport(const ExportRequest &request,
                         std::function<void(float)> onProgress = {},
                         std::function<bool()> shouldCancel = {});
  bool isExportActive() const;
  std::wstring lastExportError() const;

private:
  friend class LegacyCoreAdapter;

  struct Impl;
  std::unique_ptr<Impl> impl;
};

} // namespace midi
