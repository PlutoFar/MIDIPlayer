#pragma once

#include <algorithm>
#include <vector>

#include <juce_graphics/juce_graphics.h>

namespace midi {

// Postconditions: 优先保留调色板中精确匹配的保存色，其次选择首色；空调色板保留保存色。
inline juce::Colour
selectPaletteAccent(const std::vector<juce::Colour> &palette,
                    juce::Colour savedAccent) {
  if (palette.empty())
    return savedAccent;

  const auto saved = std::find(palette.begin(), palette.end(), savedAccent);
  return saved != palette.end() ? *saved : palette.front();
}

} // namespace midi
