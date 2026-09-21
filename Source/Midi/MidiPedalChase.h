#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <algorithm>
#include <vector>

// Responsibilities: 恢复定位点前实际出现的延音、保持和柔音踏板值；旧音符不重新触发。
// Preconditions: 旧声部和控制器已复位；CC121 之前的踏板值失效。
inline void appendChasedPedals(const juce::MidiMessageSequence &sequence,
                              int endIndex, juce::MidiBuffer &output) {
  std::array<std::array<int, 3>, 16> lastEvents;
  for (auto &channel : lastEvents)
    channel.fill(-1);
  for (int i = 0; i < endIndex; ++i) {
    const auto &message = sequence.getEventPointer(i)->message;
    const int channel = message.getChannel() - 1;
    if (channel < 0 || channel >= 16)
      continue;
    auto &events = lastEvents[(size_t)channel];
    if (message.isControllerOfType(64))
      events[0] = i;
    else if (message.isControllerOfType(66))
      events[1] = i;
    else if (message.isControllerOfType(67))
      events[2] = i;
    else if (message.isControllerOfType(121))
      events.fill(-1);
  }
  // Ordering: 保持原文件跨通道的先后顺序，避免全通道钢琴音源收到虚构的踏板释放。
  std::vector<int> ordered;
  for (const auto &channel : lastEvents)
    for (const int index : channel)
      if (index >= 0)
        ordered.push_back(index);
  std::sort(ordered.begin(), ordered.end());
  for (const int index : ordered)
    output.addEvent(sequence.getEventPointer(index)->message, 0);
}
