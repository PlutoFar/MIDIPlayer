#pragma once

// Trust Boundary: Shell 命令行只在这里按文件存在性和扩展名筛选；MIDI 内容由核心解析。
// Postconditions: 找不到支持的文件时返回空 `File`；字符串处理不打开或执行命令。

#include <juce_core/juce_core.h>

inline bool isSupportedMidiPath(const juce::File &file) {
  if (!file.existsAsFile())
    return false;

  const auto ext = file.getFileExtension().toLowerCase();
  return ext == ".mid" || ext == ".midi";
}

inline juce::String stripMatchingCommandLineQuotes(juce::String value) {
  value = value.trim();
  if (value.length() >= 2 &&
      ((value.startsWithChar('"') && value.endsWithChar('"')) ||
       (value.startsWithChar('\'') && value.endsWithChar('\'')))) {
    return value.substring(1, value.length() - 1).trim();
  }

  return value;
}

inline juce::File parseMidiFileFromCommandLine(const juce::String &commandLine) {
  const auto trimmed = commandLine.trim();
  if (trimmed.isEmpty())
    return {};

  const juce::File wholeArgument(stripMatchingCommandLineQuotes(trimmed));
  if (isSupportedMidiPath(wholeArgument))
    return wholeArgument;

  juce::StringArray tokens;
  tokens.addTokens(trimmed, " \t\r\n", "\"'");
  tokens.trim();
  tokens.removeEmptyStrings();

  for (auto token : tokens) {
    token = stripMatchingCommandLineQuotes(token);
    if (token.startsWithChar('-') || token.startsWithChar('/'))
      continue;

    const juce::File file(token);
    if (isSupportedMidiPath(file))
      return file;
  }

  return {};
}
