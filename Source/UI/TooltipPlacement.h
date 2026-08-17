#pragma once

#include <cmath>
#include <limits>
#include <juce_gui_basics/juce_gui_basics.h>

namespace TooltipPlacement {

inline int intersectionArea(const juce::Rectangle<int> &a,
                            const juce::Rectangle<int> &b) {
  const auto intersection = a.getIntersection(b);
  return intersection.isEmpty()
             ? 0
             : intersection.getWidth() * intersection.getHeight();
}

inline juce::Rectangle<int>
place(juce::Point<int> tooltipSize, juce::Rectangle<int> anchor,
      juce::Rectangle<int> parentArea,
      const juce::Array<juce::Rectangle<int>> &avoidAreas = {}, int gap = 8,
      int edgeMargin = 4) {
  if (tooltipSize.x <= 0 || tooltipSize.y <= 0 || parentArea.isEmpty())
    return {};

  const auto available = parentArea.reduced(edgeMargin);
  if (available.isEmpty())
    return {};
  tooltipSize.x = juce::jmin(tooltipSize.x, available.getWidth());
  tooltipSize.y = juce::jmin(tooltipSize.y, available.getHeight());

  const int centredX = anchor.getCentreX() - tooltipSize.x / 2;
  const int centredY = anchor.getCentreY() - tooltipSize.y / 2;
  const juce::Rectangle<int> candidates[] = {
      {centredX, anchor.getY() - gap - tooltipSize.y, tooltipSize.x,
       tooltipSize.y},
      {centredX, anchor.getBottom() + gap, tooltipSize.x, tooltipSize.y},
      {anchor.getRight() + gap, centredY, tooltipSize.x, tooltipSize.y},
      {anchor.getX() - gap - tooltipSize.x, centredY, tooltipSize.x,
       tooltipSize.y}};

  juce::Rectangle<int> best;
  int bestScore = std::numeric_limits<int>::max();
  for (int index = 0; index < 4; ++index) {
    const auto candidate = candidates[index].constrainedWithin(available);
    int score = intersectionArea(candidate, anchor) * 10000 + index;
    for (const auto &avoid : avoidAreas)
      score += intersectionArea(candidate, avoid) * 100;

    const auto displacement =
        candidate.getPosition() - candidates[index].getPosition();
    score += std::abs(displacement.x) + std::abs(displacement.y);
    if (score < bestScore) {
      best = candidate;
      bestScore = score;
    }
  }
  return best;
}

} // namespace TooltipPlacement
