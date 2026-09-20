#pragma once

// Contract: 文本宽度与标签宽度使用同一逻辑坐标单位；只有可见区域为正且文本溢出时启用滚动。

inline bool shouldRunMarqueeTimer(int textWidth, int labelWidth) {
  return labelWidth > 0 && textWidth > labelWidth;
}
