#pragma once

#include <juce_core/juce_core.h>

struct BackgroundHistoryLayout {
  int viewportWidth = 0;
  int tileWidth = 0;
  int totalWidth = 0;
  int maxScrollX = 0;

  bool canScrollLeft(int position) const { return position > 0; }

  bool canScrollRight(int position) const { return position < maxScrollX; }

  int getPreviousPagePosition(int position) const {
    return juce::jmax(0, position - viewportWidth);
  }

  int getNextPagePosition(int position) const {
    return juce::jmin(maxScrollX, position + viewportWidth);
  }
};

inline BackgroundHistoryLayout
makeBackgroundHistoryLayout(int itemCount, int viewportWidth, int gap) {
  BackgroundHistoryLayout result;
  result.viewportWidth = juce::jmax(1, viewportWidth);

  if (itemCount <= 0) {
    result.tileWidth = result.viewportWidth;
    result.totalWidth = result.viewportWidth;
    return result;
  }

  result.tileWidth =
      itemCount == 1
          ? result.viewportWidth
          : juce::jmax(1, (result.viewportWidth - gap) / 2);
  result.totalWidth =
      itemCount * result.tileWidth + juce::jmax(0, itemCount - 1) * gap;
  result.maxScrollX =
      juce::jmax(0, result.totalWidth - result.viewportWidth);
  return result;
}

inline juce::Array<juce::File>
getUniqueRecentBackgroundFiles(const juce::Array<juce::File> &files,
                               int maximumFiles) {
  juce::Array<juce::File> uniqueFiles;

  for (const auto &candidate : files) {
    bool isDuplicate = false;
    for (const auto &existing : uniqueFiles) {
      if (candidate.getSize() == existing.getSize() &&
          candidate.hasIdenticalContentTo(existing)) {
        isDuplicate = true;
        break;
      }
    }

    if (!isDuplicate)
      uniqueFiles.add(candidate);
    if (uniqueFiles.size() >= maximumFiles)
      break;
  }

  return uniqueFiles;
}
