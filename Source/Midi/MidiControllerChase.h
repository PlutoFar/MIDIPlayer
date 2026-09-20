#pragma once
#include <juce_audio_basics/juce_audio_basics.h>

// Responsibilities: 恢复控制器、参数选择、音色和弯音；复用 JUCE 的 RPN/NRPN 消息顺序。
// Ordering: 音色切换和 CC121 将历史分段，防止旧控制值在复位后重新生效。
inline void appendChasedControllers(const juce::MidiMessageSequence &sequence,
                                   int endIndex, juce::MidiBuffer &output) {
  for (int channel = 1; channel <= 16; ++channel) {
    juce::MidiMessageSequence epoch;
    auto flush = [&] {
      juce::Array<juce::MidiMessage> updates;
      epoch.createControllerUpdatesForTime(channel, epoch.getEndTime(), updates);
      // JUCE 仅在两个 bank 字节齐全时生成 bank；独立字节也必须保留。
      int bankMsb = -1, bankLsb = -1;
      for (int i = 0; i < epoch.getNumEvents(); ++i) {
        const auto &message = epoch.getEventPointer(i)->message;
        if (message.isControllerOfType(0)) bankMsb = message.getControllerValue();
        if (message.isControllerOfType(32)) bankLsb = message.getControllerValue();
      }
      for (const auto &message : updates) {
        if (message.isController()) {
          const int cc = message.getControllerNumber();
          if (cc == 0 || cc == 32 || cc == 64 || cc == 66 || cc >= 120)
            continue;
        }
        output.addEvent(message, 0);
      }
      if (bankMsb >= 0)
        output.addEvent(juce::MidiMessage::controllerEvent(channel, 0, bankMsb), 0);
      if (bankLsb >= 0)
        output.addEvent(juce::MidiMessage::controllerEvent(channel, 32, bankLsb), 0);
      epoch.clear();
    };
    for (int i = 0; i < endIndex; ++i) {
      const auto &message = sequence.getEventPointer(i)->message;
      if (!message.isForChannel(channel))
        continue;
      if (message.isProgramChange() || message.isControllerOfType(121)) {
        flush();
        output.addEvent(message, 0);
      } else if (message.isController() || message.isPitchWheel()) {
        epoch.addEvent(message);
      }
    }
    flush();
  }
}
