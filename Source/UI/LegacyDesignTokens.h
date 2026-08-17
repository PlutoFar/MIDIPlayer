#pragma once

#include <algorithm>
#include <cmath>

namespace LegacyDesignTokens {

namespace Typography {
constexpr float referenceBody = 14.0f;
constexpr float defaultLegacyBody = 16.0f;
constexpr float minimumBody = 14.0f;
constexpr float maximumBody = 22.0f;
constexpr float minimumResolvedSize = 12.0f;

constexpr float caption = 12.0f;
constexpr float body = 14.0f;
constexpr float bodyLarge = 18.0f;
constexpr float navigation = 18.0f;
constexpr float subtitle = 20.0f;
constexpr float title = 28.0f;

inline float resolve(float semanticSize, float configuredBodySize) {
  return std::max(minimumResolvedSize,
                  semanticSize + configuredBodySize - referenceBody);
}

inline int lineHeight(float semanticSize, float configuredBodySize) {
  return static_cast<int>(
      std::ceil(resolve(semanticSize, configuredBodySize) * 1.4f));
}
} // namespace Typography

namespace Icon {
constexpr float overlay = 12.0f;
constexpr float small = 16.0f;
constexpr float paneToggle = 20.0f;
constexpr float primaryAction = 24.0f;
constexpr float toolbar = primaryAction;
constexpr float transport = primaryAction;
constexpr float navigation = primaryAction;
constexpr float primary = primaryAction;
} // namespace Icon

namespace Motion {
constexpr float navigationPressedScale = 0.85f;
constexpr float navigationHoverScale = 1.15f;
constexpr float playbackModeInitialScale = 0.8f;
} // namespace Motion

namespace Layout {
// 所有尺寸均为逻辑像素；JUCE peer 负责应用当前显示器的 DPI 缩放。
constexpr int sidebarExpandedWidth = 180;
constexpr int sidebarCollapsedWidth = 64;
constexpr int navigationIconSlot = 44;
constexpr int navigationPaneToggleButtonSize = 40;
constexpr int navigationMinimumItemHeight = 52;
constexpr int navigationCollapseHeight = 48;
constexpr int navigationTopPadding = 8;
constexpr int navigationSidePadding = 8;

constexpr int minimumControlHeight = 40;
constexpr int toolbarButtonSize = 40;
constexpr int transportButtonSize = 40;
constexpr int transportPrimaryButtonSize = 48;
constexpr int transportMinimumHeight = 92;
constexpr int transportHorizontalPadding = 20;
constexpr int transportVerticalPadding = 8;
constexpr int transportProgressHeight = 24;
constexpr int transportProgressGap = 4;
constexpr int transportVolumeAreaWidth = 200;
constexpr int transportMinimumTrackWidth = 220;
constexpr int transportCentrePadding = 40;
constexpr int contentHorizontalPadding = 24;
constexpr int contentVerticalPadding = 16;
constexpr int pageHeaderMinimumHeight = 56;
constexpr int pageTitleMinimumWidth = 160;
constexpr int pageTitleWidth = 240;
constexpr int pluginSelectorMinimumWidth = 160;
constexpr int pluginSelectorWidth = 220;
constexpr int controlGap = 8;

inline int navigationItemHeight(float configuredBodySize) {
  return std::max(
      navigationMinimumItemHeight,
      Typography::lineHeight(Typography::navigation, configuredBodySize) + 20);
}

inline int controlHeight(float configuredBodySize) {
  return std::max(
      minimumControlHeight,
      Typography::lineHeight(Typography::body, configuredBodySize) + 12);
}

inline int pageHeaderHeight(float configuredBodySize) {
  return std::max(
      pageHeaderMinimumHeight,
      Typography::lineHeight(Typography::title, configuredBodySize) + 8);
}

inline int transportHeight(float configuredBodySize) {
  const int textHeight =
      Typography::lineHeight(Typography::body, configuredBodySize) * 2;
  const int controlRowHeight =
      std::max(transportPrimaryButtonSize, textHeight);
  return std::max(
      transportMinimumHeight,
      transportVerticalPadding * 2 + transportProgressHeight +
          transportProgressGap + controlRowHeight);
}
} // namespace Layout

} // namespace LegacyDesignTokens
