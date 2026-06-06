#pragma once

#include "../Utils/UserSettings.h"
#include "CustomControls.h"
#include "CustomLookAndFeel.h"
class FluentLookAndFeel;
#include <juce_gui_basics/juce_gui_basics.h>
#include <map>

/**
    NavigationSidebar: Collapsible Windows 11 style navigation.

    Features:
    - Collapsible (shows only icons when collapsed)
    - Smooth animation
    - Proper Segoe Fluent Icons rendering
    - Remembers collapsed state
    - Pin (always-on-top) button
    - Collapsed tooltips
    - Animated icon shift on selection
    - Footer click ripple animation
*/
class NavigationSidebar : public juce::Component, public juce::Timer {
public:
  struct NavItem {
    juce::String id;
    juce::String icon; // Segoe Fluent Icons character
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

    // Main navigation items
    items = {
        {"library", L"\uE8D6", L"乐器库"},
        {"playlist", L"\uE90B", L"音乐列表"},
    };

    // Footer items (settings-like, no selection highlight)
    footerItems = {
        {"pin", L"\uE718", L"置顶"},
        {"fonts", L"\uE8D2", L"字体"},
        {"background", L"\uE91B", L"背景"},
        {"settings", L"\uE713", L"设置"},
    };

    // Collapse button
    addAndMakeVisible(collapseBtn);
    collapseBtn.onClick = [this]() { toggleCollapsed(); };

    // Load saved state
    isCollapsed = getAppSettings().getSidebarCollapsed();
    currentWidth = isCollapsed ? collapsedWidth : expandedWidth;
    targetWidth = currentWidth;

    // Load pin state
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

  int getPreferredWidth() const { return (int)currentWidth; }

  bool getCollapsed() const { return isCollapsed; }

  bool getPinned() const { return isPinned; }

  void toggleCollapsed() {
    isCollapsed = !isCollapsed;
    getAppSettings().setSidebarCollapsed(isCollapsed);
    targetWidth = isCollapsed ? collapsedWidth : expandedWidth;
    ensureAnimating();
  }

  void timerCallback() override {
    bool needsMore = false;
    bool widthChanged = false;

    // 1. Sidebar width animation (slower factor for visible collapse/expand)
    float widthDiff = targetWidth - currentWidth;
    if (std::abs(widthDiff) > 0.5f) {
      float prevWidth = currentWidth;
      currentWidth += widthDiff * 0.12f;
      widthChanged = ((int)currentWidth != (int)prevWidth);
      needsMore = true;
    } else if (currentWidth != targetWidth) {
      currentWidth = targetWidth;
      widthChanged = true;
    }

    // 2. Indicator height animation (main items only)
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

    // 3. Icon scale animation (all items: main + footer)
    auto animateScales = [&](const std::vector<NavItem> &navItems) {
      for (auto &ni : navItems) {
        float target = 1.0f;
        if (ni.id == pressedItemId)
          target = 0.85f; // mouseDown: shrink
        else if (ni.id == hoveredItemId)
          target = 1.15f; // hover: grow

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

    // Only trigger parent layout when sidebar width actually changed
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

    // Right border
    g.setColour(colors.cardBorder);
    g.drawVerticalLine(getWidth() - 1, 0.0f, (float)getHeight());
  }

  void resized() override {
    auto area = getLocalBounds();
    int itemHeight = 44;
    int collapseHeight = 40;
    int topPadding = 8;
    int sidePadding = 6;

    // Collapse button at top
    auto collapseArea =
        area.removeFromTop(collapseHeight).reduced(sidePadding, 4);
    collapseBtn.setBounds(collapseArea.removeFromLeft(36));

    area.removeFromTop(topPadding);

    // Main items
    itemBounds.clear();
    for (size_t i = 0; i < items.size(); ++i) {
      itemBounds.push_back(
          area.removeFromTop(itemHeight).reduced(sidePadding, 4));
    }

    // Footer at bottom
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

    // Draw collapse button icon
    g.setFont(fluentLookAndFeel.getIconFont(14.0f));
    g.setColour(colors.textPrimary);
    juce::String collapseIcon =
        isCollapsed ? L"\uE76C" : L"\uE76B"; // ChevronRight / ChevronLeft
    g.drawText(collapseIcon, collapseBtn.getBounds(),
               juce::Justification::centred, false);

    // Draw main nav items (with animated selection)
    for (size_t i = 0; i < items.size(); ++i) {
      if (i < itemBounds.size())
        drawMainNavItem(g, items[i], itemBounds[i],
                        items[i].id == selectedItemId,
                        items[i].id == hoveredItemId);
    }

    // Draw footer items (no selection, with click animation)
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
      // Track pressed state for scale animation (all items)
      pressedItemId = clickedId;
      ensureAnimating();

      // Check if it's a main item for selection
      bool isMainItem = false;
      for (auto &item : items) {
        if (item.id == clickedId) {
          isMainItem = true;
          break;
        }
      }

      if (isMainItem) {
        selectedItemId = clickedId;
        repaint();
      }

      // Handle pin toggle
      if (clickedId == "pin") {
        isPinned = !isPinned;
        getAppSettings().setAlwaysOnTop(isPinned);
        if (listener)
          listener->navigationPinToggled(isPinned);
        repaint();
        return;
      }

      if (listener)
        listener->navigationItemSelected(clickedId);
    } else {
      // Clicked background of sidebar
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
  // 绘制主导航项（指示条高度动画，图标固定位置）
  void drawMainNavItem(juce::Graphics &g, const NavItem &item,
                       juce::Rectangle<int> bounds, bool isSelected,
                       bool isHovered) {
    auto &colors = fluentLookAndFeel.getColors();
    int indicatorWidth = 3;
    int indicatorPadding = 4;

    // 获取指示条动画进度 (0.0 ~ 1.0)
    float indicatorProgress = 0.0f;
    auto it = indicatorHeights.find(item.id);
    if (it != indicatorHeights.end())
      indicatorProgress = it->second;

    // Background
    if (isSelected) {
      g.setColour(colors.navSelected);
      g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
    } else if (isHovered) {
      g.setColour(colors.navHover);
      g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
    }

    // Selection indicator - 从中心向上下展开的动画
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

    // Icon - 固定位置：收起时居中，展开时左对齐
    int iconAreaWidth = 40;
    auto iconArea = bounds;
    if (isCollapsed || currentWidth <= collapsedWidth + 10) {
      // 收起状态：图标居中
      iconArea = bounds.withWidth(iconAreaWidth);
      int centerX = bounds.getX() + (bounds.getWidth() - iconAreaWidth) / 2;
      iconArea.setX(centerX);
    } else {
      // 展开状态：图标左对齐（留出指示条空间）
      iconArea.removeFromLeft(indicatorPadding + indicatorWidth + 4);
      iconArea = iconArea.removeFromLeft(iconAreaWidth);
    }

    // Apply icon scale transform (hover/press animation)
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
    g.setFont(fluentLookAndFeel.getIconFont(16.0f));
    g.setColour(isSelected ? colors.accentPrimary : colors.textPrimary);
    g.drawText(item.icon, iconArea, juce::Justification::centred, false);
    g.restoreState();

    // Label (fade with sidebar width)
    {
      float fadeStart = collapsedWidth + 10.0f;
      float fadeEnd = collapsedWidth + 50.0f;
      float labelAlpha = juce::jlimit(
          0.0f, 1.0f, (currentWidth - fadeStart) / (fadeEnd - fadeStart));
      if (labelAlpha > 0.01f) {
        auto labelArea = bounds;
        labelArea.removeFromLeft(indicatorPadding + indicatorWidth + 4 +
                                 iconAreaWidth);
        g.saveState();
        g.reduceClipRegion(labelArea);
        g.setFont(fluentLookAndFeel.getDefaultFont(14.0f, isSelected));
        g.setColour(colors.textPrimary.withMultipliedAlpha(labelAlpha));
        g.drawText(item.label, labelArea.reduced(4, 0),
                   juce::Justification::centredLeft, true);
        g.restoreState();
      }
    }
  }

  // 绘制底部设置项（无选中态，有点击缩放动画）
  void drawFooterItem(juce::Graphics &g, const NavItem &item,
                      juce::Rectangle<int> bounds, bool isHovered) {
    auto &colors = fluentLookAndFeel.getColors();

    // Hover background
    if (isHovered) {
      g.setColour(colors.navHover);
      g.fillRoundedRectangle(bounds.toFloat(), 6.0f);
    }

    // Icon
    int iconAreaWidth = 40;
    auto iconArea = bounds;
    if (isCollapsed || currentWidth <= collapsedWidth + 10) {
      iconArea = bounds.withWidth(iconAreaWidth);
      int centerX = bounds.getX() + (bounds.getWidth() - iconAreaWidth) / 2;
      iconArea.setX(centerX);
    } else {
      iconArea.removeFromLeft(8);
      iconArea = iconArea.removeFromLeft(iconAreaWidth);
    }

    // Apply icon scale transform (hover/press animation)
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
    g.setFont(fluentLookAndFeel.getIconFont(16.0f));
    // Pin 按钮特殊处理：置顶时使用强调色 + 实心图标
    if (item.id == "pin") {
      g.setColour(isPinned ? colors.accentPrimary : colors.textPrimary);
      g.drawText(isPinned ? L"\uE840" : L"\uE718", iconArea,
                 juce::Justification::centred, false);
    } else {
      g.setColour(colors.textPrimary);
      g.drawText(item.icon, iconArea, juce::Justification::centred, false);
    }
    g.restoreState();

    // Label (fade with sidebar width)
    {
      float fadeStart = collapsedWidth + 10.0f;
      float fadeEnd = collapsedWidth + 50.0f;
      float labelAlpha = juce::jlimit(
          0.0f, 1.0f, (currentWidth - fadeStart) / (fadeEnd - fadeStart));
      if (labelAlpha > 0.01f) {
        auto labelArea = bounds;
        labelArea.removeFromLeft(8 + iconAreaWidth);
        g.saveState();
        g.reduceClipRegion(labelArea);
        g.setFont(fluentLookAndFeel.getDefaultFont(14.0f, false));
        g.setColour(colors.textPrimary.withMultipliedAlpha(labelAlpha));
        // Pin 按钮文字需要反映状态
        if (item.id == "pin") {
          g.drawText(isPinned ? L"取消置顶" : L"置顶", labelArea.reduced(4, 0),
                     juce::Justification::centredLeft, true);
        } else {
          g.drawText(item.label, labelArea.reduced(4, 0),
                     juce::Justification::centredLeft, true);
        }
        g.restoreState();
      }
    }
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

  void ensureAnimating() {
    if (!isTimerRunning())
      startTimerHz(60);
  }

  FluentLookAndFeel &fluentLookAndFeel;
  Listener *listener = nullptr;

  std::vector<NavItem> items;
  std::vector<NavItem> footerItems;
  std::vector<juce::Rectangle<int>> itemBounds;
  std::vector<juce::Rectangle<int>> footerBounds;

  InvisibleButton collapseBtn;

  juce::String selectedItemId = "library";
  juce::String hoveredItemId;

  bool isCollapsed = false;
  bool isPinned = false;
  float currentWidth = 180.0f;
  float targetWidth = 180.0f;
  static constexpr float expandedWidth = 180.0f;
  static constexpr float collapsedWidth = 56.0f;

  // 指示条高度动画 (0.0=隐藏, 1.0=完全显示)
  std::map<juce::String, float> indicatorHeights;

  // Icon scale animation for all items (hover/press)
  std::map<juce::String, float> iconScales;
  juce::String pressedItemId;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(NavigationSidebar)
};
