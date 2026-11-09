#pragma once

#include "Core.h"

#include "../AudioEngine/AudioDeviceService.h"
#include "../AudioEngine/AudioEngine.h"
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
struct Core::Impl : private juce::AsyncUpdater, private juce::Timer {
  using StateLock = std::lock_guard<std::mutex>;

  UserSettings &settings;
  AudioEngine engine;
  AudioDeviceService audio;
  PluginLibrary library;
  PlaylistManager playlist;
  mutable std::mutex stateMutex;
  std::atomic<bool> pluginScanActive{false};
  std::atomic<bool> commandTaskActive{false};
  std::atomic<bool> commandChangesAudio{false};
  std::atomic<bool> audioConfigurationActive{false};

  // Invariant: 文件、名称和列表索引共同描述当前曲目；-1 表示未绑定列表项。
  int currentTrackIndex = -1;
  juce::File currentMidiFile;
  juce::String currentMidiName;
  juce::File currentPlaylistFile;
  juce::String playlistErrorText;
  // Invariant: 延迟播放只接受当前代次；暂停、停止及曲目替换递增此值使旧请求失效。
  int trackSwitchGeneration = 0;
  bool scrubbing = false;

  // Concurrency: 导出在核心锁内受理，结束时解除实时控制的互斥限制。
  std::atomic<bool> exportActiveFlag{false};

  explicit Impl(UserSettings &settings);
  ~Impl() override;

  // Postconditions: 快照方法取得 `stateMutex` 后复制状态，不返回内部可修改引用。
  AppState buildState();
  PlaylistState buildPlaylistState();
  PlaylistSummary buildPlaylistSummary() const;
  // Preconditions: 消息线程持有 stateMutex；后台操作不能等待消息线程回调。
  // Postconditions: 返回受理结果，执行结果在消息线程无锁交付。
  // Ordering: `changesAudio` 只控制播放互斥；所有插件任务仍共享同一命令执行顺序。
  juce::Result startCommandTask(std::function<juce::Result()> operation,
                                CommandCompletion completion,
                                bool changesAudio = true);
  void handleAsyncUpdate() override;
  void timerCallback() override;
  // Concurrency: join 后才读取工作线程结果；回调和延迟关闭标记仅由消息线程访问。
  std::thread commandTask;
  CommandCompletion commandCompletion;
  juce::Result commandResult{juce::Result::ok()};
  bool closeEditorWhenIdle = false;
  // Ordering: 在锁内受理设备操作，释放锁后执行原生调用，结束时清除互斥标记。
  juce::String configureAudio(std::function<juce::String()> operation);

  // Concurrency: 插件入口遵循 `Core` 的消息线程/扫描线程约定；目录引用访问由 `stateMutex` 串行化。
  juce::Result scan(std::function<bool()> shouldCancel);
  bool findById(const PluginId &id, juce::PluginDescription &out);
  juce::Result loadAsync(const PluginId &id, CommandCompletion completion);
  juce::Result unloadAsync(CommandCompletion completion);
  juce::Result editorAsync(CommandCompletion completion);
  void closeEditor();
  void terminateCrashedWorker();

  // Preconditions: 查询和命令受理均由调用方持有 stateMutex。
  bool canStartPlayback();
  bool beginMidiLoad(const juce::File &file, TrackId trackId,
                     std::function<void(bool)> completion = {},
                     std::function<void()> onPluginMissing = {});
  juce::String midiErrorText;
  void play();
  void pause();
  void togglePlay();
  void stop();
  void resetPlayback();
  void seek(double ratio);
  void volume(float value);
  void next();
  void prev();
  void handleTrackEnd();

  // Postconditions: 列表命令在 `stateMutex` 内维护索引、路径及错误；失败以返回值报告。
  bool addToPlaylist(const juce::File &file);
  int addFilesToPlaylist(const std::vector<juce::File> &files);
  bool removeTrack(TrackId id);
  bool moveTrack(TrackId id, int toIndex);
  bool refreshTrack(int index);
  bool clearPlaylist();
  void setPlayMode(int mode);
  bool saveList(const juce::File &file);
  bool loadList(const juce::File &file);

};

} // namespace midi
