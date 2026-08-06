#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

inline constexpr int exportOfflineBlockSize = 1024;

inline bool shouldPreserveExportHeadroom(const juce::String &formatName,
                                         int bitDepth,
                                         bool useFloatingPoint) {
  return formatName.equalsIgnoreCase("WAV") && bitDepth == 32 &&
         useFloatingPoint;
}

inline bool shouldDitherExport(const juce::String &formatName, int bitDepth,
                               bool useFloatingPoint) {
  const bool isLosslessInteger =
      formatName.equalsIgnoreCase("WAV") ||
      formatName.equalsIgnoreCase("FLAC");
  return isLosslessInteger && !useFloatingPoint && bitDepth > 0 &&
         bitDepth < 32;
}

inline void applyMasterOutputStage(juce::AudioBuffer<float> &buffer,
                                   float masterGain,
                                   bool preserveHeadroom) {
  buffer.applyGain(masterGain);
  if (preserveHeadroom)
    return;

  for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
    auto *samples = buffer.getWritePointer(channel);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample)
      samples[sample] = std::tanh(samples[sample]);
  }
}

inline void applyTpdfDither(juce::AudioBuffer<float> &buffer, int bitDepth,
                            juce::Random &random) {
  if (bitDepth <= 0 || bitDepth >= 32)
    return;

  const float quantisationStep =
      1.0f / static_cast<float>(uint32_t{1} << (bitDepth - 1));
  for (int channel = 0; channel < buffer.getNumChannels(); ++channel) {
    auto *samples = buffer.getWritePointer(channel);
    for (int sample = 0; sample < buffer.getNumSamples(); ++sample) {
      const float triangularNoise =
          (random.nextFloat() - random.nextFloat()) * quantisationStep;
      samples[sample] += triangularNoise;
    }
  }
}

inline float measureExportTailLevel(const juce::AudioBuffer<float> &buffer) {
  float maxLevel = 0.0f;
  for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
    maxLevel = juce::jmax(
        maxLevel,
        buffer.getMagnitude(channel, 0, buffer.getNumSamples()));
  return maxLevel;
}
