#pragma once

#include "../Utils/UserSettings.h"

// Responsibilities: 显示用户配置保存失败；设置存储不依赖界面控件。
// Preconditions: 消息线程调用；本次运行中的配置已更新，磁盘失败保留内存值。
// Postconditions: 保存失败显示独立诊断，不改变插件、播放或外观操作的执行结果。
inline void saveAppSettingsWithFeedback() {
  const auto result = getAppSettings().saveDetailed();
  if (result.failed())
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, L"设置未保存",
        L"设置已在本次运行中生效，无法保存到文件。\n\n" +
            result.getErrorMessage());
}
