#include "LegacyBridge.h"

#include "CoreImpl.h"

namespace midi {

LegacyCoreAdapter::LegacyCoreAdapter(Core &coreRef) : core(coreRef) {}

juce::AudioDeviceManager &LegacyCoreAdapter::deviceManager() {
  return core.impl->engine.getDeviceManager();
}

MidiPlayer &LegacyCoreAdapter::midiPlayer() {
  return core.impl->engine.getMidiPlayer();
}

juce::KnownPluginList &LegacyCoreAdapter::pluginList() {
  return core.impl->engine.getPluginList();
}

PlaylistManager &LegacyCoreAdapter::playlist() { return core.impl->playlist; }

AudioEngine &LegacyCoreAdapter::engine() { return core.impl->engine; }

} // namespace midi
