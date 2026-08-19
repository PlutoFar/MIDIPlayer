#pragma once

#include <juce_core/juce_core.h>

inline juce::String formatAudioSampleRate(double sampleRate) {
  if (sampleRate <= 0.0)
    return "-";

  if (sampleRate >= 1000.0)
    return juce::String(sampleRate / 1000.0, 1) + " kHz";

  return juce::String((int)std::round(sampleRate)) + " Hz";
}

inline juce::String formatAudioBufferSize(int samples, double sampleRate) {
  if (samples <= 0)
    return "-";

  juce::String text = juce::String(samples) + " samples";
  if (sampleRate > 0.0)
    text += L" \u00B7 " +
            juce::String(samples * 1000.0 / sampleRate, 1) + " ms";
  return text;
}

inline juce::BigInteger makeStereoOutputMask(int channelCount,
                                             int firstChannel) {
  juce::BigInteger channels;
  if (channelCount <= 0)
    return channels;

  if (channelCount == 1) {
    channels.setBit(0);
    return channels;
  }

  const int first = juce::jlimit(0, channelCount - 2, firstChannel);
  channels.setBit(first);
  channels.setBit(first + 1);
  return channels;
}
