#pragma once

#include "../Midi/MidiPlayer.h"
#include "../PluginBridge/PluginBridgeClient.h"
#include "RealtimeAudioBuffer.h"
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

// Responsibilities: 生成 MIDI 事件、驱动插件渲染并应用实时输出处理。
// Ownership: 持有 `MidiPlayer` 和进程桥接客户端；设备与文件编码由外部服务持有。
// Concurrency: `Core` 串行化插件命令与导出；JUCE 在设备回调线程调用处理器接口。
// Ordering: 所有设备回调和导出必须先结束，再析构引擎及共享内存。
class AudioEngine final : public juce::AudioProcessor, private juce::Thread {
public:
  AudioEngine();
  ~AudioEngine() override;
  // Preconditions: 描述来自已扫描目录；调用方已独占插件任务，禁止与导出并发。
  // Ordering: 暂停实时处理，卸载旧实例，再加载并配置新实例；替换失败不恢复旧插件。
  // Failures: 加载返回 `false` 并记录 `getLastPluginError`；卸载异常由进程状态报告。
  bool loadPlugin(const juce::PluginDescription &description,
                  std::function<void(double)> prepareMidi = {});
  void unloadPlugin();
  // Preconditions: 已加载插件，命令线程独占桥接控制通道；窗口归工作进程所有。
  // Failures: 打开返回 `false` 并记录诊断；关闭错误通过进程状态查询。
  bool openPluginEditor();
  void closePluginEditor() { bridge.closeEditor(); }
  // Concurrency: 插件状态、名称及进程错误查询使用原子状态或内部锁，可与命令线程并发。
  bool hasPluginLoaded() const { return bridge.isPluginLoaded(); }
  juce::String getLoadedPluginName() const {
    return bridge.getLoadedPluginName();
  }
  bool hasPluginWorkerCrashed() const {
    return bridge.getStatus() == PluginBridge::BridgeStatus::crashed;
  }
  juce::String getPluginWorkerError() const { return bridge.getLastError(); }
  // Preconditions: 命令线程独占且工作进程已崩溃；暂停实时处理后释放进程和映射。
  void terminateCrashedPluginWorker();
  // Concurrency: 取消可从其他线程发起；重置仅用于受理下一项任务、启动其线程之前。
  void cancelPendingPluginOperation() { bridge.cancelPendingOperation(); }
  void resetPluginCancellation() { bridge.resetCancellation(); }
  struct DeviceConfiguration {
    double sampleRate;
    int blockSize;
    unsigned revision;
  };
  DeviceConfiguration deviceConfiguration();
  unsigned configurationRevision() const { return deviceRevision.load(); }
  // Ordering: 暂停实时处理后应用本次受理的设备配置，恢复调用前的暂停状态。
  // Failures: 返回 `false` 并记录插件诊断；失败不发布新的已配置修订号。
  bool preparePlugin(DeviceConfiguration configuration,
                     std::function<void(double)> prepareMidi = {});
  // Postconditions: 采样率单位为 Hz；修订号不一致时，实时输出等待重配置完成。
  double liveSampleRate() const { return deviceSampleRate.load(); }
  bool requiresPrepare() const {
    return hasPluginLoaded() &&
           preparedRevision.load() != deviceRevision.load();
  }
  // Concurrency: 设备回调只发布原子配置；MIDI 快照更新及阻塞 IPC 由核心调度。
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  // Preconditions: JUCE 串行调用并持有处理器回调锁；回调只消费预分配音频，不执行 IPC 或 MIDI 分配。
  // Postconditions: 欠载部分输出静音并记录计数；渲染线程继续按顺序提交 MIDI，禁止丢弃控制事件。
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
  // Ownership: 返回借用引用；只供核心编排，序列生产和消费必须遵循 `MidiPlayer` 并发契约。
  MidiPlayer &getMidiPlayer() { return midiPlayer; }
  // Postconditions: 原子保存 [0,1] 线性增益；不修改界面百分比或持久化配置。
  void setMasterVolume(float value) {
    masterVolume.store(juce::jlimit(0.0f, 1.0f, value));
  }
  float getMasterVolume() const { return masterVolume.load(); }
  uint64_t getUnderrunCount() const { return underrunCount.load(); }
  int getRenderLatencySamples() const { return renderLatencySamples.load(); }
  juce::String getLastPluginError() const {
    const juce::ScopedLock lock(errorLock);
    return lastPluginError;
  }
  // Ownership: 独占借用插件，持有独立 MIDI 游标；实时曲目始终由引擎持有。
  // Ordering: stop 实时渲染后进入离线模式，finish 恢复插件及实时渲染。
  class OfflineExportSession {
  public:
    OfflineExportSession(AudioEngine &owner,
                         std::unique_ptr<juce::MidiMessageSequence> sequence,
                         double sampleRate);
    ~OfflineExportSession();
    OfflineExportSession(const OfflineExportSession &) = delete;
    OfflineExportSession &operator=(const OfflineExportSession &) = delete;

    juce::Result preparationResult() const { return preparation; }
    juce::Result finish();
    juce::Result render(juce::AudioBuffer<float> &audio, juce::MidiBuffer &midi);
    double durationSamples() const { return player.getDurationInSamples(); }
    int latencySamples() const { return engine.bridge.getLatencySamples(); }
    float masterGain() const { return engine.getMasterVolume(); }

  private:
    AudioEngine &engine;
    MidiPlayer player;
    double rate;
    juce::Result preparation{juce::Result::ok()};
    bool active = false;
  };
  // Contract: 以下 JUCE 元数据接口描述固定的 MIDI 输入、立体声音频输出处理器。
  // 插件窗口和插件状态位于子进程，因此本处理器不提供本地编辑器或状态序列化。
  const juce::String getName() const override {
    return "ModernMidiPlayerEngine";
  }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return "None"; }
  void changeProgramName(int, const juce::String &) override {}
  bool hasEditor() const override { return false; }
  juce::AudioProcessorEditor *createEditor() override { return nullptr; }
  void getStateInformation(juce::MemoryBlock &) override {}
  void setStateInformation(const void *, int) override {}

private:
  // Concurrency: 设备回调只读音频 FIFO；本线程独占 MIDI 消费及阻塞插件 IPC。
  void run() override;
  bool startRealtimeRenderer(double rate, int devicePeriod);
  void stopRealtimeRenderer();
  void renderRealtimeBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &);
  // Ordering: 启动结果在线程注册 MMCSS 后发布，设备回调只在成功后恢复。
  juce::WaitableEvent renderThreadStarted;
  juce::Result renderThreadStartResult{juce::Result::ok()};
  RealtimeAudioBuffer realtimeAudio;
  juce::AudioBuffer<float> realtimeBlock;
  juce::MidiBuffer realtimeMidi;
  juce::SmoothedValue<float> outputGain;
  int renderQuantum = 512;
  double preparedRenderSampleRate = 44100.0;
  std::atomic<uint64_t> underrunCount{0};
  std::atomic<int> renderLatencySamples{0};
  void setLastPluginError(const juce::String &message) {
    const juce::ScopedLock lock(errorLock);
    lastPluginError = message;
  }
  // Preconditions: 专用渲染线程或独占导出中的唯一调用；不得与桥接资源释放并发。
  // Ordering: MIDI 按宿主块生成，再按共享块容量切分；任一分块失败即返回 `false`。
  bool renderPluginBlock(MidiPlayer &, MidiPlayer::ProcessMode,
                         juce::AudioBuffer<float> &, juce::MidiBuffer &,
                         double sampleRate);
  juce::Result prepareForOfflineExport(double sampleRate);
  juce::Result restoreFromOfflineExport();
  void flushPluginStateBeforeUnload();
  PluginBridge::PluginBridgeClient bridge;
  std::atomic<unsigned> deviceRevision{0};
  std::atomic<unsigned> preparedRevision{0};
  std::atomic<double> deviceSampleRate{44100.0};
  std::atomic<int> deviceBlockSize{512};
  MidiPlayer midiPlayer;

  std::atomic<float> masterVolume{0.8f};
  // Reason: 停止先淡出再释放音符；计数单位为采样，不随设备采样率重算。
  int fadeOutDuration = 2048;
  int fadeOutSamples = 2048;
  bool stopCleanupDone = false;
  int tailSamplesRendered = 0;
  int tailSilentSamples = 0;

  // Ordering: seek 清理块先静音，后续音频按剩余采样数淡入；阶段 0 为结束，2 为淡入。
  int seekCrossfadeDuration = 128;
  int seekCrossfadeSamples = 0;
  int seekCrossfadePhase = 0;

  mutable juce::CriticalSection errorLock;
  juce::String lastPluginError;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
