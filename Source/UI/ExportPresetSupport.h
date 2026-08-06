#pragma once

#include <juce_core/juce_core.h>

struct ExportPresetSpec {
  int id = 4;
  juce::String displayName;
  juce::String formatName;
  double sampleRate = 0.0;
  int bitDepth = 0;
  bool useFloatingPoint = false;
  int qualityIndex = 0;
  bool autoTail = true;
};

inline ExportPresetSpec getExportPreset(int presetId) {
  switch (presetId) {
  case 1:
    return {1, L"CD 标准发售级 (WAV / 44.1kHz / 16-bit)", "WAV",
            44100.0, 16, false, 0, true};
  case 2:
    return {2, L"高解析度音频 (FLAC / 96kHz / 24-bit / 压缩等级 5)",
            "FLAC", 96000.0, 24, false, 5, true};
  case 3:
    return {3, L"专业母带级 (WAV / 192kHz / 32-bit Float)", "WAV",
            192000.0, 32, true, 0, true};
  default:
    return {};
  }
}

inline int exportQualityComboIdFromIndex(int qualityIndex) {
  return juce::jmax(0, qualityIndex) + 1;
}

inline int exportQualityIndexFromComboId(int comboId) {
  return juce::jmax(0, comboId - 1);
}

inline int findMatchingExportPreset(const juce::String &formatName,
                                    double sampleRate, int bitDepth,
                                    bool useFloatingPoint, int qualityIndex,
                                    bool autoTail = true) {
  for (int presetId = 1; presetId <= 3; ++presetId) {
    const auto preset = getExportPreset(presetId);
    if (preset.formatName.equalsIgnoreCase(formatName) &&
        juce::approximatelyEqual(preset.sampleRate, sampleRate) &&
        preset.bitDepth == bitDepth &&
        preset.useFloatingPoint == useFloatingPoint &&
        preset.qualityIndex == qualityIndex &&
        preset.autoTail == autoTail) {
      return presetId;
    }
  }

  return 4;
}
