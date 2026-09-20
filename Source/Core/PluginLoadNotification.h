#pragma once

#include <string>
#include <string_view>

namespace midi {

inline constexpr int pluginLoadSuccessToastDurationMs = 3000;

// Contract: 构造加载成功提示的自有文本；空名称使用通用标题，不改变插件或界面状态。
inline std::wstring
makePluginLoadSuccessToastTitle(std::wstring_view pluginName) {
  if (pluginName.empty())
    return L"插件加载成功";

  std::wstring title = L"插件加载成功: ";
  title += pluginName;
  return title;
}

} // namespace midi
