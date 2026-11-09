#pragma once

#include "CustomLookAndFeel.h"
#include "DesignTokens.h"

// Contract: 消息线程设置调色板及选择；只在用户选色时调用 `onColorSelected`。
// Postconditions: 更换调色板清除旧索引，空输入使用默认颜色；颜色未匹配时保留当前选择。
class PaletteSelector : public juce::Component {
public:
  std::function<void(juce::Colour)> onColorSelected;
  int selectedIndex = -1;

  void setPalette(const std::vector<juce::Colour> &colors) {
    if (palette == colors)
      return;
    palette = colors;
    if (palette.empty())
      palette = {juce::Colour(0xFF0078D4)};

    selectedIndex = -1;
    repaint();
  }

  void setSelectedIndex(int index) {
    if (selectedIndex != index) {
      selectedIndex = index;
      repaint();
    }
  }

  void setSelectedColor(juce::Colour c) {
    if (palette.empty())
      return;
    for (size_t i = 0; i < palette.size(); ++i) {
      // 允许过渡动画中的轻微舍入误差
      auto p = palette[i];
      if (std::abs(p.getRed() - c.getRed()) <= 1 &&
          std::abs(p.getGreen() - c.getGreen()) <= 1 &&
          std::abs(p.getBlue() - c.getBlue()) <= 1) {
        setSelectedIndex((int)i);
        return;
      }
    }
    // 动画中间色找不到匹配项时保持当前选择，避免闪烁
  }

  void paint(juce::Graphics &g) override {
    if (palette.empty())
      return;

    auto area = getLocalBounds().toFloat();
    int count = std::min((int)palette.size(), 6);
    float gap = 8.0f;
    float maxSwatchWidth = 48.0f;
    float maxSlots = 6.0f;
    float totalGapSpace = (maxSlots - 1.0f) * gap;
    float swatchWidth = juce::jmax(
        1.0f, juce::jmin(maxSwatchWidth,
                         (area.getWidth() - totalGapSpace) / maxSlots));

    for (int i = 0; i < count; ++i) {
      juce::Rectangle<float> r(i * (swatchWidth + gap), 0, swatchWidth,
                               area.getHeight());

      g.setColour(palette[i]);
      g.fillRoundedRectangle(r, 4.0f);

      if (i == selectedIndex) {
        float lux = (palette[i].getFloatRed() * 0.2126f +
                     palette[i].getFloatGreen() * 0.7152f +
                     palette[i].getFloatBlue() * 0.0722f);
        bool isBright = lux > 0.65f;

        g.setColour(isBright ? juce::Colours::black.withAlpha(0.6f)
                             : juce::Colours::white.withAlpha(0.85f));
        g.drawRoundedRectangle(r.reduced(0.5f), 4.0f, 3.5f);

        if (auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel())) {
          g.setColour(isBright ? juce::Colours::black.withAlpha(0.85f)
                               : juce::Colours::white.withAlpha(0.95f));
          laf->drawIconGlyph(g, L"\uE73E", r.toFloat(),
                             DesignTokens::Icon::small);
        }
      }
    }
  }

  void mouseDown(const juce::MouseEvent &e) override {
    if (palette.empty() || !onColorSelected)
      return;

    int count = std::min((int)palette.size(), 6);
    float gap = 8.0f;
    float maxSlots = 6.0f;
    float totalGapSpace = (maxSlots - 1.0f) * gap;
    float swatchWidth =
        juce::jmax(1.0f, juce::jmin(48.0f, ((float)getWidth() - totalGapSpace) /
                                               maxSlots));

    for (int i = 0; i < count; ++i) {
      float startX = i * (swatchWidth + gap);
      if (e.position.x >= startX && e.position.x <= startX + swatchWidth) {
        setSelectedIndex(i);
        onColorSelected(palette[i]);
        break;
      }
    }
  }

private:
  std::vector<juce::Colour> palette;
};
