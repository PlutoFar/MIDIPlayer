#pragma once

// Contract: 消息线程更新目标并逐帧推进；展开和悬停进度保持在 [0,1]。
// Postconditions: 读取接口只返回当前动画状态；`tick` 返回是否仍需刷新。

#include <algorithm>
#include <cmath>

class ComboBoxAnimationState {
public:
  void setTargets(bool shouldOpen, bool shouldHover) {
    openTarget = shouldOpen ? 1.0f : 0.0f;
    hoverTarget = shouldHover ? 1.0f : 0.0f;
  }

  bool tick() {
    openProgress = approach(openProgress, openTarget, 0.22f);
    hoverProgress = approach(hoverProgress, hoverTarget, 0.18f);
    return isAnimating();
  }

  bool isAnimating() const {
    return openProgress != openTarget || hoverProgress != hoverTarget;
  }

  float getOpenProgress() const { return openProgress; }
  float getHoverProgress() const { return hoverProgress; }

private:
  static float approach(float current, float target, float response) {
    const float next = current + (target - current) * response;
    return std::abs(target - next) < 0.002f ? target : next;
  }

  float openProgress = 0.0f;
  float hoverProgress = 0.0f;
  float openTarget = 0.0f;
  float hoverTarget = 0.0f;
};
