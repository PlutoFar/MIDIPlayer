#pragma once

#include <juce_core/juce_core.h>

namespace midi {

/**
    解析 VST3 插件 worker 子进程要启动的可执行文件。

    优先返回与当前可执行文件同目录下的 `MidiWorker.exe`（WinUI / portable
    布局）；若不存在，则回退为重启当前可执行文件自身作为 worker（Legacy 单
    exe 布局，也是历史行为）。两种布局都通过命令行 UID
    (`PluginBridge::workerCommandLineUid`) 进入 worker 分支。

    见 docs/winui3-refactor-plan.md 的 WorkerPath 一节。
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
