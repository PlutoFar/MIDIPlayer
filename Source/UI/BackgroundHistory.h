#pragma once

#include <juce_graphics/juce_graphics.h>

// Owns the managed PNG directory. The dialog owns the active selection.
class BackgroundHistory {
public:
  explicit BackgroundHistory(juce::File directory);
  juce::Array<juce::File> recentImages() const;
  juce::Result store(const juce::Image &image, juce::File &storedFile) const;
  juce::Result remove(const juce::File &file, const juce::File &activeFile) const;

private:
  juce::Array<juce::File> sortedFiles() const;
  juce::File directory;
};
