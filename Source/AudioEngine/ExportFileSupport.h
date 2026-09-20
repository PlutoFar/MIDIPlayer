#pragma once

// Contract: 只规范化扩展名和判断覆盖提示；不创建、覆盖或锁定文件。
// Preconditions: 格式由导出界面选择；未识别名称映射为 .wav，不能据此认定编码器支持该名称。

#include <juce_core/juce_core.h>

inline juce::String getExportFileExtension(const juce::String &formatName) {
  if (formatName.equalsIgnoreCase("FLAC"))
    return ".flac";
  if (formatName.containsIgnoreCase("Ogg"))
    return ".ogg";
  return ".wav";
}

inline juce::File normaliseExportTargetFile(const juce::File &file,
                                            const juce::String &formatName) {
  const auto extension = getExportFileExtension(formatName);
  if (file.getFileExtension().equalsIgnoreCase(extension))
    return file;
  return file.withFileExtension(extension);
}

inline bool exportTargetNeedsOverwriteConfirmation(const juce::File &file) {
  return file.existsAsFile();
}
