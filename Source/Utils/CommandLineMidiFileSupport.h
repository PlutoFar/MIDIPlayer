#pragma once

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
