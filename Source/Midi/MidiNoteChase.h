#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <vector>

// Responsibilities: 重建定位点之前仍按住或被踏板维持的音符。
// Ordering: 先恢复保持踏板捕获的音符，再恢复踏板和其余音符，最后释放已经松开的键。
// Boundary: MIDI 重触发恢复音高/力度和踏板关系，无法恢复插件内部包络或采样播放进度。
inline void appendChasedNotes(const juce::MidiMessageSequence &sequence,
                             int endIndex, juce::MidiBuffer &output) {
  struct Note { int key; juce::uint8 velocity; bool down = true, sostenuto = false; };
  struct Channel { std::vector<Note> notes; int sustain = 0, sostenuto = 0; };
  std::array<Channel, 16> channels;
  for (int i = 0; i < endIndex; ++i) {
    const auto &message = sequence.getEventPointer(i)->message;
    const int channel = message.getChannel() - 1;
    if (channel < 0 || channel >= 16)
      continue;
    auto &state = channels[(size_t)channel];
    if (message.isNoteOn()) {
      state.notes.push_back({message.getNoteNumber(), message.getVelocity()});
    } else if (message.isNoteOff()) {
      for (auto &note : state.notes)
        if (note.key == message.getNoteNumber() && note.down) {
          note.down = false;
          break;
        }
    } else if (message.isController()) {
      const int controller = message.getControllerNumber();
      const int value = message.getControllerValue();
      if (controller == 64) {
        state.sustain = value;
      } else if (controller == 66) {
        if (state.sostenuto < 64 && value >= 64)
          for (auto &note : state.notes)
            note.sostenuto = note.down;
        if (value < 64)
          for (auto &note : state.notes)
            note.sostenuto = false;
        state.sostenuto = value;
      } else if (controller == 120) {
        state.notes.clear();
      } else if (controller == 121) {
        state.sustain = state.sostenuto = 0;
        for (auto &note : state.notes)
          note.sostenuto = false;
      } else if (controller == 123) {
        for (auto &note : state.notes)
          note.down = false;
      }
    }
    state.notes.erase(std::remove_if(state.notes.begin(), state.notes.end(),
        [&](const Note &note) { return !note.down && !note.sostenuto && state.sustain < 64; }),
        state.notes.end());
  }
  for (int ch = 0; ch < 16; ++ch) {
    const auto &state = channels[(size_t)ch];
    const auto emitNotes = [&](bool held) {
      for (const auto &note : state.notes)
        if (note.sostenuto == held)
          output.addEvent(juce::MidiMessage::noteOn(ch + 1, note.key, note.velocity), 0);
    };
    emitNotes(true);
    output.addEvent(juce::MidiMessage::controllerEvent(ch + 1, 66, state.sostenuto), 0);
    for (const auto &note : state.notes)
      if (note.sostenuto && !note.down)
        output.addEvent(juce::MidiMessage::noteOff(ch + 1, note.key), 0);
    emitNotes(false);
    output.addEvent(juce::MidiMessage::controllerEvent(ch + 1, 64, state.sustain), 0);
    for (const auto &note : state.notes)
      if (!note.sostenuto && !note.down)
        output.addEvent(juce::MidiMessage::noteOff(ch + 1, note.key), 0);
  }
}
