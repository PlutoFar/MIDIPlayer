#pragma once

#include <juce_core/juce_core.h>

namespace midi {

/**
    Contract: 优先选择与主程序同目录的 `MidiWorker.exe`，否则选择当前可执行文件。
    Preconditions: 两种目标都必须实现 `PluginBridge::workerCommandLineUid` 对应入口。
    Postconditions: 只解析路径，不启动进程；文件存在不证明版本匹配或可成功连接。
*/
struct WorkerPath {
  static juce::File resolve() {
    const auto exe =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    const auto dedicated =
        exe.getParentDirectory().getChildFile("MidiWorker.exe");
    if (dedicated.existsAsFile())
      return dedicated;
    return exe;
  }
};

} // namespace midi
