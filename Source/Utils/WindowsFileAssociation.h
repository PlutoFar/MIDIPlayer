#pragma once

#include <juce_core/juce_core.h>

inline juce::String getMidiFileProgId() {
  return "ModernMidiPlayer.MIDIFile";
}

inline juce::String makeMidiFileAssociationCommand(const juce::String &exePath) {
  return exePath.quoted() + " \"%1\"";
}

// Contract: 两种扩展名的 Shell 默认打开程序都必须匹配本次运行的可执行文件完整路径。
bool isMidiFileAssociatedToSelf();

// Side effect: 注册当前用户的打开方式和默认应用候选项；默认选择由 Windows 管理。
// Failures: 返回失败项及 Windows 错误码；此前成功的注册项保留。
juce::Result registerMidiFileAssociation();
