#pragma once

struct BackgroundImageState {
  bool hasProcessedImage = false;
  bool hasPreviousImage = false;
  bool hasOriginalImage = false;
  bool isTransitioning = false;
};

inline bool
hasDrawableBackgroundImage(const BackgroundImageState &state) {
  if (state.isTransitioning)
    return state.hasProcessedImage || state.hasPreviousImage;

  return state.hasProcessedImage || state.hasOriginalImage;
}

inline bool shouldDrawVisibleBackgroundImage(
    const BackgroundImageState &state, bool effectRequiresProcessedImage) {
  if (state.isTransitioning)
    return state.hasProcessedImage || state.hasPreviousImage;

  if (state.hasProcessedImage)
    return true;

  if (effectRequiresProcessedImage)
    return false;

  return state.hasOriginalImage;
}

inline bool shouldDrawOriginalBackgroundImage(
    const BackgroundImageState &state, bool effectRequiresProcessedImage) {
  return !state.isTransitioning && state.hasOriginalImage &&
         !state.hasProcessedImage && !effectRequiresProcessedImage;
}

inline bool isBackgroundEffectStrengthEnabled(int selectedEffectId,
                                              bool hasBackgroundImage) {
  return hasBackgroundImage && selectedEffectId > 1;
}
