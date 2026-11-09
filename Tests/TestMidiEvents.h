#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace miditest {

inline int findControllerSamplePosition(const juce::MidiBuffer &buffer,
                                        int channel, int controller, int value) {
  for (const auto event : buffer) {
    const auto message = event.getMessage();
    if (message.isController() && message.getChannel() == channel &&
        message.getControllerNumber() == controller &&
        message.getControllerValue() == value)
      return event.samplePosition;
  }
  return -1;
}

inline int findNoteSamplePosition(const juce::MidiBuffer &buffer, bool noteOn,
                                  int channel, int note) {
  for (const auto event : buffer) {
    const auto message = event.getMessage();
    const bool matchesType = noteOn ? message.isNoteOn() : message.isNoteOff();
    if (matchesType && message.getChannel() == channel &&
        message.getNoteNumber() == note)
      return event.samplePosition;
  }
  return -1;
}

} // namespace miditest
