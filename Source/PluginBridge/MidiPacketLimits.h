#pragma once

#include <juce_core/juce_core.h>

namespace PluginBridge {

struct MidiPacketLimits {
  static constexpr int maxPacketBytes = 8192;
  static constexpr int eventHeaderBytes = sizeof(int) * 2;
  static constexpr int maxEventBytes = maxPacketBytes - eventHeaderBytes;
};

inline juce::String midiEventTooLargeError() {
  return juce::String(L"单条 MIDI 事件超过插件传输上限（") +
         juce::String(MidiPacketLimits::maxEventBytes) + L" 字节）。";
}

} // namespace PluginBridge
