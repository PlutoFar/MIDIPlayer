#pragma once

#include <juce_audio_formats/juce_audio_formats.h>

struct ExportFormatCapabilities {
  bool available = false;
  juce::Array<int> sampleRates;
  juce::Array<int> bitDepths;
  juce::StringArray qualityOptions;
};

inline bool exportFormatNameMatches(const juce::String &requested,
                                    const juce::String &available) {
  if (requested.equalsIgnoreCase("WAV"))
    return available.containsIgnoreCase("WAV");
  if (requested.equalsIgnoreCase("FLAC"))
    return available.containsIgnoreCase("FLAC");
  if (requested.containsIgnoreCase("Ogg"))
    return available.containsIgnoreCase("Ogg") ||
           available.containsIgnoreCase("Vorbis");
  return false;
}

inline juce::AudioFormat *
findExportAudioFormat(juce::AudioFormatManager &manager,
                      const juce::String &formatName) {
  for (int i = 0; i < manager.getNumKnownFormats(); ++i) {
    auto *format = manager.getKnownFormat(i);
    if (format != nullptr &&
        exportFormatNameMatches(formatName, format->getFormatName()))
      return format;
  }
  return nullptr;
}

inline ExportFormatCapabilities
getExportFormatCapabilities(const juce::String &formatName) {
  juce::AudioFormatManager manager;
  manager.registerBasicFormats();

  ExportFormatCapabilities result;
  if (auto *format = findExportAudioFormat(manager, formatName)) {
    result.available = true;
    result.sampleRates = format->getPossibleSampleRates();
    result.bitDepths = format->getPossibleBitDepths();
    result.qualityOptions = format->getQualityOptions();
  }
  return result;
}

inline juce::Result validateExportFormatSettings(
    const juce::String &formatName, double sampleRate, int bitDepth,
    bool useFloatingPoint, int qualityIndex) {
  const auto capabilities = getExportFormatCapabilities(formatName);
  if (!capabilities.available)
    return juce::Result::fail(L"当前 JUCE 构建不支持导出格式: " +
                              formatName);

  const int roundedSampleRate = juce::roundToInt(sampleRate);
  if (sampleRate <= 0.0 ||
      (!capabilities.sampleRates.isEmpty() &&
       !capabilities.sampleRates.contains(roundedSampleRate))) {
    return juce::Result::fail(formatName + L" 不支持 " +
                              juce::String(roundedSampleRate) +
                              L" Hz 采样率。");
  }

  if (!capabilities.bitDepths.isEmpty() &&
      !capabilities.bitDepths.contains(bitDepth)) {
    return juce::Result::fail(formatName + L" 不支持 " +
                              juce::String(bitDepth) + L"-bit 导出。");
  }

  const bool wantsOgg = formatName.containsIgnoreCase("Ogg");
  const bool wantsWav = formatName.equalsIgnoreCase("WAV");
  if (wantsOgg && (bitDepth != 32 || !useFloatingPoint))
    return juce::Result::fail(
        L"Ogg Vorbis 编码器需要 32-bit 浮点输入。");

  if (wantsWav && bitDepth == 32 && !useFloatingPoint)
    return juce::Result::fail(L"32-bit WAV 必须使用浮点采样格式。");

  if (useFloatingPoint && bitDepth != 32)
    return juce::Result::fail(L"浮点采样格式仅支持 32-bit。");

  if (!capabilities.qualityOptions.isEmpty() &&
      !juce::isPositiveAndBelow(qualityIndex,
                                capabilities.qualityOptions.size())) {
    return juce::Result::fail(formatName + L" 的质量/压缩等级无效。");
  }

  return juce::Result::ok();
}

inline juce::AudioFormatWriterOptions createExportWriterOptions(
    double sampleRate, int bitDepth, bool useFloatingPoint, int qualityIndex,
    const juce::String &title) {
  auto options = juce::AudioFormatWriterOptions()
                     .withSampleRate(sampleRate)
                     .withNumChannels(2)
                     .withBitsPerSample(bitDepth)
                     .withQualityOptionIndex(qualityIndex)
                     .withSampleFormat(
                         useFloatingPoint
                             ? juce::AudioFormatWriterOptions::SampleFormat::
                                   floatingPoint
                             : juce::AudioFormatWriterOptions::SampleFormat::
                                   integral)
                     .withMetadata("software", "MIDI Player");

  if (title.isNotEmpty())
    options = options.withMetadata("title", title);

  return options;
}
