#pragma once

// Responsibilities: 当前 JUCE 构建的编码能力查询、参数校验和 writer 选项构造。
// Trust Boundary: `validateExportFormatSettings` 检查实际编码能力；界面预设不能替代该校验。

#include <juce_audio_formats/juce_audio_formats.h>
#include <cmath>
#include <limits>

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

// Ownership: 返回格式管理器持有的借用指针；管理器销毁后失效，不支持时返回 nullptr。
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

// Postconditions: 返回自有能力列表，编码器不存在时 `available=false`。
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

// Trust Boundary: 采样率必须为有限正整数 Hz，质量索引从 0 起；先验证数值再转换整数。
// Failures: 无效数值或不支持的编码组合返回带诊断的失败结果，不替换请求参数。
inline juce::Result validateExportFormatSettings(
    const juce::String &formatName, double sampleRate, int bitDepth,
    bool useFloatingPoint, int qualityIndex) {
  if (!std::isfinite(sampleRate) || sampleRate <= 0.0 ||
      sampleRate > static_cast<double>(std::numeric_limits<int>::max()) ||
      std::floor(sampleRate) != sampleRate)
    return juce::Result::fail(L"采样率必须为有效的正整数 Hz。");
  if (qualityIndex < 0)
    return juce::Result::fail(L"质量/压缩等级无效。");

  const auto capabilities = getExportFormatCapabilities(formatName);
  if (!capabilities.available)
    return juce::Result::fail(L"当前 JUCE 构建不支持导出格式: " +
                              formatName);

  const int roundedSampleRate = static_cast<int>(sampleRate);
  if (!capabilities.sampleRates.isEmpty() &&
      !capabilities.sampleRates.contains(roundedSampleRate)) {
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

  if (capabilities.qualityOptions.isEmpty() ? qualityIndex != 0
      : !juce::isPositiveAndBelow(qualityIndex,
                                  capabilities.qualityOptions.size())) {
    return juce::Result::fail(formatName + L" 的质量/压缩等级无效。");
  }

  return juce::Result::ok();
}

// Preconditions: 参数已通过格式校验；仅构造双通道 writer 选项，不打开文件。
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
