#pragma once

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
