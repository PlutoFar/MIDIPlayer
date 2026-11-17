#pragma once
#include "../PluginBridge/MidiPacketLimits.h"
#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>

// Responsibilities: 完整读取并验证 MIDI 文件，输出秒级序列；不修改播放或界面状态。
// Concurrency: 调用方在工作线程解析；结果成功后才发布给播放器。
inline juce::Result readMidiSequence(const juce::File &file,
                                     std::unique_ptr<juce::MidiMessageSequence> &sequence) {
  if (!file.hasFileExtension("mid;midi"))
    return juce::Result::fail(L"请选择 .mid 或 .midi 文件。");
  auto input = file.createInputStream();
  if (input == nullptr)
    return juce::Result::fail(L"无法读取 MIDI 文件: " + file.getFullPathName());
  juce::MidiFile midi;
  if (!midi.readFrom(*input))
    return juce::Result::fail(L"MIDI 文件格式无效: " + file.getFullPathName());
  midi.convertTimestampTicksToSeconds();
  auto parsed = std::make_unique<juce::MidiMessageSequence>();
  for (int i = 0; i < midi.getNumTracks(); ++i) {
    const auto *track = midi.getTrack(i);
    for (int j = 0; j < track->getNumEvents(); ++j) {
      const auto &message = track->getEventPointer(j)->message;
      if (!std::isfinite(message.getTimeStamp()) || message.getTimeStamp() < 0.0)
        return juce::Result::fail(L"MIDI 文件包含无效事件时间: " + file.getFullPathName());
      // JUCE 的 MidiBuffer 可能拒绝极长 SysEx，必须在构造播放快照之前报告文件错误。
      if (!message.isMetaEvent() &&
          message.getRawDataSize() > PluginBridge::MidiPacketLimits::maxEventBytes) {
        return juce::Result::fail(PluginBridge::midiEventTooLargeError() + L" 文件: " +
                                  file.getFullPathName() + L"，事件大小: " +
                                  juce::String(message.getRawDataSize()) + L" 字节");
      }
      parsed->addEvent(message);
    }
  }
  parsed->sort();
  parsed->updateMatchedPairs();
  sequence = std::move(parsed);
  return juce::Result::ok();
}
