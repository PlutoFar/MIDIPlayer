#pragma once

#include "../Utils/UserSettings.h"
#include "CustomLookAndFeel.h"
#include "SidebarAnimationSupport.h"
class FluentLookAndFeel;
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

class NavigationSidebar : public juce::Component, public juce::Timer {
public:
  struct NavItem {
    juce::String id;
    juce::String icon;
    juce::String label;
  };

  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void navigationItemSelected(const juce::String &itemId) = 0;
    virtual void navigationBackgroundClicked() = 0;
    virtual void navigationPinToggled(bool isPinned) = 0;
  };

  NavigationSidebar(FluentLookAndFeel &laf) : fluentLookAndFeel(laf) {
    setOpaque(false);
    setWantsKeyboardFocus(true);
    setMouseClickGrabsKeyboardFocus(false);

    items = {
        {"library", L"\uE8D6", L"乐器库"},
        {"playlist", L"\uE90B", L"音乐列表"},
    };

    // 底部功能项不参与页面选中高亮。
    footerItems = {
        {"pin", L"\uE718", L"置顶"},
        {"fonts", L"\uE8D2", L"字体"},
        {"background", L"\uE91B", L"背景"},
        {"settings", L"\uE713", L"设置"},
    };

    addAndMakeVisible(collapseBtn);
    collapseBtn.setTooltip(L"折叠或展开导航");
    collapseBtn.onClick = [this]() { toggleCollapsed(); };

    isCollapsed = getAppSettings().getSidebarCollapsed();
    animation.reset(isCollapsed);

    isPinned = getAppSettings().getAlwaysOnTop();
  }

  void setListener(Listener *l) { listener = l; }

  void setSelectedItem(const juce::String &itemId) {
    if (selectedItemId != itemId) {
      selectedItemId = itemId;
      ensureAnimating();
      repaint();
    }
  }

  int getPreferredWidth() const {
    return juce::roundToInt(animation.getWidth());
  }

  bool getCollapsed() const { return isCollapsed; }

  bool getPinned() const { return isPinned; }

  void toggleCollapsed() {
    isCollapsed = !isCollapsed;
    getAppSettings().setSidebarCollapsed(isCollapsed);
    animation.setCollapsed(isCollapsed);
    ensureAnimating();
  }

  void timerCallback() override {
    bool needsMore = false;
    bool widthChanged = false;

    const int previousWidth = juce::roundToInt(animation.getWidth());
    if (animation.tick()) {
      needsMore = true;
    }
    widthChanged =
        previousWidth != juce::roundToInt(animation.getWidth());

    for (auto &item : items) {
      float target = (item.id == selectedItemId) ? 1.0f : 0.0f;
      auto it = indicatorHeights.find(item.id);
      float current = (it != indicatorHeights.end()) ? it->second : 0.0f;
      float diff = target - current;
      if (std::abs(diff) > 0.01f) {
        indicatorHeights[item.id] = current + diff * 0.2f;
        needsMore = true;
      } else {
        indicatorHeights[item.id] = target;
      }
    }

    auto animateScales = [&](const std::vector<NavItem> &navItems) {
      for (auto &ni : navItems) {
        float target = 1.0f;
        if (ni.id == pressedItemId)
          target = LegacyDesignTokens::Motion::navigationPressedScale;
        else if (ni.id == hoveredItemId)
          target = LegacyDesignTokens::Motion::navigationHoverScale;

        auto it = iconScales.find(ni.id);
        float current = (it != iconScales.end()) ? it->second : 1.0f;
        float diff = target - current;
        if (std::abs(diff) > 0.005f) {
          iconScales[ni.id] = current + diff * 0.25f;
          needsMore = true;
        } else {
          iconScales[ni.id] = target;
        }
      }
    };
    animateScales(items);
    animateScales(footerItems);

    if (!needsMore)
      stopTimer();

    // 只有宽度变化时才触发布局，避免动画期间反复重排。
    if (widthChanged) {
      if (auto *parent = getParentComponent())
        parent->resized();
    }
    repaint();
  }

  void paint(juce::Graphics &g) override {
    auto &colors = fluentLookAndFeel.getColors();
    g.setColour(colors.sidebarBackground);
    g.fillRect(getLocalBounds());

    g.setColour(colors.cardBorder);
    g.drawVerticalLine(getWidth() - 1, 0.0f, (float)getHeight());
  }

  void resized() override {
    auto area = getLocalBounds();
    const int itemHeight = LegacyDesignTokens::Layout::navigationItemHeight(
        fluentLookAndFeel.getUIFontSize());
    constexpr int collapseHeight =
        LegacyDesignTokens::Layout::navigationCollapseHeight;
    constexpr int topPadding =
        LegacyDesignTokens::Layout::navigationTopPadding;
    constexpr int sidePadding =
        LegacyDesignTokens::Layout::navigationSidePadding;

    auto collapseArea =
        area.removeFromTop(collapseHeight).reduced(sidePadding, 4);
    collapseBtn.setBounds(collapseArea.removeFromLeft(36));

    area.removeFromTop(topPadding);

    itemBounds.clear();
    for (size_t i = 0; i < items.size(); ++i) {
      itemBounds.push_back(
          area.removeFromTop(itemHeight).reduced(sidePadding, 4));
    }

    footerBounds.clear();
    auto footerArea = getLocalBounds().removeFromBottom(
        itemHeight * (int)footerItems.size() + 16);
    footerArea.removeFromTop(8);
    for (size_t i = 0; i < footerItems.size(); ++i) {
      footerBounds.push_back(
          footerArea.removeFromTop(itemHeight).reduced(sidePadding, 4));
    }
  }

  void paintOverChildren(juce::Graphics &g) override {
    auto &colors = fluentLookAndFeel.getColors();

    g.setColour(colors.textPrimary);
    juce::String collapseIcon =
        isCollapsed ? L"\uE76C" : L"\uE76B";
    fluentLookAndFeel.drawIconGlyph(
        g, collapseIcon, collapseBtn.getBounds().toFloat(),
        LegacyDesignTokens::Icon::navigation);

    for (size_t i = 0; i < items.size(); ++i) {
      if (i < itemBounds.size())
        drawMainNavItem(g, items[i], itemBounds[i],
                        items[i].id == selectedItemId,
                        items[i].id == hoveredItemId);
    }

    for (size_t i = 0; i < footerItems.size(); ++i) {
      if (i < footerBounds.size())
        drawFooterItem(g, footerItems[i], footerBounds[i],
                       footerItems[i].id == hoveredItemId);
    }
  }

  void mouseMove(const juce::MouseEvent &e) override {
    auto newHover = getItemAtPoint(e.getPosition());
    if (newHover != hoveredItemId) {
      hoveredItemId = newHover;
      ensureAnimating();
      repaint();
    }
  }

  void mouseExit(const juce::MouseEvent &) override {
    hoveredItemId = "";
    ensureAnimating();
    repaint();
  }

  void mouseDown(const juce::MouseEvent &e) override {
    auto clickedId = getItemAtPoint(e.getPosition());
    if (clickedId.isNotEmpty()) {
      showKeyboardFocus = false;
      focusItemId = clickedId;
      pressedItemId = clickedId;
      ensureAnimating();
      activateItem(clickedId);
    } else {
      if (listener)
        listener->navigationBackgroundClicked();
    }
  }

  void mouseUp(const juce::MouseEvent &) override {
    if (pressedItemId.isNotEmpty()) {
      pressedItemId = "";
      ensureAnimating();
    }
  }

private:
  void drawMainNavItem(juce::Graphics &g, const NavItem &item,
                       juce::Rectangle<int> bounds, bool isSelected,
                       bool isHovered) {
    auto &colors = fluentLookAndFeel.getColors();
    int indicatorWidth = 3;
    int indicatorPadding = 4;

    float indicatorProgress = 0.0f;
    auto it = indicatorHeights.find(item.id);
    if (it != indicatorHeights.end())
      indicatorProgress = it->second;

    if (isSelected) {
      g.setColour(colors.navSelected);
      g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
    } else if (isHovered) {
      g.setColour(colors.navHover);
      g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
    }
    drawKeyboardFocus(g, bounds, item.id);

    // 选中指示条从中心向上下展开。
    if (indicatorProgress > 0.01f) {
      float fullHeight = (float)bounds.getHeight() - 16.0f;
      float animHeight = fullHeight * indicatorProgress;
      float centerY = (float)bounds.getY() + (float)bounds.getHeight() / 2.0f;
      auto indicator = juce::Rectangle<float>(
          (float)bounds.getX() + indicatorPadding, centerY - animHeight / 2.0f,
          (float)indicatorWidth, animHeight);
      g.setColour(colors.accentPrimary.withAlpha(indicatorProgress));
      g.fillRoundedRectangle(indicator, 1.5f);
    }

    constexpr int iconAreaWidth =
        LegacyDesignTokens::Layout::navigationIconSlot;
    auto iconArea = getAnimatedIconArea(
        bounds, indicatorPadding + indicatorWidth + 4);

    float scale = 1.0f;
    auto scaleIt = iconScales.find(item.id);
    if (scaleIt != iconScales.end())
      scale = scaleIt->second;

    g.saveState();
    if (std::abs(scale - 1.0f) > 0.001f) {
      auto center = iconArea.getCentre().toFloat();
      g.addTransform(juce::AffineTransform::translation(-center.x, -center.y)
                         .scaled(scale)
                         .translated(center.x, center.y));
    }
    g.setColour(isSelected ? colors.accentPrimary : colors.textPrimary);
    fluentLookAndFeel.drawIconGlyph(
        g, item.icon, iconArea.toFloat(),
        LegacyDesignTokens::Icon::navigation);
    g.restoreState();

    drawAnimatedLabel(
        g, item.label, bounds,
        indicatorPadding + indicatorWidth + 4 + iconAreaWidth,
        fluentLookAndFeel.getNavigationFont(isSelected));
  }

  void drawFooterItem(juce::Graphics &g, const NavItem &item,
                      juce::Rectangle<int> bounds, bool isHovered) {
    auto &colors = fluentLookAndFeel.getColors();

    if (isHovered) {
      g.setColour(colors.navHover);
      g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
    }
    drawKeyboardFocus(g, bounds, item.id);

    constexpr int iconAreaWidth =
        LegacyDesignTokens::Layout::navigationIconSlot;
    auto iconArea = getAnimatedIconArea(bounds, 8);

    float scale = 1.0f;
    auto scaleIt = iconScales.find(item.id);
    if (scaleIt != iconScales.end())
      scale = scaleIt->second;

    g.saveState();
    if (std::abs(scale - 1.0f) > 0.001f) {
      auto center = iconArea.getCentre().toFloat();
      g.addTransform(juce::AffineTransform::translation(-center.x, -center.y)
                         .scaled(scale)
                         .translated(center.x, center.y));
    }
    if (item.id == "pin") {
      g.setColour(isPinned ? colors.accentPrimary : colors.textPrimary);
      fluentLookAndFeel.drawIconGlyph(
          g, isPinned ? L"\uE840" : L"\uE718", iconArea.toFloat(),
          LegacyDesignTokens::Icon::navigation);
    } else {
      g.setColour(colors.textPrimary);
      fluentLookAndFeel.drawIconGlyph(
          g, item.icon, iconArea.toFloat(),
          LegacyDesignTokens::Icon::navigation);
    }
    g.restoreState();

    drawAnimatedLabel(
        g, item.id == "pin" ? (isPinned ? L"取消置顶" : L"置顶")
                            : item.label,
        bounds, 8 + iconAreaWidth,
        fluentLookAndFeel.getNavigationFont());
  }

  bool keyPressed(const juce::KeyPress &key) override {
    showKeyboardFocus = true;
    const int count = static_cast<int>(items.size() + footerItems.size());
    if (count == 0)
      return false;

    int index = itemIndex(focusItemId);
    if (index < 0)
      index = itemIndex(selectedItemId);
    if (index < 0)
      index = 0;

    if (key == juce::KeyPress::upKey || key == juce::KeyPress::leftKey) {
      index = (index + count - 1) % count;
      focusItemId = itemIdAt(index);
      repaint();
      return true;
    }
    if (key == juce::KeyPress::downKey || key == juce::KeyPress::rightKey) {
      index = (index + 1) % count;
      focusItemId = itemIdAt(index);
      repaint();
      return true;
    }
    if (key == juce::KeyPress::returnKey || key == juce::KeyPress::spaceKey) {
      focusItemId = itemIdAt(index);
      activateItem(focusItemId);
      return true;
    }
    return false;
  }

  void focusGained(FocusChangeType) override {
    showKeyboardFocus = true;
    repaint();
  }
  void focusLost(FocusChangeType) override {
    showKeyboardFocus = false;
    repaint();
  }

  juce::Rectangle<int>
  getAnimatedIconArea(juce::Rectangle<int> bounds,
                      int expandedLeftInset) const {
    constexpr int iconAreaWidth =
        LegacyDesignTokens::Layout::navigationIconSlot;
    const int expandedX = bounds.getX() + expandedLeftInset;
    const int collapsedX =
        bounds.getX() + (bounds.getWidth() - iconAreaWidth) / 2;
    const float progress = animation.getCompactProgress();
    const int x = juce::roundToInt(
        expandedX + (collapsedX - expandedX) * progress);
    return {x, bounds.getY(), iconAreaWidth, bounds.getHeight()};
  }

  void drawAnimatedLabel(juce::Graphics &g, const juce::String &text,
                         juce::Rectangle<int> visibleBounds, int leftInset,
                         const juce::Font &font) {
    const float alpha = animation.getLabelAlpha();
    if (alpha <= 0.01f)
      return;

    auto labelArea = visibleBounds;
    labelArea.setWidth(
        juce::roundToInt(SidebarAnimationController::expandedWidth) - 12);
    labelArea.removeFromLeft(leftInset);
    labelArea.translate(juce::roundToInt((alpha - 1.0f) * 6.0f), 0);

    g.saveState();
    g.reduceClipRegion(visibleBounds);
    g.setFont(font);
    g.setColour(fluentLookAndFeel.getColors().textPrimary
                    .withMultipliedAlpha(alpha));
    g.drawText(text, labelArea.reduced(4, 0),
               juce::Justification::centredLeft, false);
    g.restoreState();
  }

  juce::String getItemAtPoint(juce::Point<int> pos) {
    for (size_t i = 0; i < items.size(); ++i)
      if (i < itemBounds.size() && itemBounds[i].contains(pos))
        return items[i].id;
    for (size_t i = 0; i < footerItems.size(); ++i)
      if (i < footerBounds.size() && footerBounds[i].contains(pos))
        return footerItems[i].id;
    return "";
  }

  int itemIndex(const juce::String &id) const {
    for (size_t i = 0; i < items.size(); ++i)
      if (items[i].id == id)
        return static_cast<int>(i);
    for (size_t i = 0; i < footerItems.size(); ++i)
      if (footerItems[i].id == id)
        return static_cast<int>(items.size() + i);
    return -1;
  }

  juce::String itemIdAt(int index) const {
    if (index >= 0 && index < static_cast<int>(items.size()))
      return items[static_cast<size_t>(index)].id;
    index -= static_cast<int>(items.size());
    if (index >= 0 && index < static_cast<int>(footerItems.size()))
      return footerItems[static_cast<size_t>(index)].id;
    return {};
  }

  void activateItem(const juce::String &id) {
    const int index = itemIndex(id);
    if (index < 0)
      return;
    if (index < static_cast<int>(items.size()))
      selectedItemId = id;

    if (id == "pin") {
      isPinned = !isPinned;
      getAppSettings().setAlwaysOnTop(isPinned);
      if (listener)
        listener->navigationPinToggled(isPinned);
    } else if (listener) {
      listener->navigationItemSelected(id);
    }
    repaint();
  }

  void drawKeyboardFocus(juce::Graphics &g, juce::Rectangle<int> bounds,
                         const juce::String &id) const {
    if (!showKeyboardFocus || !hasKeyboardFocus(false) || focusItemId != id)
      return;
    g.setColour(fluentLookAndFeel.getColors().accentPrimary);
    g.drawRoundedRectangle(bounds.toFloat().reduced(1.0f), 6.0f, 2.0f);
  }

  void ensureAnimating() {
    if (!isTimerRunning())
      startTimerHz(60);
  }

  // 折叠按钮只接收点击，图标由侧边栏统一绘制。
  class TransparentButton : public juce::Button {
  public:
    TransparentButton() : juce::Button("") {}
    void paintButton(juce::Graphics &, bool, bool) override {}
  };

  FluentLookAndFeel &fluentLookAndFeel;
  Listener *listener = nullptr;

  std::vector<NavItem> items;
  std::vector<NavItem> footerItems;
  std::vector<juce::Rectangle<int>> itemBounds;
  std::vector<juce::Rectangle<int>> footerBounds;

  TransparentButton collapseBtn;

  juce::String selectedItemId = "library";
  juce::String hoveredItemId;
  juce::String focusItemId = "library";
  bool showKeyboardFocus = false;

  bool isCollapsed = false;
  bool isPinned = false;
  SidebarAnimationController animation;

  std::map<juce::String, float> indicatorHeights;

  std::map<juce::String, float> iconScales;
  juce::String pressedItemId;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NavigationSidebar)
};
