#pragma once

#include "AudioDeviceState.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Owns device callbacks and persistence. Destroy before the processor it uses.
class AudioDeviceService final : private juce::ChangeListener,
                                 public juce::ChangeBroadcaster {
public:
  explicit AudioDeviceService(juce::AudioProcessor &processor);
  ~AudioDeviceService() override;
  AudioDeviceState state(bool rescan);
  juce::String setDriver(const juce::String &name);
  juce::String apply(const juce::AudioDeviceManager::AudioDeviceSetup &setup);
  juce::String showControlPanel();
  void playTestSound();
  bool hasDevice() const { return deviceAvailable.load(); }
  bool isFirstRun() const { return firstRun; }
  bool wasRestoredWithFallback() const { return restoredWithFallback; }
  juce::String lastError() const {
    const juce::ScopedLock lock(statusLock);
    return error;
  }

private:
  juce::String save();
  void restore();
  void publishStatus(const juce::String &diagnostic);
  void changeListenerCallback(juce::ChangeBroadcaster *) override;
  juce::AudioDeviceManager manager;
  juce::AudioProcessorPlayer player;
  std::atomic<bool> deviceAvailable{false};
  mutable juce::CriticalSection statusLock;
  juce::String error;
  bool firstRun = false;
  bool restoredWithFallback = false;
  bool recovering = false;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioDeviceService)
};
