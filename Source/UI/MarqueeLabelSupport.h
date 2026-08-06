#pragma once

inline bool shouldRunMarqueeTimer(int textWidth, int labelWidth) {
  return labelWidth > 0 && textWidth > labelWidth;
}
