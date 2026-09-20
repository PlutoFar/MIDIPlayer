#pragma once

#include "Core.h"

#include "../AudioEngine/AudioDeviceService.h"
#include "../AudioEngine/AudioEngine.h"
#include "../AudioEngine/OfflineRenderer.h"
#include "../AudioEngine/PluginLibrary.h"
#include "../Playlist/PlaylistManager.h"
#include "../Utils/UserSettings.h"

#include <atomic>
#include <functional>
#include <juce_events/juce_events.h>
#include <memory>
#include <mutex>
#include <thread>

namespace midi {

  // Ownership: `Impl` 独占服务对象、播放状态及后台命令；外部只使用 `Core` 契约。
// Concurrency: `stateMutex` 保护播放/列表元数据及互斥任务的受理过程，渲染和 IPC 不持有此锁。
// Ordering: 成员逆序析构使 `audio` 先解除设备回调，之后才销毁 `engine`。
struct Core::Impl : private juce::AsyncUpdater {
  using StateLock = std::lock_guard<std::recursive_mutex>;

  AudioEngine engine;
  AudioDeviceService audio{engine};
  PluginLibrary library;
  OfflineRenderer renderer{engine};
  PlaylistManager playlist;
  mutable std::recursive_mutex stateMutex;
  // Ordering: 导出先取得 `exportMutex`，再短暂取得 `stateMutex`；禁止反向等待。
  std::mutex exportMutex;
  std::atomic<bool> pluginScanActive{false};
  std::atomic<bool> commandTaskActive{false};
  std::atomic<bool> commandChangesAudio{false};
  std::atomic<bool> audioConfigurationActive{false};
  juce::String pluginErrorText;

  // Invariant: 文件、名称和列表索引共同描述当前曲目；-1 表示未绑定列表项。
  int currentTrackIndex = -1;
  juce::File currentMidiFile;
  juce::String currentMidiName;
  juce::File currentPlaylistFile;
  juce::String playlistErrorText;
  // Invariant: 延迟播放只接受当前代次；暂停、停止及曲目替换递增此值使旧请求失效。
  int trackSwitchGeneration = 0;
  bool isHandlingTrackEnd = false;

  // Concurrency: 导出标记/进度可独立读取，取消结果和错误文本由 `stateMutex` 保护。
  std::atomic<bool> exportActiveFlag{false};
  std::atomic<float> exportProgressValue{0.0f};
  bool exportCancelledFlag = false;
  juce::String exportErrorText;

  ~Impl();

  // Postconditions: 快照方法取得 `stateMutex` 后复制状态，不返回内部可修改引用。
  AppState buildState();
  PlaylistState buildPlaylistState();
  // Preconditions: 消息线程调用，完成回调不得抛出异常；`operation` 不得等待界面回调。
  // Postconditions: 忙时返回 `false`；受理后由工作线程执行，再由消息线程交付结果。
  // Ordering: `changesAudio` 只控制播放互斥；所有插件任务仍共享同一命令执行顺序。
  bool startCommandTask(std::function<bool()> operation,
                       std::function<void(bool)> completion,
                       bool changesAudio = true);
  void handleAsyncUpdate() override;
  // Concurrency: 读取 `commandTaskSucceeded` 前必须完成 `join`；回调和延迟关闭标记仅由消息线程访问。
  std::thread commandTask;
  std::function<void(bool)> commandCompletion;
  bool commandTaskSucceeded = false;
  juce::String commandTaskError;
  bool closeEditorWhenIdle = false;
  // Ordering: 在锁内受理设备操作，释放锁后执行原生调用，结束时清除互斥标记。
  juce::String configureAudio(std::function<juce::String()> operation);
  double sampleRate() const;

  // Concurrency: 插件入口遵循 `Core` 的消息线程/扫描线程约定；目录引用访问由 `stateMutex` 串行化。
  bool scan(std::function<bool()> shouldCancel);
  bool findById(const PluginId &id, juce::PluginDescription &out);
  bool loadAsync(const PluginId &id, std::function<void(bool)> completion);
  bool unloadAsync(std::function<void(bool)> completion);
  bool editorAsync(std::function<void(bool)> completion);
  void closeEditor();
  void terminateCrashedWorker();

  // Preconditions: 播放入口使用已选定的本机路径和列表索引；`loadMidi` 负责验证 MIDI 内容。
  // Concurrency: 播放方法内部取得 `stateMutex`；导出调用 `loadMidi` 时必须已独占导出状态。
  bool canStartPlayback();
  bool loadMidi(const juce::File &file);
  bool beginMidiLoad(const juce::File &file, bool addToList, bool autoPlay,
                     std::function<void(bool)> completion = {},
                     std::function<void()> onPluginMissing = {});
  juce::String midiErrorText;
  void play();
  void pause();
  void togglePlay();
  void stop();
  void seek(double ratio);
  void volume(float value);
  void next();
  void prev();
  void handleTrackEnd();
  void tick(bool uiSuppressTrackAdvance);

  // Postconditions: 列表命令在 `stateMutex` 内维护索引、路径及错误；失败以返回值报告。
  bool addToPlaylist(const juce::File &file);
  int addFilesToPlaylist(const std::vector<juce::File> &files);
  bool removeTrack(int index);
  bool moveTrack(int fromIndex, int toIndex);
  bool refreshTrack(int index);
  bool clearPlaylist();
  void setPlayMode(int mode);
  juce::File trackFileAt(int index) const;
  bool saveList(const juce::File &file);
  bool loadList(const juce::File &file);

  // Preconditions: 捕获和恢复由独占导出流程调用；位置以秒保存，避免设备采样率变化导致偏移。
  // Postconditions: 恢复失败清空当前 MIDI 并返回诊断；已经失效的延迟切曲状态不被恢复。
  struct ExportPlaybackState {
    int trackIndex = -1;
    juce::File file;
    double positionSeconds = 0.0;
    bool wasPlaying = false;
  };
  ExportPlaybackState captureExportPlaybackState();
  juce::Result restoreExportPlaybackState(const ExportPlaybackState &state);
  Core::ExportResult runExport(int trackIndex, const ExportSettings &settings,
                               const juce::File &targetFile,
                               std::function<void(float)> onProgress,
                               std::function<bool()> shouldCancel);
};

} // namespace midi
