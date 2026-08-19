#pragma once

#include <cstdint>
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
__declspec(dllimport) HRESULT __stdcall DwmFlush();
__declspec(dllimport) BOOL __stdcall ShowWindow(HWND hwnd, int command);
}

namespace Win11Helpers {

struct NativeWindowRect {
  long left = 0;
  long top = 0;
  long right = 0;
  long bottom = 0;
};

namespace DwmAttributes {
constexpr DWORD DWMWA_TRANSITIONS_FORCEDISABLED = 3;
constexpr DWORD DWMWA_CLOAK = 13;
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

namespace CompositionAttributes {
constexpr int WCA_ACCENT_POLICY = 19;
constexpr int ACCENT_DISABLED = 0;
constexpr int ACCENT_ENABLE_TRANSPARENTGRADIENT = 2;
constexpr int ACCENT_ENABLE_BLURBEHIND = 3;
constexpr int ACCENT_ENABLE_ACRYLICBLURBEHIND = 4;

struct AccentPolicy {
  int state = ACCENT_DISABLED;
  int flags = 0;
  DWORD gradientColor = 0;
  int animationId = 0;
};

struct WindowCompositionAttributeData {
  int attribute = WCA_ACCENT_POLICY;
  void *data = nullptr;
  size_t size = 0;
};
} // namespace CompositionAttributes

inline BOOL setWindowCompositionAttribute(HWND hwnd, void *data) {
  using Function = BOOL(__stdcall *)(HWND, void *);
  static juce::DynamicLibrary user32("user32.dll");
  static auto function = reinterpret_cast<Function>(
      user32.getFunction("SetWindowCompositionAttribute"));
  return function != nullptr ? function(hwnd, data) : 0;
}

inline bool setOwnedWindow(juce::Component *window, juce::Component *owner) {
  if (window == nullptr || owner == nullptr || window->getPeer() == nullptr ||
      owner->getPeer() == nullptr)
    return false;

  auto windowHandle = static_cast<HWND>(window->getPeer()->getNativeHandle());
  auto ownerHandle = static_cast<HWND>(owner->getPeer()->getNativeHandle());
  if (windowHandle == nullptr || ownerHandle == nullptr ||
      windowHandle == ownerHandle)
    return false;

  using Function = std::intptr_t(__stdcall *)(HWND, int, std::intptr_t);
  static juce::DynamicLibrary user32("user32.dll");
  static auto function = reinterpret_cast<Function>(
      user32.getFunction("SetWindowLongPtrW"));
  if (function == nullptr)
    return false;

  constexpr int ownerWindowIndex = -8; // GWLP_HWNDPARENT for top-level windows.
  function(windowHandle, ownerWindowIndex,
           reinterpret_cast<std::intptr_t>(ownerHandle));
  return true;
}

inline bool centreWindowOnOwner(juce::Component *window,
                                juce::Component *owner) {
  if (window == nullptr || owner == nullptr || window->getPeer() == nullptr ||
      owner->getPeer() == nullptr)
    return false;

  auto windowHandle = static_cast<HWND>(window->getPeer()->getNativeHandle());
  auto ownerHandle = static_cast<HWND>(owner->getPeer()->getNativeHandle());
  if (windowHandle == nullptr || ownerHandle == nullptr)
    return false;

  using GetWindowRectFunction = BOOL(__stdcall *)(HWND, NativeWindowRect *);
  using SetWindowPosFunction =
      BOOL(__stdcall *)(HWND, HWND, int, int, int, int, unsigned int);
  static juce::DynamicLibrary user32("user32.dll");
  static auto getWindowRect = reinterpret_cast<GetWindowRectFunction>(
      user32.getFunction("GetWindowRect"));
  static auto setWindowPos = reinterpret_cast<SetWindowPosFunction>(
      user32.getFunction("SetWindowPos"));
  if (getWindowRect == nullptr || setWindowPos == nullptr)
    return false;

  NativeWindowRect ownerRect;
  NativeWindowRect windowRect;
  if (getWindowRect(ownerHandle, &ownerRect) == 0 ||
      getWindowRect(windowHandle, &windowRect) == 0)
    return false;

  const int windowWidth = static_cast<int>(windowRect.right - windowRect.left);
  const int windowHeight = static_cast<int>(windowRect.bottom - windowRect.top);
  const int ownerWidth = static_cast<int>(ownerRect.right - ownerRect.left);
  const int ownerHeight = static_cast<int>(ownerRect.bottom - ownerRect.top);
  const int x = static_cast<int>(ownerRect.left) +
                (ownerWidth - windowWidth) / 2;
  const int y = static_cast<int>(ownerRect.top) +
                (ownerHeight - windowHeight) / 2;

  constexpr unsigned int noSize = 0x0001;
  constexpr unsigned int noZOrder = 0x0004;
  constexpr unsigned int noActivate = 0x0010;
  constexpr unsigned int noOwnerZOrder = 0x0200;
  return setWindowPos(windowHandle, nullptr, x, y, 0, 0,
                      noSize | noZOrder | noActivate | noOwnerZOrder) != 0;
}

inline bool hideNativeWindowAndFlush(juce::Component *component) {
  if (component == nullptr || component->getPeer() == nullptr)
    return false;
  auto hwnd = static_cast<HWND>(component->getPeer()->getNativeHandle());
  if (hwnd == nullptr)
    return false;

  BOOL cloak = 1;
  DwmSetWindowAttribute(hwnd, DwmAttributes::DWMWA_CLOAK, &cloak,
                        sizeof(cloak));
  constexpr int hideWindow = 0; // SW_HIDE
  ShowWindow(hwnd, hideWindow);
  DwmFlush();
  return true;
}

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
    为自绘标题栏对话框保留深色边框和圆角，但不启用系统背景材质。
*/
inline int applyFluentDialogStyle(juce::Component *component,
                                  bool useDarkMode = true,
                                  bool useDwmRoundedCorners = false) {
  if (component == nullptr)
    return 0;
  auto *peer = component->getPeer();
  if (peer == nullptr)
    return 0;
  HWND hwnd = (HWND)peer->getNativeHandle();
  if (hwnd == nullptr)
    return 0;

  int appliedCount = 0;
  BOOL transitionsDisabled = 1;
  if (DwmSetWindowAttribute(
          hwnd, DwmAttributes::DWMWA_TRANSITIONS_FORCEDISABLED,
          &transitionsDisabled, sizeof(transitionsDisabled)) >= 0)
    appliedCount++;

  BOOL darkMode = useDarkMode ? 1 : 0;
  if (DwmSetWindowAttribute(hwnd, DwmAttributes::DWMWA_USE_IMMERSIVE_DARK_MODE,
                            &darkMode, sizeof(darkMode)) >= 0)
    appliedCount++;

  if (isWindows11OrLater()) {
    int backdropType = DwmAttributes::DWMSBT_NONE;
    if (DwmSetWindowAttribute(hwnd, DwmAttributes::DWMWA_SYSTEMBACKDROP_TYPE,
                              &backdropType, sizeof(backdropType)) >= 0)
      appliedCount++;

    int cornerPreference =
        useDwmRoundedCorners ? DwmAttributes::DWMWCP_ROUND
                             : DwmAttributes::DWMWCP_DONOTROUND;
    if (DwmSetWindowAttribute(hwnd,
                              DwmAttributes::DWMWA_WINDOW_CORNER_PREFERENCE,
                              &cornerPreference, sizeof(cornerPreference)) >= 0)
      appliedCount++;
  }

  return appliedCount;
}

inline int clearDialogMaterial(juce::Component *component) {
  if (component == nullptr || component->getPeer() == nullptr)
    return 0;
  auto hwnd = static_cast<HWND>(component->getPeer()->getNativeHandle());
  if (hwnd == nullptr)
    return 0;

  int appliedCount = 0;
  int backdropType = DwmAttributes::DWMSBT_NONE;
  if (DwmSetWindowAttribute(hwnd, DwmAttributes::DWMWA_SYSTEMBACKDROP_TYPE,
                            &backdropType, sizeof(backdropType)) >= 0)
    appliedCount++;

  CompositionAttributes::AccentPolicy policy;
  policy.state = CompositionAttributes::ACCENT_DISABLED;
  CompositionAttributes::WindowCompositionAttributeData data;
  data.data = &policy;
  data.size = sizeof(policy);
  if (setWindowCompositionAttribute(hwnd, &data) != 0)
    appliedCount++;
  return appliedCount;
}

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

  CompositionAttributes::AccentPolicy policy;
  CompositionAttributes::WindowCompositionAttributeData data;
  data.data = &policy;
  data.size = sizeof(policy);
  setWindowCompositionAttribute(hwnd, &data);
}

} // namespace Win11Helpers

#else
namespace Win11Helpers {
inline bool isWindows11OrLater() { return false; }
inline bool setOwnedWindow(juce::Component *, juce::Component *) {
  return false;
}
inline bool centreWindowOnOwner(juce::Component *window,
                                juce::Component *owner) {
  if (window == nullptr || owner == nullptr)
    return false;
  window->centreAroundComponent(owner, window->getWidth(),
                                window->getHeight());
  return true;
}
inline bool hideNativeWindowAndFlush(juce::Component *) { return false; }
inline int applyWin11Style(juce::Component *, bool = true) { return 0; }
inline int applyFluentDialogStyle(juce::Component *, bool = true,
                                  bool = false) {
  return 0;
}
inline int clearDialogMaterial(juce::Component *) { return 0; }
inline void updateDarkMode(juce::Component *, bool) {}
inline void removeWin11Style(juce::Component *) {}
} // namespace Win11Helpers
#endif
