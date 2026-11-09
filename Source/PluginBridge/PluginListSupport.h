#pragma once

// Contract: 调用方独占插件目录，格式管理器已注册相关格式。
// Postconditions: 删除格式管理器判定已不存在的条目并返回数量；不写缓存文件。

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
