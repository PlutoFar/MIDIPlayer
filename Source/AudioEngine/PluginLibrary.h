#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Plugin discovery and cache persistence; Core serialises access to the
// catalog.
class PluginLibrary {
public:
  PluginLibrary();
  void copyPluginListTo(juce::KnownPluginList &destination) const;
  bool scanPlugins(juce::KnownPluginList &destination,
                   const std::function<bool()> &shouldCancel);
  bool replacePluginList(const juce::KnownPluginList &source);
  const juce::KnownPluginList &plugins() const { return pluginList; }
  juce::String lastError() const {
    const juce::ScopedLock lock(errorLock);
    return error;
  }

private:
  void loadKnownPluginList();
  static void quarantineCorruptPluginCache(const juce::File &file);
  void setError(const juce::String &value) {
    const juce::ScopedLock lock(errorLock);
    error = value;
  }
  juce::KnownPluginList pluginList;
  mutable juce::CriticalSection errorLock;
  juce::String error;
};
