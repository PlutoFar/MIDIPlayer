#pragma once

// Contract: 布局长度为逻辑像素，间距非负；分页位置应处于当前滚动范围。
// Preconditions: 去重输入为候选本地文件且 `maximumFiles` 为正；内容比较可能同步读取磁盘。
// Postconditions: 删除许可函数只判断归属和文件名，实际删除仍由调用方执行。

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

inline bool canRemoveRecentBackgroundCache(const juce::File &cacheDirectory,
                                           const juce::File &currentFile,
                                           const juce::File &candidate) {
  return candidate.isAChildOf(cacheDirectory) &&
         candidate.getFileName().matchesWildcard("bg_*.png", true) &&
         !candidate.getFullPathName().equalsIgnoreCase(
             currentFile.getFullPathName());
}
