#pragma once

// Contract: 布尔值描述同一时刻的背景图像状态；本模块只选择绘制来源和控件可用性。
// Invariant: 需要处理图的效果在结果未就绪时不展示原图；效果 ID 必须使用持久化编号。

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
