#include "CoreImpl.h"

// Responsibilities: 设备变更与插件/导出的业务互斥；具体设备操作由 `AudioDeviceService` 执行。

namespace midi {

AudioDeviceState Core::audioDeviceState(bool rescan) {
  return impl->audio.state(rescan);
}

juce::String
Core::Impl::configureAudio(std::function<juce::String()> operation) {
  {
    StateLock lock(stateMutex);
    if (pluginTaskActive.load() || exportActiveFlag.load() ||
        audioConfigurationActive.load())
      return L"当前操作尚未结束，无法修改音频设备";
    audioConfigurationActive.store(true);
  }
  struct Completion {
    std::atomic<bool> &active;
    ~Completion() { active.store(false); }
  } completion{audioConfigurationActive};
  // Concurrency: 原生控制面板可能泵送消息；操作标记保持置位，但不持有 `stateMutex`。
  return operation();
}

juce::String Core::setAudioDriver(const juce::String &name) {
  return impl->configureAudio([&] { return impl->audio.setDriver(name); });
}

juce::String Core::configureAudioDevice(
    const juce::AudioDeviceManager::AudioDeviceSetup &setup) {
  return impl->configureAudio([&] { return impl->audio.apply(setup); });
}

juce::String Core::showAudioControlPanel() {
  return impl->configureAudio([&] { return impl->audio.showControlPanel(); });
}

void Core::playTestSound() {
  if (!impl->exportActiveFlag.load() && !impl->pluginTaskActive.load())
    impl->audio.playTestSound();
}

void Core::addAudioDeviceListener(juce::ChangeListener *listener) {
  impl->audio.addChangeListener(listener);
}
void Core::removeAudioDeviceListener(juce::ChangeListener *listener) {
  impl->audio.removeChangeListener(listener);
}

} // namespace midi
