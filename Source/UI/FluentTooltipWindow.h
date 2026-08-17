#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

class FluentTooltipWindow final : public juce::TooltipWindow {
public:
  explicit FluentTooltipWindow(juce::Component *parentComponent = nullptr,
                               int millisecondsBeforeTipAppears = 700)
      : juce::TooltipWindow(parentComponent, millisecondsBeforeTipAppears) {
    // TooltipWindow 默认创建不透明 peer。关闭不透明标志后，JUCE 会创建
    // layered desktop peer，使圆角外区域保留透明通道。
    setOpaque(false);
  }
};
