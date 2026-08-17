#pragma once

#include "LegacyDesignTokens.h"
#include <algorithm>
#include <cmath>

inline float getNavigationIconTargetScale(bool isHovered, bool isPressed) {
  if (isPressed)
    return LegacyDesignTokens::Motion::navigationPressedScale;
  if (isHovered)
    return LegacyDesignTokens::Motion::navigationHoverScale;
  return 1.0f;
}

inline bool advanceNavigationIconScale(float &currentScale, bool isHovered,
                                       bool isPressed) {
  const float target = getNavigationIconTargetScale(isHovered, isPressed);
  const float difference = target - currentScale;
  if (std::abs(difference) <= 0.005f) {
    currentScale = target;
    return false;
  }
  currentScale += difference * 0.25f;
  return true;
}

class SidebarAnimationController {
public:
  enum class Phase {
    idleExpanded,
    hidingLabels,
    collapsingWidth,
    idleCollapsed,
    expandingWidth,
    showingLabels
  };

  static constexpr float expandedWidth =
      static_cast<float>(LegacyDesignTokens::Layout::sidebarExpandedWidth);
  static constexpr float collapsedWidth =
      static_cast<float>(LegacyDesignTokens::Layout::sidebarCollapsedWidth);

  void reset(bool collapsed) {
    targetCollapsed = collapsed;
    width = collapsed ? collapsedWidth : expandedWidth;
    widthVelocity = 0.0f;
    labelAlpha = collapsed ? 0.0f : 1.0f;
    phase = collapsed ? Phase::idleCollapsed : Phase::idleExpanded;
  }

  void setCollapsed(bool collapsed) {
    targetCollapsed = collapsed;

    if (collapsed) {
      if (width <= collapsedWidth && labelAlpha <= 0.0f) {
        phase = Phase::idleCollapsed;
      } else if (labelAlpha > 0.0f) {
        phase = Phase::hidingLabels;
      } else {
        phase = Phase::collapsingWidth;
      }
      return;
    }

    if (width >= expandedWidth && labelAlpha >= 1.0f) {
      phase = Phase::idleExpanded;
    } else if (width < expandedWidth) {
      labelAlpha = 0.0f;
      phase = Phase::expandingWidth;
    } else {
      phase = Phase::showingLabels;
    }
  }

  bool tick() {
    switch (phase) {
    case Phase::hidingLabels:
      labelAlpha = std::max(0.0f, labelAlpha - labelStep);
      if (labelAlpha <= 0.0f)
        phase = targetCollapsed ? Phase::collapsingWidth
                                : Phase::showingLabels;
      break;

    case Phase::collapsingWidth:
      if (advanceWidthSpring(collapsedWidth)) {
        phase = targetCollapsed ? Phase::idleCollapsed
                                : Phase::expandingWidth;
      }
      break;

    case Phase::expandingWidth: {
      const bool settled = advanceWidthSpring(expandedWidth);
      updateExpandingLabelAlpha();
      if (settled) {
        labelAlpha = 1.0f;
        phase = targetCollapsed ? Phase::hidingLabels
                                : Phase::idleExpanded;
      }
      break;
    }

    case Phase::showingLabels:
      labelAlpha = std::min(1.0f, labelAlpha + labelStep);
      if (labelAlpha >= 1.0f)
        phase = targetCollapsed ? Phase::hidingLabels
                                : Phase::idleExpanded;
      break;

    case Phase::idleExpanded:
    case Phase::idleCollapsed:
      break;
    }

    return isAnimating();
  }

  float getWidth() const { return width; }
  float getLabelAlpha() const { return labelAlpha; }
  Phase getPhase() const { return phase; }

  float getCompactProgress() const {
    return std::clamp((expandedWidth - width) /
                          (expandedWidth - collapsedWidth),
                      0.0f, 1.0f);
  }

  bool isAnimating() const {
    return phase != Phase::idleExpanded && phase != Phase::idleCollapsed;
  }

private:
  bool advanceWidthSpring(float target) {
    const float difference = target - width;
    widthVelocity =
        (widthVelocity + difference * springStiffness) * springDamping;
    width += widthVelocity;

    if (std::abs(difference) <= settleDistance &&
        std::abs(widthVelocity) <= settleVelocity) {
      width = target;
      widthVelocity = 0.0f;
      return true;
    }
    return false;
  }

  void updateExpandingLabelAlpha() {
    const float expansion =
        std::clamp((width - collapsedWidth) /
                       (expandedWidth - collapsedWidth),
                   0.0f, 1.0f);
    labelAlpha =
        std::clamp((expansion - labelRevealStart) /
                       (1.0f - labelRevealStart),
                   0.0f, 1.0f);
  }

  static constexpr float labelStep = 0.22f;
  static constexpr float labelRevealStart = 0.34f;
  static constexpr float springStiffness = 0.16f;
  static constexpr float springDamping = 0.58f;
  static constexpr float settleDistance = 0.08f;
  static constexpr float settleVelocity = 0.08f;

  float width = expandedWidth;
  float widthVelocity = 0.0f;
  float labelAlpha = 1.0f;
  bool targetCollapsed = false;
  Phase phase = Phase::idleExpanded;
};
