#pragma once

#include <juce_gui_extra/juce_gui_extra.h>

#if JUCE_WINDOWS

// 前置声明 Windows 类型，避免引入 windows.h 后污染 JUCE 头文件。
extern "C" {
typedef void *HWND;
typedef long HRESULT;
typedef unsigned long DWORD;
typedef int BOOL;

__declspec(dllimport) HRESULT __stdcall
DwmSetWindowAttribute(HWND hwnd, DWORD dwAttribute, const void *pvAttribute,
                      DWORD cbAttribute);
}

/**
    Windows 11 专用窗口样式辅助函数。

    封装 DWM 深色模式、Mica 背景和圆角窗口属性。
*/
namespace Win11Helpers {

namespace DwmAttributes {
constexpr DWORD DWMWA_USE_IMMERSIVE_DARK_MODE = 20;
constexpr DWORD DWMWA_WINDOW_CORNER_PREFERENCE = 33;
constexpr DWORD DWMWA_SYSTEMBACKDROP_TYPE = 38;

constexpr int DWMSBT_AUTO = 0;
constexpr int DWMSBT_NONE = 1;
constexpr int DWMSBT_MAINWINDOW = 2;      // Mica
constexpr int DWMSBT_TRANSIENTWINDOW = 3; // Acrylic
constexpr int DWMSBT_TABBEDWINDOW = 4;    // Mica Alt

constexpr int DWMWCP_DEFAULT = 0;
constexpr int DWMWCP_DONOTROUND = 1;
constexpr int DWMWCP_ROUND = 2;
constexpr int DWMWCP_ROUNDSMALL = 3;
} // namespace DwmAttributes

/**
    检测当前系统是否为 Windows 11 或更高版本。
*/
inline bool isWindows11OrLater() {
  auto osType = juce::SystemStats::getOperatingSystemType();
  if (osType >= juce::SystemStats::Windows10) {
    juce::String osName = juce::SystemStats::getOperatingSystemName();
    int buildStart = osName.indexOf("Build ");
    if (buildStart >= 0) {
      int buildNum = osName.substring(buildStart + 6).getIntValue();
      return buildNum >= 22000;
    }
    return osName.containsIgnoreCase("Windows 11");
  }
  return false;
}

/**
    应用 Windows 11 样式：Mica、深色模式和圆角。
*/
inline int applyWin11Style(juce::Component *component,
                           bool useDarkMode = true) {
  if (component == nullptr)
    return 0;
  auto *peer = component->getPeer();
  if (peer == nullptr)
    return 0;
  HWND hwnd = (HWND)peer->getNativeHandle();
  if (hwnd == nullptr)
    return 0;

  int appliedCount = 0;

  BOOL darkMode = useDarkMode ? 1 : 0;
  if (DwmSetWindowAttribute(hwnd, DwmAttributes::DWMWA_USE_IMMERSIVE_DARK_MODE,
                            &darkMode, sizeof(darkMode)) >= 0)
    appliedCount++;

  if (isWindows11OrLater()) {
    int backdropType = DwmAttributes::DWMSBT_MAINWINDOW;
    if (DwmSetWindowAttribute(hwnd, DwmAttributes::DWMWA_SYSTEMBACKDROP_TYPE,
                              &backdropType, sizeof(backdropType)) >= 0)
      appliedCount++;

    int cornerPreference = DwmAttributes::DWMWCP_ROUND;
    if (DwmSetWindowAttribute(hwnd,
                              DwmAttributes::DWMWA_WINDOW_CORNER_PREFERENCE,
                              &cornerPreference, sizeof(cornerPreference)) >= 0)
      appliedCount++;
  }

  return appliedCount;
}

/**
    动态更新深色模式状态。
*/
inline void updateDarkMode(juce::Component *component, bool useDarkMode) {
  if (component == nullptr)
    return;
  auto *peer = component->getPeer();
  if (peer == nullptr)
    return;
  HWND hwnd = (HWND)peer->getNativeHandle();
  if (hwnd == nullptr)
    return;

  BOOL darkMode = useDarkMode ? 1 : 0;
  DwmSetWindowAttribute(hwnd, DwmAttributes::DWMWA_USE_IMMERSIVE_DARK_MODE,
                        &darkMode, sizeof(darkMode));
}

/**
    移除 Windows 11 窗口效果。
*/
inline void removeWin11Style(juce::Component *component) {
  if (component == nullptr)
    return;
  auto *peer = component->getPeer();
  if (peer == nullptr)
    return;
  HWND hwnd = (HWND)peer->getNativeHandle();
  if (hwnd == nullptr)
    return;

  int backdropType = DwmAttributes::DWMSBT_NONE;
  DwmSetWindowAttribute(hwnd, DwmAttributes::DWMWA_SYSTEMBACKDROP_TYPE,
                        &backdropType, sizeof(backdropType));

  int cornerPreference = DwmAttributes::DWMWCP_DONOTROUND;
  DwmSetWindowAttribute(hwnd, DwmAttributes::DWMWA_WINDOW_CORNER_PREFERENCE,
                        &cornerPreference, sizeof(cornerPreference));
}

} // namespace Win11Helpers

#else
namespace Win11Helpers {
inline bool isWindows11OrLater() { return false; }
inline int applyWin11Style(juce::Component *, bool = true) { return 0; }
inline void updateDarkMode(juce::Component *, bool) {}
inline void removeWin11Style(juce::Component *) {}
} // namespace Win11Helpers
#endif
