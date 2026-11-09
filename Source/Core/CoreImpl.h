#pragma once

#include "Core.h"
#include "AudioSession.h"

#include "../AudioEngine/AudioDeviceService.h"
#include "../AudioEngine/AudioEngine.h"
#include "../AudioEngine/PluginLibrary.h"
#include "../Playlist/PlaylistManager.h"
#include "../Utils/UserSettings.h"

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
  using Operation = AudioSession::Operation;

  UserSettings &settings;
  AudioEngine engine;
  AudioDeviceService audio;
  PluginLibrary library;
  PlaylistManager playlist;
  mutable std::mutex stateMutex;
  AudioSession audioSession;

  // 用于由调用线程执行的扫描、设备配置和导出。持有者在受理后、锁外构造。
  struct SessionOperation {
    explicit SessionOperation(Impl &core) : owner(core) {}
    SessionOperation(const SessionOperation &) = delete;
    SessionOperation &operator=(const SessionOperation &) = delete;
    Impl &owner;
    ~SessionOperation() {
      StateLock lock(owner.stateMutex);
      owner.audioSession.finish();
    }
  };

  // 已载入的文件可以在列表之外继续播放。行号由当前列表和绑定 ID 派生。
  struct LoadedTrack {
    juce::File source;
    TrackId id = 0;
  } loadedTrack;
  juce::File currentPlaylistFile;
  juce::String playlistErrorText;
  // Invariant: 延迟播放只接受当前代次；暂停、停止及曲目替换递增此值使旧请求失效。
  int trackSwitchGeneration = 0;
  bool scrubbing = false;

  explicit Impl(UserSettings &settings);
  ~Impl() override;

  // Postconditions: 快照方法取得 `stateMutex` 后复制状态，不返回内部可修改引用。
  AppState buildState();
  PlaylistState buildPlaylistState();
  PlaylistSummary buildPlaylistSummary() const;
  // Preconditions: 消息线程持有 stateMutex；后台操作不能等待消息线程回调。
  // Postconditions: 返回受理结果，执行结果在消息线程无锁交付。
  juce::Result startCommandTask(Operation operation,
                                std::function<juce::Result()> work,
                                CommandCompletion completion = {});
  void handleAsyncUpdate() override;
  void timerCallback() override;
  // Concurrency: join 后才读取工作线程结果，完成回调由消息线程访问。
  std::thread commandTask;
  CommandCompletion commandCompletion;
  juce::Result commandResult{juce::Result::ok()};
  // Ordering: 在锁内受理设备操作，释放锁后执行原生调用，结束时清除互斥标记。
  juce::String configureAudio(std::function<juce::String()> operation);

  // Concurrency: 插件入口遵循 `Core` 的消息线程/扫描线程约定；目录引用访问由 `stateMutex` 串行化。
  juce::Result scan(std::function<bool()> shouldCancel);
  bool findById(const PluginId &id, juce::PluginDescription &out);
  juce::Result loadAsync(const PluginId &id, CommandCompletion completion);
  juce::Result unloadAsync(CommandCompletion completion);
  juce::Result editorAsync(CommandCompletion completion);
  void closeEditor();
  void closeEditorWhenIdle();
  bool workerCrashed() const;

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
  void clearLoadedTrack();
  int currentTrackIndex() const;
  void seek(double ratio);
  void volume(float value);
  void next();
  void prev();
  void handleTrackEnd();

  // Postconditions: 列表命令在 `stateMutex` 内维护索引、路径及错误；失败以返回值报告。
  juce::Result addToPlaylist(const juce::File &file);
  int addFilesToPlaylist(const std::vector<juce::File> &files);
  bool removeTrack(TrackId id);
  bool moveTrack(TrackId id, int toIndex);
  juce::Result refreshTrack(int index);
  bool clearPlaylist();
  void setPlayMode(int mode);
  bool saveList(const juce::File &file);
  bool loadList(const juce::File &file);

};

} // namespace midi
