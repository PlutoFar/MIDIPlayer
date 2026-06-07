#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    FluentLookAndFeel: Windows 11 Media Player style implementation.

    Based on the actual Windows Media Player design with:
    - Light theme support (matching reference)
    - Full high DPI support
    - Proper icon sizing and spacing
    - Clean typography
*/
class TransparentButton;

class FluentLookAndFeel : public juce::LookAndFeel_V4 {
public:
  // Windows 11 Light/Dark theme colors
  struct FluentColors {
    // Light theme (matching Windows Media Player)
    juce::Colour background{0xFFF3F3F3};
    juce::Colour sidebarBackground{0xFFF9F9F9};
    juce::Colour cardBackground{0xFFFFFFFF};
    juce::Colour cardBorder{0xFFE5E5E5};

    // Accent
    juce::Colour accentPrimary{0xFF0078D4}; // Windows Blue
    juce::Colour accentLight{0xFF2B88D8};
    juce::Colour navIndicator{0xFF0078D4};

    // Text
    juce::Colour textPrimary{0xFF1A1A1A};
    juce::Colour textSecondary{0xFF606060};
    juce::Colour textDisabled{0xFFA0A0A0};

    // Controls
    juce::Colour controlBackground{0xFFFFFFFF};
    juce::Colour controlBorder{0xFFD0D0D0};
    juce::Colour controlHover{0xFFF0F0F0};
    juce::Colour controlPressed{0xFFE8E8E8};

    // Navigation
    juce::Colour navSelected{0x15000000};
    juce::Colour navHover{0x08000000};

    // Transport bar
    juce::Colour transportBackground{0xFFFAFAFA};
    juce::Colour sliderTrack{0xFFE0E0E0};
    juce::Colour sliderProgress{0xFF0078D4}; // Match accent
  };

  FluentLookAndFeel(bool darkMode = true) : isDarkMode(darkMode) {
    if (darkMode) {
      // Dark theme colors with optimized Glassmorphism
      // Dark theme colors - Opaque background to avoid desktop bleed
      colors.background = juce::Colour(0xFF121212);
      colors.sidebarBackground =
          juce::Colour(0x40000000); // Overlay over background
      colors.cardBackground = juce::Colour(0xFF1E1E1E); // Solid base for cards
      colors.cardBorder = juce::Colour(0x30FFFFFF);
      colors.textPrimary = juce::Colour(0xFFFFFFFF);
      colors.textSecondary = juce::Colour(0xFFB0B0B0);
      colors.textDisabled = juce::Colour(0xFF606060);

      // Control colors (Translucent Glass)
      colors.controlBackground = juce::Colour(0x25FFFFFF); // 15% white
      colors.controlBorder = juce::Colour(0x30FFFFFF);     // 20% white border
      colors.controlHover = juce::Colour(0x40FFFFFF);      // 25% white
      colors.controlPressed = juce::Colour(0x50FFFFFF);    // 30% white

      colors.navSelected = juce::Colour(0x35FFFFFF);
      colors.navHover = juce::Colour(0x15FFFFFF);
      colors.transportBackground = juce::Colour(0x60000000); // 38% black
      colors.sliderTrack = juce::Colour(0x40FFFFFF);
    }
    applyScheme();
  }

  void updateAccentColor(juce::Colour newColor) {
    colors.accentPrimary = newColor;
    colors.navIndicator = newColor;
    colors.sliderProgress = newColor;

    // Generate a lighter variant for hover/gradients
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

  // === DPI Scaling ===
  // Cached for performance - refresh when display settings change
  float getScaleFactor() const {
    auto now = juce::Time::currentTimeMillis();
    // Refresh cache every 1 second (display changes are rare)
    if (now - lastScaleUpdate > 1000) {
      lastScaleUpdate = now;
      if (auto *display =
              juce::Desktop::getInstance().getDisplays().getPrimaryDisplay()) {
        cachedScaleFactor = (float)display->scale;
      } else {
        cachedScaleFactor = 1.0f;
      }
    }
    return cachedScaleFactor;
  }

  float scaled(float value) const { return value * getScaleFactor(); }
  int scaledInt(int value) const {
    return juce::roundToInt(scaled((float)value));
  }


  // === Typography ===
  // Configurable font names
  juce::String uiFontName = "Source Han Sans SC"; // 思源黑体 for UI elements
  juce::String playlistFontName = "Microsoft YaHei UI"; // Default playlist font

  void setUIFont(const juce::String &fontName) { uiFontName = fontName; }
  void setPlaylistFont(const juce::String &fontName) {
    playlistFontName = fontName;
  }

  const juce::String &getUIFontName() const { return uiFontName; }
  const juce::String &getPlaylistFontName() const { return playlistFontName; }

  juce::Font getDefaultFont(float size = 14.0f, bool semibold = false) const {
    // Try configured UI font first (Source Han Sans SC)
    juce::FontOptions options(uiFontName, scaled(size),
                              semibold ? juce::Font::bold : juce::Font::plain);
    juce::Font font(options);

    // If not available, try Microsoft YaHei for Chinese support
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) {
      font = juce::Font(
          juce::FontOptions("Microsoft YaHei UI", scaled(size),
                            semibold ? juce::Font::bold : juce::Font::plain));
    }

    // Final fallback to Segoe UI
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) {
      font = juce::Font(
          juce::FontOptions("Segoe UI", scaled(size),
                            semibold ? juce::Font::bold : juce::Font::plain));
    }

    return font;
  }

  juce::Font getPlaylistFont(float size = 16.0f, bool bold = false) const {
    // Use configured playlist font
    juce::FontOptions options(playlistFontName, scaled(size),
                              bold ? juce::Font::bold : juce::Font::plain);
    juce::Font font(options);

    // Initial Fallback
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) {
      font = juce::Font(
          juce::FontOptions("Microsoft YaHei UI", scaled(size),
                            bold ? juce::Font::bold : juce::Font::plain));
    }

    // Final Fallback
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) {
      font = juce::Font(juce::FontOptions(
          "SimHei", scaled(size), // Fallback to SimHei if YaHei fails?
          bold ? juce::Font::bold : juce::Font::plain));
    }

    return font;
  }

  juce::Font getIconFont(float size = 16.0f) const {
    // Check if Segoe Fluent Icons is available
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

    juce::String fontName = "Segoe Fluent Icons"; // Preferred for Win11
    if (!hasFluentIcons) {
      if (hasMDL2)
        fontName = "Segoe MDL2 Assets"; // Fallback to Win10
      else
        fontName = "Segoe UI Symbol"; // Final fallback
    }

    // Use a fixed size for icons to prevent font-internal scaling issues
    return juce::Font(
        juce::FontOptions(fontName, scaled(size), juce::Font::plain));
  }

  juce::Font getLabelFont(juce::Label &label) override {
    auto f = label.getFont();
    // Prevent double-scaling: if the font is already using our custom UI font,
    // or if it has a custom size, just return it with its exact physical height.
    if (f.getTypefaceName() == juce::Font::getDefaultSansSerifFontName()) {
      juce::FontOptions options(uiFontName, f.getHeight(),
                                f.isBold() ? juce::Font::bold : juce::Font::plain);
      return juce::Font(options);
    }
    return f;
  }

  // === Buttons ===
  void drawButtonBackground(juce::Graphics &g, juce::Button &button,
                            const juce::Colour &backgroundColor,
                            bool isMouseOver, bool isButtonDown) override {
    auto bounds = button.getLocalBounds().toFloat();
    float radius = scaled(8.0f);
    // Only draw background if interactive (hover/down) or toggled
    // OR if it's a standard button (background colour is NOT transparentBlack)
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
        // Standard button logic
        if (!button.isEnabled())
          fill = backgroundColor.withAlpha(0.2f);
        else if (backgroundColor != colors.controlBackground &&
                 backgroundColor != juce::Colours::transparentBlack) {
          // Custom colored button (e.g. Danger Red) - vary the custom color
          if (isButtonDown)
            fill = backgroundColor.darker(0.15f);
          else if (isMouseOver)
            fill = backgroundColor.brighter(0.15f);
        } else {
          // Default control colors
          if (isButtonDown)
            fill = colors.controlPressed;
          else if (isMouseOver)
            fill = colors.controlHover;
          else
            fill = colors.controlBackground;
        }
      } else {
        // Transparent button logic (minimalist)
        // Draw NOTHING. The component (TransparentButton/AnimatedIconButton)
        // handles its own visual state (opacity/scaling).
        // Drawing here creates a redundant "mask".
      }

      if (fill != juce::Colours::transparentBlack) {
        g.setColour(fill);
        g.fillRoundedRectangle(bounds, radius);
      }

      // Draw border for standard buttons or toggled states for clarity
      if (isStandardButton || button.getToggleState()) {
        // Destructive buttons usually have a darker border
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

  // Standard L-string literal approach (industry standard for Windows/JUCE)
  void drawButtonText(juce::Graphics &g, juce::TextButton &button, bool,
                      bool) override {
    auto text = button.getButtonText();

    // Standard L-string literal approach (industry standard for Windows/JUCE)
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

  // === Circular Play Button (like Windows Media Player) ===
  void drawCircularButton(juce::Graphics &g, juce::Rectangle<float> bounds,
                          const juce::String &icon, bool isPlaying, bool,
                          bool) {
    // Background fill removed as per user request (redundant highlight)

    // Icon
    g.setFont(getIconFont(20.0f));
    g.setColour(colors.textPrimary);
    g.drawText(icon, bounds.toNearestInt(), juce::Justification::centred,
               false);
  }

  int getSliderThumbRadius(juce::Slider &slider) override {
    if (slider.getSliderStyle() == juce::Slider::LinearHorizontal ||
        slider.getSliderStyle() == juce::Slider::LinearVertical)
      return scaledInt(7); // Fixed 14px diameter interaction
    return juce::LookAndFeel_V4::getSliderThumbRadius(slider);
  }

  juce::Slider::SliderLayout getSliderLayout(juce::Slider &slider) override {
    juce::Slider::SliderLayout layout;
    auto bounds = slider.getLocalBounds();

    // 1. Calculate Text Box Layout
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
        // Centered or other modes: overlay? simplified for now
        layout.textBoxBounds = bounds;
      }
    } else {
      layout.textBoxBounds = {};
    }

    // 2. Calculate Slider Track Layout
    if (slider.getSliderStyle() == juce::Slider::LinearHorizontal) {
      int radius = getSliderThumbRadius(slider);
      // Inset the slider bounds by the thumb radius so the thumb stays fully
      // inside
      layout.sliderBounds = bounds.reduced(radius, 0);
    } else {
      layout.sliderBounds = bounds;
    }

    return layout;
  }

  // === Sliders ===
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
    float thumbSize = scaled(14.0f); // Outer ring size (reduced from 16px)
    float thumbRadius = thumbSize / 2.0f;

    // Track spans full width
    auto trackArea =
        bounds.withSizeKeepingCentre(bounds.getWidth(), trackHeight);

    // Background track
    auto trackColor =
        isEnabled ? colors.sliderTrack : colors.sliderTrack.withAlpha(0.3f);
    g.setColour(trackColor);

    // Draw background track slightly inset to keep rounded caps inside visual
    // component
    float visualPadding = trackHeight / 2.0f;
    juce::Path trackPath;
    trackPath.startNewSubPath(trackArea.getX() + visualPadding,
                              trackArea.getCentreY());
    trackPath.lineTo(trackArea.getRight() - visualPadding,
                     trackArea.getCentreY());
    g.strokePath(trackPath,
                 juce::PathStrokeType(trackHeight, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));

    // Value track (progress fill)
    // Only draw if we've moved past the start of the track
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

    // Windows 11 Fluent Thumb: Outer ring + animated inner circle
    // Always draw even when disabled, but use grey for inner circle
    float thumbX = sliderPos - thumbRadius;
    float thumbY = bounds.getCentreY() - thumbRadius;

    // Calculate inner circle size based on state
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

    // 1. Drop shadow for depth
    g.setColour(juce::Colours::black.withAlpha(0.3f));
    g.fillEllipse(thumbX + 1.0f, thumbY + 2.0f, thumbSize, thumbSize);

    // 2. Outer Ring (dark background for Win11 dark theme)
    g.setColour(juce::Colour(0xFF3B3B3B));
    g.fillEllipse(thumbX, thumbY, thumbSize, thumbSize);

    // 3. Outer Ring Border
    g.setColour(juce::Colour(0xFF5A5A5A));
    g.drawEllipse(thumbX + 0.5f, thumbY + 0.5f, thumbSize - 1.0f,
                  thumbSize - 1.0f, 1.0f);

    // 4. Inner Circle (colored if enabled, grey if disabled)
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

    // Always draw background for ComboBox so boundaries are clear
    // Use DARK background with subtle changes (avoid "stuck" look)
    // Fix: If popup is active (menu open), treat as 'Hover' or 'Idle', NOT
    // pressed
    juce::Colour bg;
    bool isMenuOpen = box.isPopupActive();

    if (!box.isEnabled()) {
      bg = juce::Colours::black.withAlpha(0.1f);
    } else if (isButtonDown || isMenuOpen) {
      // Active State -> Clearly Darkest (0.6f)
      bg = juce::Colours::black.withAlpha(0.6f);
    } else if (box.isMouseOver()) {
      // Hover -> Slightly Darker (0.45f)
      bg = juce::Colours::black.withAlpha(0.45f);
    } else {
      // Idle -> Dark by default (0.35f) as requested
      bg = juce::Colours::black.withAlpha(0.35f);
    }

    g.setColour(bg);
    g.fillRoundedRectangle(bounds, radius);

    // Explicit border
    g.setColour(colors.controlBorder);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);

    // Smaller Chevron
    auto arrowZone = bounds.removeFromRight(scaled(24.0f));
    g.setFont(getIconFont(8.0f));
    g.setColour(colors.textSecondary);
    g.drawText(L"\uE70D", arrowZone.toNearestInt(),
               juce::Justification::centred, false);
  }

  juce::Font getComboBoxFont(juce::ComboBox &) override {
    return getDefaultFont(12.0f); // Smaller font to avoid truncation
  }

  int getMenuWindowFlags() override {
    return juce::ComponentPeer::windowIsSemiTransparent; // No drop shadow to
                                                         // avoid corner
                                                         // artifacts
  }

  juce::Font getPopupMenuFont() override {
    return getDefaultFont(13.0f); // Standard size for menus
  }

  // === Popup Menu ===
  void drawPopupMenuBackground(juce::Graphics &g, int width,
                               int height) override {
    // Clear corner artifacts
    g.fillAll(juce::Colours::transparentBlack);

    float radius = scaled(8.0f);
    // Menu background: 80% opacity black for a nice glass feel
    g.setColour(juce::Colours::black.withAlpha(0.8f));
    g.fillRoundedRectangle(0.0f, 0.0f, (float)width, (float)height, radius);

    // Subtle border
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

    // Reserve less space for checkmark or none if not needed
    auto checkArea = r.removeFromLeft(scaledInt(20));

    if (isTicked) {
      g.setColour(isActive ? colors.accentPrimary : colors.textDisabled);
      g.setFont(getIconFont(10.0f));
      g.drawText(L"\uE73E", checkArea, juce::Justification::centred, false);
    }

    // Draw text with less padding
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

    // Keep within parent bounds
    return bounds.constrainedWithin(parentArea.reduced(edgeGap));
  }

  void drawTooltip(juce::Graphics &g, const juce::String &text, int width,
                   int height) override {
    // CRITICAL for transparency
    g.fillAll(juce::Colours::transparentBlack);

    juce::Rectangle<float> bounds(0, 0, (float)width, (float)height);

    // Clean, rounded rectangle background
    // Because we setOpaque(false) in MainContentComponent, this will be the
    // ONLY background.
    g.setColour(juce::Colour(0xE6202020)); // High opacity dark background (90%)
    g.fillRoundedRectangle(bounds, 6.0f);

    // Subtle border for definition
    g.setColour(juce::Colours::white.withAlpha(0.1f));
    g.drawRoundedRectangle(bounds, 6.0f, 1.0f);

    // Text
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

    // Background (Solid dark for alert content)
    g.setColour(colors.background);
    g.fillRoundedRectangle(bounds, radius);

    // Border
    g.setColour(colors.cardBorder);
    g.drawRoundedRectangle(bounds.reduced(0.5f), radius, 1.0f);

    // Text drawing (standard layout)
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

  // === DocumentWindow (Custom Frames for Windows 8/10/11) ===
  void drawDocumentWindowTitleBar(juce::DocumentWindow &window,
                                  juce::Graphics &g, int w, int h,
                                  int titleSpaceX, int titleSpaceW,
                                  const juce::Image *icon,
                                  bool drawTitleTextOnLeft) override {
    auto bounds = juce::Rectangle<int>(0, 0, w, h).toFloat();

    // Background (Match dark theme, slightly translucent overlay)
    g.setColour(colors.cardBackground);
    g.fillAll();

    // Title text
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

  // DPI scale factor cache
  mutable juce::int64 lastScaleUpdate = 0;
  mutable float cachedScaleFactor = 1.0f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(FluentLookAndFeel)
};
