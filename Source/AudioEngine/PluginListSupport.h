#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

inline int
removeMissingPluginTypes(juce::KnownPluginList &pluginList,
                         const juce::AudioPluginFormatManager &formatManager) {
  auto types = pluginList.getTypes();
  int removed = 0;

  for (int i = types.size(); --i >= 0;) {
    const auto type = types.getUnchecked(i);
    if (!formatManager.doesPluginStillExist(type)) {
      pluginList.removeType(type);
      ++removed;
    }
  }

  return removed;
}
