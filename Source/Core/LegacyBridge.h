#pragma once

#include "Core.h"

namespace juce {
class AudioDeviceManager;
class KnownPluginList;
} // namespace juce

class AudioEngine;
class MidiPlayer;
class PlaylistManager;

namespace midi {

class LegacyCoreAdapter {
public:
  explicit LegacyCoreAdapter(Core &coreRef);

  juce::AudioDeviceManager &deviceManager();
  MidiPlayer &midiPlayer();
  juce::KnownPluginList &pluginList();
  PlaylistManager &playlist();
  AudioEngine &engine();

private:
  Core &core;
};

} // namespace midi
