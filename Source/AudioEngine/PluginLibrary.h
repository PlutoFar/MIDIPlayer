#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

// Responsibilities: 插件目录扫描、缓存读取及目录发布；不创建播放插件实例。
// Ownership: 持有 `KnownPluginList`；`Core::stateMutex` 串行化目录读写和借用引用访问。
// Concurrency: 扫描子进程处理临时目录，发布阶段才修改本实例目录。
class PluginLibrary {
public:
  // Side effect: 读取插件缓存；无效缓存尝试隔离为独立文件，目录保持空值。
  PluginLibrary();
  // Preconditions: 调用方持有目录访问锁；复制到独立目录，调用方拥有 `destination`。
  void copyPluginListTo(juce::KnownPluginList &destination) const;
  // Preconditions: `destination` 由扫描调用独占；取消回调不得抛出异常。
  // Failures: 子进程失败、取消或结果 XML 无效时返回 `false` 并记录 `lastError`。
  bool scanPlugins(juce::KnownPluginList &destination,
                   const std::function<bool()> &shouldCancel);
  // Ordering: 缓存替换成功后才发布内存目录；返回 `false` 时保留原目录并提供诊断。
  bool replacePluginList(const juce::KnownPluginList &source);
  // Ownership: 返回借用引用；调用方必须在访问期间继续持有核心目录锁。
  const juce::KnownPluginList &plugins() const { return pluginList; }
  // Concurrency: 返回加锁错误副本，不借用内部字符串。
  juce::String lastError() const {
    const juce::ScopedLock lock(errorLock);
    return error;
  }

private:
  void loadKnownPluginList();
  static void quarantineCorruptPluginCache(const juce::File &file);
  void setError(const juce::String &value) {
    const juce::ScopedLock lock(errorLock);
    error = value;
  }
  juce::KnownPluginList pluginList;
  mutable juce::CriticalSection errorLock;
  juce::String error;
};
