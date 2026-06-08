#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    Windows 11 风格 LookAndFeel。

    统一应用主题色、字体回退、图标字体、弹出菜单和自定义窗口按钮。
*/
class TransparentButton;

class FluentLookAndFeel : public juce::LookAndFeel_V4 {
public:
  // Windows 11 浅色/深色主题色。
  struct FluentColors {
    juce::Colour background{0xFFF3F3F3};
    juce::Colour sidebarBackground{0xFFF9F9F9};
    juce::Colour cardBackground{0xFFFFFFFF};
    juce::Colour cardBorder{0xFFE5E5E5};

    juce::Colour accentPrimary{0xFF0078D4};
    juce::Colour accentLight{0xFF2B88D8};
    juce::Colour navIndicator{0xFF0078D4};

    juce::Colour textPrimary{0xFF1A1A1A};
    juce::Colour textSecondary{0xFF606060};
    juce::Colour textDisabled{0xFFA0A0A0};

    juce::Colour controlBackground{0xFFFFFFFF};
    juce::Colour controlBorder{0xFFD0D0D0};
    juce::Colour controlHover{0xFFF0F0F0};
    juce::Colour controlPressed{0xFFE8E8E8};

    juce::Colour navSelected{0x15000000};
    juce::Colour navHover{0x08000000};

    juce::Colour transportBackground{0xFFFAFAFA};
    juce::Colour sliderTrack{0xFFE0E0E0};
    juce::Colour sliderProgress{0xFF0078D4};
  };

  FluentLookAndFeel(bool darkMode = true) : isDarkMode(darkMode) {
    if (darkMode) {
      // 深色主题背景保持不透明，避免透出桌面内容。
      colors.background = juce::Colour(0xFF121212);
      colors.sidebarBackground =
          juce::Colour(0x40000000);
      colors.cardBackground = juce::Colour(0xFF1E1E1E);
      colors.cardBorder = juce::Colour(0x30FFFFFF);
      colors.textPrimary = juce::Colour(0xFFFFFFFF);
      colors.textSecondary = juce::Colour(0xFFB0B0B0);
      colors.textDisabled = juce::Colour(0xFF606060);

      colors.controlBackground = juce::Colour(0x25FFFFFF);
      colors.controlBorder = juce::Colour(0x30FFFFFF);
      colors.controlHover = juce::Colour(0x40FFFFFF);
      colors.controlPressed = juce::Colour(0x50FFFFFF);

      colors.navSelected = juce::Colour(0x35FFFFFF);
      colors.navHover = juce::Colour(0x15FFFFFF);
      colors.transportBackground = juce::Colour(0x60000000);
      colors.sliderTrack = juce::Colour(0x40FFFFFF);
    }
    applyScheme();
  }

  void updateAccentColor(juce::Colour newColor) {
    colors.accentPrimary = newColor;
    colors.navIndicator = newColor;
    colors.sliderProgress = newColor;

    // hover 和渐变使用同一套强调色派生值。
    colors.accentLight = newColor.brighter(0.2f);

    applyScheme();
  }

  void applyScheme() {
    setColour(juce::ResizableWindow::backgroundColourId, colors.background);
    setColour(juce::DocumentWindow::backgroundColourId, colors.background);
    setColour(juce::TextButton::buttonColourId, colors.controlBackground);
    setColour(juce::TextButton::textColourOffId, colors.textPrimary);
    setColour(juce::ComboBox::backgroundColourId, colors.controlBackground);
    setColour(juce::ComboBox::outlineColourId, colors.controlBorder);
    setColour(juce::ComboBox::textColourId, colors.textPrimary);
    setColour(juce::Label::textColourId, colors.textPrimary);
    setColour(juce::ListBox::backgroundColourId,
              juce::Colours::transparentBlack);
    setColour(juce::Slider::thumbColourId, colors.textPrimary);

    setColour(juce::ResizableWindow::backgroundColourId, colors.background);
    setColour(juce::DocumentWindow::backgroundColourId, colors.background);
    setColour(juce::PopupMenu::backgroundColourId,
              juce::Colours::transparentBlack);
  }

  // JUCE 在 HiDPI 下传入逻辑像素，LookAndFeel 度量保持同一坐标系，避免字体/图标二次缩放。
  float scaled(float value) const { return value; }
  int scaledInt(int value) const { return value; }

  // === 字体 ===
  juce::String uiFontName = "Source Han Sans SC";
  juce::String playlistFontName = "Microsoft YaHei UI";

  void setUIFont(const juce::String &fontName) { uiFontName = fontName; }
  void setPlaylistFont(const juce::String &fontName) {
    playlistFontName = fontName;
  }

  const juce::String &getUIFontName() const { return uiFontName; }
  const juce::String &getPlaylistFontName() const { return playlistFontName; }

  juce::Font getDefaultFont(float size = 14.0f, bool semibold = false) const {
    juce::FontOptions options(uiFontName, scaled(size),
                              semibold ? juce::Font::bold : juce::Font::plain);
    juce::Font font(options);

    // 中文界面优先回退到微软雅黑，避免系统默认字体缺字。
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) {
      font = juce::Font(
          juce::FontOptions("Microsoft YaHei UI", scaled(size),
                            semibold ? juce::Font::bold : juce::Font::plain));
    }

    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) {
      font = juce::Font(
          juce::FontOptions("Segoe UI", scaled(size),
                            semibold ? juce::Font::bold : juce::Font::plain));
    }

    return font;
  }

  juce::Font getPlaylistFont(float size = 16.0f, bool bold = false) const {
    juce::FontOptions options(playlistFontName, scaled(size),
                              bold ? juce::Font::bold : juce::Font::plain);
    juce::Font font(options);

    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) {
      font = juce::Font(
          juce::FontOptions("Microsoft YaHei UI", scaled(size),
                            bold ? juce::Font::bold : juce::Font::plain));
    }

    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) {
      font = juce::Font(juce::FontOptions(
          "SimHei", scaled(size),
          bold ? juce::Font::bold : juce::Font::plain));
    }

    return font;
  }

  juce::Font getIconFont(float size = 16.0f) const {
    // 图标字体探测使用静态缓存；运行期间安装字体不会刷新。
    static bool hasFluentIcons = []() {
      auto fonts = juce::Font::findAllTypefaceNames();
      for (auto &f : fonts)
        if (f.equalsIgnoreCase("Segoe Fluent Icons"))
          return true;
      return false;
    }();

    static bool hasMDL2 = []() {
      auto fonts = juce::Font::findAllTypefaceNames();
      for (auto &f : fonts)
        if (f.equalsIgnoreCase("Segoe MDL2 Assets"))
          return true;
      return false;
    }();

    juce::String fontName = "Segoe Fluent Icons";
    if (!hasFluentIcons) {
      if (hasMDL2)
        fontName = "Segoe MDL2 Assets";
      else
        fontName = "Segoe UI Symbol";
    }

    // 图标使用固定字号，避免不同 Segoe 图标字体的内部度量差异。
    return juce::Font(
        juce::FontOptions(fontName, scaled(size), juce::Font::plain));
  }

  juce::Font getLabelFont(juce::Label &label) override {
    auto f = label.getFont();
    // Label 已有自定义字号时保持物理高度，避免二次缩放。
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) {
      juce::FontOptions options(uiFontName, f.getHeight(),
                                f.isBold() ? juce::Font::bold : juce::Font::plain);
      return juce::Font(options);
    }
    return f;
  }

  // === 按钮 ===
  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &backgroundColor,
                            bool isMouseOver, bool isButtonDown) override {
    auto bounds = button.getLocalBounds().toFloat();
    float radius = scaled(8.0f);
    // 透明按钮由组件自己绘制状态；标准按钮和切换态在 LookAndFeel 中绘制背景。
    bool isStandardButton =
        (backgroundColor != juce::Colours::transparentBlack);

    if (button.getToggleState() || isMouseOver || isButtonDown ||
        isStandardButton) {
      juce::Colour fill = backgroundColor;

      if (button.getToggleState()) {
        fill = isButtonDown  ? colors.accentPrimary.darker(0.2f)
               : isMouseOver ? colors.accentPrimary.brighter(0.1f)
                             : colors.accentPrimary;
      } else if (isStandardButton) {
        if (!button.isEnabled())
          fill = backgroundColor.withAlpha(0.2f);
        else if (backgroundColor != colors.controlBackground &&
                 backgroundColor != juce::Colours::transparentBlack) {
          if (isButtonDown)
            fill = backgroundColor.darker(0.15f);
          else if (isMouseOver)
            fill = backgroundColor.brighter(0.15f);
        } else {
          if (isButtonDown)
            fill = colors.controlPressed;
          else if (isMouseOver)
            fill = colors.controlHover;
          else
            fill = colors.controlBackground;
        }
      } else {
      }

      if (fill != juce::Colours::transparentBlack) {
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, radius);
      }

      if (isStandardButton || button.getToggleState()) {
        if (backgroundColor == juce::Colours::red ||
            backgroundColor.getHue() < 0.05f) {
          g.setColour(fill.darker(0.2f).withAlpha(0.5f));
        } else {
          g.setColour(colors.controlBorder);
        }
        g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);
      }
    }
  }

  void drawButtonText(juce::Graphics &g, juce::TextButton &button, bool,
                      bool) override {
    auto text = button.getButtonText();

    // PUA 字符按图标字体绘制，其余文本使用界面字体。
    if (text.length() > 0 &&
        (uint32_t)text.getCharPointer().getAndAdvance() >= 0xE000) {
      g.setFont(getIconFont(juce::jmin(16.0f, button.getHeight() * 0.5f)));
      g.setColour(button.getToggleState() ? juce::Colours::white
                                          : colors.textPrimary);
    } else {
      g.setFont(getDefaultFont(14.0f, false));
      g.setColour(button.getToggleState() ? juce::Colours::white
                                          : colors.textPrimary);
    }

    g.drawText(text, button.getLocalBounds(), juce::Justification::centred,
               false);
  }

  // === 圆形播放按钮 ===
  void drawCircularButton(juce::Graphics &g, juce::Rectangle<float> bounds,
                          const juce::String &icon, bool isPlaying, bool,
                          bool) {
    g.setFont(getIconFont(20.0f));
    g.setColour(colors.textPrimary);
    g.drawText(icon, bounds.toNearestInt(), juce::Justification::centred,
               false);
  }

  int getSliderThumbRadius(juce::Slider &slider) override {
    if (slider.getSliderStyle() == juce::Slider::LinearHorizontal ||
        slider.getSliderStyle() == juce::Slider::LinearVertical)
      return scaledInt(7);
    return juce::LookAndFeel_V4::getSliderThumbRadius(slider);
  }

  juce::Slider::SliderLayout getSliderLayout(juce::Slider &slider) override {
    juce::Slider::SliderLayout layout;
    auto bounds = slider.getLocalBounds();

    if (slider.getTextBoxPosition() != juce::Slider::NoTextBox) {
      int w = slider.getTextBoxWidth();
      int h = slider.getTextBoxHeight();
      int gap = 10;

      if (slider.getTextBoxPosition() == juce::Slider::TextBoxLeft) {
        layout.textBoxBounds = bounds.removeFromLeft(w);
        bounds.removeFromLeft(gap);
      } else if (slider.getTextBoxPosition() == juce::Slider::TextBoxRight) {
        layout.textBoxBounds = bounds.removeFromRight(w);
        bounds.removeFromRight(gap);
      } else if (slider.getTextBoxPosition() == juce::Slider::TextBoxAbove) {
        layout.textBoxBounds = bounds.removeFromTop(h);
        bounds.removeFromTop(gap);
      } else if (slider.getTextBoxPosition() == juce::Slider::TextBoxBelow) {
        layout.textBoxBounds = bounds.removeFromBottom(h);
        bounds.removeFromBottom(gap);
      } else {
        // 居中和其他文本框模式直接覆盖滑块区域。
        layout.textBoxBounds = bounds;
      }
    } else {
      layout.textBoxBounds = {};
    }

    if (slider.getSliderStyle() == juce::Slider::LinearHorizontal) {
      int radius = getSliderThumbRadius(slider);
      // 轨道内缩一个 thumb 半径，确保圆点不会越出组件边界。
      layout.sliderBounds = bounds.reduced(radius, 0);
    } else {
      layout.sliderBounds = bounds;
    }

    return layout;
  }

  // === 滑块 ===
  void drawLinearSlider(juce::Graphics &g, int x, int y, int width, int height,
                        float sliderPos, float, float,
                        juce::Slider::SliderStyle style,
                        juce::Slider &slider) override {
    if (style != juce::Slider::LinearHorizontal)
      return;

    bool isEnabled = slider.isEnabled();
    auto bounds =
        juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height);
    float trackHeight = scaled(4.0f);
    float thumbSize = scaled(14.0f);
    float thumbRadius = thumbSize / 2.0f;

    auto trackArea =
        bounds.withSizeKeepingCentre(bounds.getWidth(), trackHeight);

    auto trackColor =
        isEnabled ? colors.sliderTrack : colors.sliderTrack.withAlpha(0.3f);
    g.setColour(trackColor);

    // 背景轨道略微内缩，圆角端点保持在可视边界内。
    float visualPadding = trackHeight / 2.0f;
    juce::Path trackPath;
    trackPath.startNewSubPath(trackArea.getX() + visualPadding,
                              trackArea.getCentreY());
    trackPath.lineTo(trackArea.getRight() - visualPadding,
                     trackArea.getCentreY());
    g.strokePath(trackPath,
                 juce::PathStrokeType(trackHeight, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));

    if (sliderPos > (trackArea.getX() + visualPadding)) {
      auto progressColor = isEnabled ? colors.sliderProgress
                                     : colors.sliderProgress.withAlpha(0.3f);
      g.setColour(progressColor);

      juce::Path progressPath;
      progressPath.startNewSubPath(trackArea.getX() + visualPadding,
                                   trackArea.getCentreY());
      progressPath.lineTo(sliderPos, trackArea.getCentreY());
      g.strokePath(progressPath, juce::PathStrokeType(
                                     trackHeight, juce::PathStrokeType::curved,
                                     juce::PathStrokeType::rounded));
    }

    float thumbX = sliderPos - thumbRadius;
    float thumbY = bounds.getCentreY() - thumbRadius;

    float innerRatio = 0.45f;
    if (isEnabled) {
      if (slider.isMouseButtonDown())
        innerRatio = 0.35f;
      else if (slider.isMouseOver())
        innerRatio = 0.55f;
    }

    float innerSize = thumbSize * innerRatio;
    float innerRadius = innerSize / 2.0f;
    float innerX = sliderPos - innerRadius;
    float innerY = bounds.getCentreY() - innerRadius;

    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillEllipse(thumbX + 1.0f, thumbY + 2.0f, thumbSize, thumbSize);

    g.setColour(juce::Colour(0xFF3B3B3B));
    g.fillEllipse(thumbX, thumbY, thumbSize, thumbSize);

    g.setColour(juce::Colour(0xFF5A5A5A));
    g.drawEllipse(thumbX + 0.5f, thumbY + 0.5f, thumbSize - 1.0f,
                  thumbSize - 1.0f, 1.0f);

    auto innerColor =
        isEnabled ? colors.sliderProgress : juce::Colours::darkgrey;
    g.setColour(innerColor);
    g.fillEllipse(innerX, innerY, innerSize, innerSize);
  }

  // === ComboBox ===
  void drawComboBox(juce::Graphics &g, int width, int height, bool isButtonDown,
                    int, int, int, int, juce::ComboBox &box) override {
    auto bounds = juce::Rectangle<float>(0, 0, (float)width, (float)height);
    float radius = scaled(6.0f);

    // 弹出菜单打开时仍绘制为激活态，避免下拉框停留在按下态。
    juce::Colour bg;
    bool isMenuOpen = box.isPopupActive();

    if (!box.isEnabled()) {
      bg = juce::Colours::black.withAlpha(0.1f);
    } else if (isButtonDown || isMenuOpen) {
      bg = juce::Colours::black.withAlpha(0.6f);
    } else if (box.isMouseOver()) {
      bg = juce::Colours::black.withAlpha(0.45f);
    } else {
      bg = juce::Colours::black.withAlpha(0.35f);
    }

    g.setColour(bg);
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(colors.controlBorder);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);

    auto arrowZone = bounds.removeFromRight(scaled(24.0f));
    g.setFont(getIconFont(8.0f));
    g.setColour(colors.textSecondary);
    g.drawText(L"\uE70D", arrowZone.toNearestInt(),
               juce::Justification::centred, false);
  }

  juce::Font getComboBoxFont(juce::ComboBox &) override {
    return getDefaultFont(12.0f);
  }

  int getMenuWindowFlags() override {
    // 半透明弹窗禁用系统阴影，避免圆角区域残影。
    return juce::ComponentPeer::windowIsSemiTransparent;
  }

  juce::Font getPopupMenuFont() override {
    return getDefaultFont(13.0f);
  }

  // === 弹出菜单 ===
  void drawPopupMenuBackground(juce::Graphics &g, int width,
                               int height) override {
    g.fillAll(juce::Colours::transparentBlack);

    float radius = scaled(8.0f);
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.fillRoundedRectangle(0.0f, 0.0f, (float)width, (float)height, radius);

    g.setColour(juce::Colour(0x30FFFFFF));
    g.drawRoundedRectangle(0.5f, 0.5f, (float)width - 1.0f,
                           (float)height - 1.0f, radius, 1.0f);
  }

  void drawPopupMenuItem(juce::Graphics &g, const juce::Rectangle<int> &area,
                         bool isSeparator, bool isActive, bool isHighlighted,
                         bool isTicked, bool, const juce::String &text,
                         const juce::String &, const juce::Drawable *,
                         const juce::Colour *) override {
    if (isSeparator) {
      g.setColour(colors.cardBorder);
      g.drawHorizontalLine(area.getCentreY(), (float)area.getX() + scaled(8.0f),
                           (float)area.getRight() - scaled(8.0f));
      return;
    }

    auto r = area.reduced(scaledInt(4), scaledInt(1));

    if (isHighlighted && isActive) {
      g.setColour(colors.accentPrimary.withAlpha(0.2f));
      g.fillRoundedRectangle(r.toFloat(), scaled(4.0f));
    }

    auto checkArea = r.removeFromLeft(scaledInt(20));

    if (isTicked) {
      g.setColour(isActive ? colors.accentPrimary : colors.textDisabled);
      g.setFont(getIconFont(10.0f));
      g.drawText(L"\uE73E", checkArea, juce::Justification::centred, false);
    }

    g.setColour(isActive ? colors.textPrimary : colors.textDisabled);
    g.setFont(getDefaultFont(13.0f));
    g.drawText(text, r.reduced(scaledInt(2), 0),
               juce::Justification::centredLeft, true);
    g.setFont(getDefaultFont(13.0f));
    g.drawText(text, r.reduced(scaledInt(2), 0),
               juce::Justification::centredLeft, true);
  }

  // === Tooltip ===
  juce::Rectangle<int>
  getTooltipBounds(const juce::String &tipText, juce::Point<int> screenPos,
                   juce::Rectangle<int> parentArea) override {
    juce::Font font = getDefaultFont(14.0f);
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4996)
#endif
    int w = font.getStringWidth(tipText) + 20;
#if defined(_MSC_VER)
#pragma warning(pop)
#endif
    int h = 32;

    const int edgeGap = 12;
    juce::Rectangle<int> bounds(screenPos.x, screenPos.y - h - 10, w, h);

    return bounds.constrainedWithin(parentArea.reduced(edgeGap));
  }

  void drawTooltip(juce::Graphics &g, const juce::String &text, int width,
                   int height) override {
    // 先清空透明背景，避免圆角区域残留。
    g.fillAll(juce::Colours::transparentBlack);

    juce::Rectangle<float> bounds(0, 0, (float)width, (float)height);

    g.setColour(juce::Colour(0xE6202020));
    g.fillRoundedRectangle(bounds, 6.0f);

    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    g.setColour(juce::Colours::white);
    g.setFont(getDefaultFont(14.0f));
    g.drawText(text, bounds, juce::Justification::centred, false);
  }

  // === AlertWindow ===
  void drawAlertBox(juce::Graphics &g, juce::AlertWindow &alert,
                    const juce::Rectangle<int> &textArea,
                    juce::TextLayout &layout) override {
    auto bounds = alert.getLocalBounds().toFloat();
    float radius = scaled(12.0f);

    g.setColour(colors.background);
    g.fillRoundedRectangle(bounds, radius);

    g.setColour(colors.cardBorder);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);

    g.setColour(alert.findColour(juce::AlertWindow::textColourId));
    layout.draw(g, textArea.toFloat());
  }

  void drawAlertButtons(juce::AlertWindow &alert) {
    for (int i = 0; i < alert.getNumButtons(); ++i) {
      if (auto *b = alert.getButton(i)) {
        b->setLookAndFeel(this);
      }
    }
  }

  // === ScrollBar ===
  void drawScrollbar(juce::Graphics &g, juce::ScrollBar &, int x, int y,
                     int width, int height, bool isVertical, int thumbStart,
                     int thumbSize, bool isMouseOver, bool) override {
    float barWidth = isMouseOver ? scaled(8.0f) : scaled(4.0f);

    juce::Rectangle<float> thumb;
    if (isVertical) {
      thumb = {(float)x + (float)width - barWidth - scaled(2.0f),
               (float)y + (float)thumbStart, barWidth, (float)thumbSize};
    } else {
      thumb = {(float)x + (float)thumbStart,
               (float)y + (float)height - barWidth - scaled(2.0f),
               (float)thumbSize, barWidth};
    }

    g.setColour(colors.textSecondary.withAlpha(isMouseOver ? 0.7f : 0.4f));
    g.fillRoundedRectangle(thumb, barWidth / 2.0f);
  }

  // === DocumentWindow ===
  void drawDocumentWindowTitleBar(juce::DocumentWindow &window,
                                  juce::Graphics &g, int w, int h,
                                  int titleSpaceX, int titleSpaceW,
                                  const juce::Image *icon,
                                  bool drawTitleTextOnLeft) override {
    auto bounds = juce::Rectangle<int>(0, 0, w, h).toFloat();

    // 标题栏背景与卡片背景保持一致。
    g.setColour(colors.cardBackground);
    g.fillAll();

    g.setColour(colors.textPrimary);
    g.setFont(getDefaultFont(14.0f, true));

    auto title = window.getName();
    g.drawText(title, titleSpaceX, 0, titleSpaceW, h,
               juce::Justification::centredLeft, true);

    if (icon != nullptr) {
      g.drawImageWithin(*icon, 6, (h - 16) / 2, 16, 16,
                        juce::RectanglePlacement::centred, false);
    }
  }

  juce::Button *createDocumentWindowButton(int buttonType) override;

  const FluentColors &getColors() const { return colors; }
  bool isDark() const { return isDarkMode; }

private:
  FluentColors colors;
  bool isDarkMode;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FluentLookAndFeel)
};
