#pragma once

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

namespace PluginBridge {

inline constexpr const char *pluginScanCommandLineFlag = "--midi-plugin-scan";
bool isPluginScanCommandLine(const juce::String &commandLine);
bool runPluginScanIfRequested(const juce::String &commandLine);

// 读取扫描日志和等待进程退出并行执行。成功后才替换目录，source 和 destination 可指向同一对象。
// shouldCancel 在调用线程执行，不能抛出异常。
juce::Result scanPluginListInChildProcess(
    const juce::KnownPluginList &source, juce::KnownPluginList &destination,
    const std::function<bool()> &shouldCancel,
    const juce::File &executableOverride = {});

} // namespace PluginBridge
