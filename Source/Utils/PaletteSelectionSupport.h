#pragma once

#include <algorithm>
#include <vector>

#include <juce_graphics/juce_graphics.h>

namespace midi {

inline juce::Colour
selectPaletteAccent(const std::vector<juce::Colour> &palette,
                    juce::Colour savedAccent) {
  if (palette.empty())
    return savedAccent;

  const auto saved = std::find(palette.begin(), palette.end(), savedAccent);
  return saved != palette.end() ? *saved : palette.front();
}

} // namespace midi
