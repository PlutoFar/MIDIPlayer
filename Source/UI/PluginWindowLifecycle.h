#pragma once

#include <juce_core/juce_core.h>

inline int getPluginEditorOpenDelayMs(const juce::String &) { return 300; }

inline bool isIvoryPluginName(const juce::String &pluginName) {
  return pluginName.containsIgnoreCase("Ivory");
}

inline bool shouldAutoOpenPluginEditorAfterLoad(const juce::String &pluginName) {
  return !isIvoryPluginName(pluginName);
}

inline bool shouldShowPluginSwitchPausedNotice(bool wasPlayingBeforeSwitch,
                                               bool hadPendingResume) {
  return wasPlayingBeforeSwitch || hadPendingResume;
}

inline juce::String makeLoadedPluginLabel(const juce::String &pluginName,
                                          bool playbackPausedBySwitch) {
  juce::String label = L"已加载: ";
  label += pluginName;
  if (playbackPausedBySwitch)
    label += L"（播放已暂停，请手动恢复）";
  return label;
}

class PluginWindowLifecycle {
public:
  struct Request {
    int generation = 0;
    const void *processor = nullptr;
  };

  int beginSwitch() {
    ++currentGeneration;
    closeWindow();
    return currentGeneration;
  }

  int getGeneration() const { return currentGeneration; }

  bool isGenerationCurrent(int generation) const {
    return generation == currentGeneration;
  }

  Request makeRequest(const void *processor) const {
    return {currentGeneration, processor};
  }

  bool isCurrent(Request request, const void *processor) const {
    return request.generation == currentGeneration &&
           request.processor != nullptr && request.processor == processor;
  }

  void markWindowOpened(Request request) {
    windowGeneration = request.generation;
    windowProcessor = request.processor;
  }

  bool windowMatchesCurrent(const void *processor) const {
    return windowGeneration == currentGeneration &&
           windowProcessor != nullptr && windowProcessor == processor;
  }

  void closeWindow() {
    windowGeneration = 0;
    windowProcessor = nullptr;
  }

private:
  int currentGeneration = 0;
  int windowGeneration = 0;
  const void *windowProcessor = nullptr;
};
