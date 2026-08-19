#pragma once

#include <cmath>
#include <cstdint>
#include <limits>
#include <juce_gui_basics/juce_gui_basics.h>

namespace TooltipPlacement {

inline juce::Component *findAnchorTarget(juce::Component *component,
                                         juce::Component *boundary) {
  if (component == nullptr || boundary == nullptr ||
      (component != boundary && !boundary->isParentOf(component)))
    return nullptr;

  juce::Component *matchedTarget = nullptr;
  for (auto *candidate = component;
       candidate != nullptr && candidate != boundary;
       candidate = candidate->getParentComponent()) {
    if (auto *client = dynamic_cast<juce::TooltipClient *>(candidate))
      if (client->getTooltip().isNotEmpty())
        matchedTarget = candidate;
  }
  return matchedTarget;
}

inline int intersectionArea(const juce::Rectangle<int> &a,
                            const juce::Rectangle<int> &b) {
  const auto intersection = a.getIntersection(b);
  return intersection.isEmpty()
             ? 0
             : intersection.getWidth() * intersection.getHeight();
}

inline int anchorGap(const juce::Rectangle<int> &tooltip,
                     const juce::Rectangle<int> &anchor) {
  if (tooltip.getBottom() <= anchor.getY())
    return anchor.getY() - tooltip.getBottom();
  if (tooltip.getY() >= anchor.getBottom())
    return tooltip.getY() - anchor.getBottom();
  if (tooltip.getRight() <= anchor.getX())
    return anchor.getX() - tooltip.getRight();
  if (tooltip.getX() >= anchor.getRight())
    return tooltip.getX() - anchor.getRight();
  return 0;
}

inline bool hasAnchorProjection(const juce::Rectangle<int> &tooltip,
                                const juce::Rectangle<int> &anchor) {
  const bool verticallySeparated = tooltip.getBottom() <= anchor.getY() ||
                                   tooltip.getY() >= anchor.getBottom();
  if (verticallySeparated)
    return tooltip.getRight() > anchor.getX() &&
           tooltip.getX() < anchor.getRight();

  const bool horizontallySeparated = tooltip.getRight() <= anchor.getX() ||
                                     tooltip.getX() >= anchor.getRight();
  return horizontallySeparated && tooltip.getBottom() > anchor.getY() &&
         tooltip.getY() < anchor.getBottom();
}

inline juce::Rectangle<int>
place(juce::Point<int> tooltipSize, juce::Rectangle<int> anchor,
      juce::Rectangle<int> parentArea,
      const juce::Array<juce::Rectangle<int>> &hardAvoidAreas = {},
      const juce::Array<juce::Rectangle<int>> &softAvoidAreas = {}, int gap = 8,
      int edgeMargin = 4) {
  if (tooltipSize.x <= 0 || tooltipSize.y <= 0 || parentArea.isEmpty())
    return {};

  const auto available = parentArea.reduced(edgeMargin);
  if (available.isEmpty())
    return {};

  tooltipSize.x = juce::jmin(tooltipSize.x, available.getWidth());
  tooltipSize.y = juce::jmin(tooltipSize.y, available.getHeight());
  const auto safeAnchor = anchor.expanded(gap);
  const int centredX = anchor.getCentreX() - tooltipSize.x / 2;
  const int centredY = anchor.getCentreY() - tooltipSize.y / 2;
  const bool preferBelow = anchor.getCentreY() < available.getCentreY();
  const bool preferRight = anchor.getCentreX() < available.getCentreX();

  juce::Rectangle<int> best;
  std::int64_t bestScore = std::numeric_limits<std::int64_t>::max();
  int candidateOrder = 0;

  const auto consider = [&](juce::Rectangle<int> candidate, int tier,
                            int crossAxisDisplacement) {
    const int order = candidateOrder++;
    if (!available.contains(candidate) ||
        intersectionArea(candidate, safeAnchor) != 0 ||
        !hasAnchorProjection(candidate, anchor))
      return;

    for (const auto &avoid : hardAvoidAreas)
      if (intersectionArea(candidate, avoid.expanded(gap)) != 0)
        return;

    std::int64_t softOverlap = 0;
    for (const auto &avoid : softAvoidAreas)
      softOverlap += intersectionArea(candidate, avoid);

    const std::int64_t score =
        static_cast<std::int64_t>(tier) * 1000000000000LL +
        static_cast<std::int64_t>(crossAxisDisplacement) * 1000LL +
        softOverlap + order;
    if (score < bestScore) {
      best = candidate;
      bestScore = score;
    }
  };

  const auto addVerticalSide = [&](bool below, int tier) {
    const int y = below ? anchor.getBottom() + gap
                        : anchor.getY() - gap - tooltipSize.y;
    if (y < available.getY() || y + tooltipSize.y > available.getBottom())
      return;

    juce::Array<int> xPositions;
    xPositions.add(centredX);
    xPositions.add(available.getX());
    xPositions.add(available.getRight() - tooltipSize.x);
    for (const auto &avoid : hardAvoidAreas) {
      xPositions.add(avoid.getX() - gap - tooltipSize.x);
      xPositions.add(avoid.getRight() + gap);
    }

    for (const auto x : xPositions) {
      const int shiftedX = juce::jlimit(
          available.getX(), available.getRight() - tooltipSize.x, x);
      consider({shiftedX, y, tooltipSize.x, tooltipSize.y}, tier,
               std::abs(shiftedX - centredX));
    }
  };

  const auto addHorizontalSide = [&](bool right, int tier) {
    const int x = right ? anchor.getRight() + gap
                        : anchor.getX() - gap - tooltipSize.x;
    if (x < available.getX() || x + tooltipSize.x > available.getRight())
      return;

    juce::Array<int> yPositions;
    yPositions.add(centredY);
    yPositions.add(available.getY());
    yPositions.add(available.getBottom() - tooltipSize.y);
    for (const auto &avoid : hardAvoidAreas) {
      yPositions.add(avoid.getY() - gap - tooltipSize.y);
      yPositions.add(avoid.getBottom() + gap);
    }

    for (const auto y : yPositions) {
      const int shiftedY = juce::jlimit(
          available.getY(), available.getBottom() - tooltipSize.y, y);
      consider({x, shiftedY, tooltipSize.x, tooltipSize.y}, tier,
               std::abs(shiftedY - centredY));
    }
  };

  addVerticalSide(preferBelow, 0);
  addVerticalSide(!preferBelow, 1);
  addHorizontalSide(preferRight, 2);
  addHorizontalSide(!preferRight, 2);
  return best;
}

} // namespace TooltipPlacement
