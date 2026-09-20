#pragma once

#include "../Midi/MidiPlayer.h"
#include "../PluginBridge/PluginBridgeClient.h"
#include "ExportSettings.h"
#include <atomic>
#include <juce_audio_processors/juce_audio_processors.h>

// Realtime MIDI/audio processing and plugin hosting. Device ownership, catalog
// persistence and file encoding belong to separate services.
class AudioEngine final : public juce::AudioProcessor {
public:
  AudioEngine();
  ~AudioEngine() override;
  bool loadPlugin(const juce::PluginDescription &description);
  void unloadPlugin();
  bool openPluginEditor();
  void closePluginEditor() { bridge.closeEditor(); }
  bool hasPluginLoaded() const { return bridge.isPluginLoaded(); }
  juce::String getLoadedPluginName() const {
    return bridge.getLoadedPluginName();
  }
  bool hasPluginWorkerCrashed() const {
    return bridge.getStatus() == PluginBridge::BridgeStatus::crashed;
  }
  juce::String getPluginWorkerError() const { return bridge.getLastError(); }
  void terminateCrashedPluginWorker();
  void cancelPendingPluginOperation() { bridge.cancelPendingOperation(); }
  void resetPluginCancellation() { bridge.resetCancellation(); }
  bool preparePlugin();
  double liveSampleRate() const { return deviceSampleRate.load(); }
  bool requiresPrepare() const {
    return hasPluginLoaded() &&
           preparedRevision.load() != deviceRevision.load();
  }
  void prepareToPlay(double sampleRate, int samplesPerBlock) override;
  void releaseResources() override {}
  void processBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &) override;
  MidiPlayer &getMidiPlayer() { return midiPlayer; }
  void setMasterVolume(float value) {
    masterVolume.store(juce::jlimit(0.0f, 1.0f, value));
  }
  float getMasterVolume() const { return masterVolume.load(); }
  juce::String getLastPluginError() const {
    const juce::ScopedLock lock(errorLock);
    return lastPluginError;
  }
  juce::String getLastExportError() const { return lastExportError; }
  bool wasLastExportCancelled() const { return lastExportCancelled; }
  bool prepareForOfflineExport(const ExportSettings &settings);
  bool restoreFromOfflineExport();
  bool isOfflineExportActive() const {
    return offlineExportActive.load(std::memory_order_acquire);
  }
  class OfflineExportSession {
  public:
    OfflineExportSession(AudioEngine &owner, const ExportSettings &settings)
        : engine(owner) {
      active = engine.prepareForOfflineExport(settings);
    }

    ~OfflineExportSession() { finish(); }

    bool finish() {
      if (!active)
        return true;
      active = false;
      return engine.restoreFromOfflineExport();
    }

    OfflineExportSession(const OfflineExportSession &) = delete;
    OfflineExportSession &operator=(const OfflineExportSession &) = delete;

    bool isActive() const { return active; }

  private:
    AudioEngine &engine;
    bool active = false;
  };
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
  friend class OfflineRenderer;
  void setLastPluginError(const juce::String &message) {
    const juce::ScopedLock lock(errorLock);
    lastPluginError = message;
  }
  bool renderPluginBlock(juce::AudioBuffer<float> &, juce::MidiBuffer &,
                         double sampleRate);
  void flushPluginStateBeforeUnload();
  PluginBridge::PluginBridgeClient bridge;
  std::atomic<unsigned> deviceRevision{0};
  std::atomic<unsigned> preparedRevision{0};
  std::atomic<double> deviceSampleRate{44100.0};
  std::atomic<int> deviceBlockSize{512};
  MidiPlayer midiPlayer;

  std::atomic<float> masterVolume{0.8f};
  std::atomic<bool> offlineExportActive{false};
  // 停止播放时的音频淡出，用于避免爆音（44.1 kHz 下约 50 ms）
  int fadeOutDuration = 2048;
  int fadeOutSamples = 2048;
  bool stopCleanupDone = false;
  int tailSamplesRendered = 0;
  int tailSilentSamples = 0;

  // seek/切曲交叉淡入：静音包含 allSoundOff 瞬态的 buffer，
  // 下一块干净 buffer 执行淡入。阶段值：0=空闲，2=淡入。
  int seekCrossfadeDuration = 128;
  int seekCrossfadeSamples = 0;
  int seekCrossfadePhase = 0;

  mutable juce::CriticalSection errorLock;
  juce::String lastPluginError;
  juce::String lastExportError;
  bool lastExportCancelled = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
