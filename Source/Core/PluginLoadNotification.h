#pragma once

#include <string>
#include <string_view>

namespace midi {

inline constexpr int pluginLoadSuccessToastDurationMs = 3000;

inline std::wstring
makePluginLoadSuccessToastTitle(std::wstring_view pluginName) {
  if (pluginName.empty())
    return L"插件加载成功";

  std::wstring title = L"插件加载成功: ";
  title += pluginName;
  return title;
}

} // namespace midi
