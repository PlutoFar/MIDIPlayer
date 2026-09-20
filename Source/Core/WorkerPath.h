#pragma once

#include <juce_core/juce_core.h>

namespace midi {

/**
    解析 VST3 插件 worker 子进程要启动的可执行文件。

    优先返回与当前可执行文件同目录下的 `MidiWorker.exe`（便携发布布局）；
    若不存在，则重启当前可执行文件自身作为 worker（单 exe 布局）。
    两种布局都通过命令行 UID
    (`PluginBridge::workerCommandLineUid`) 进入 worker 分支。
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
