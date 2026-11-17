#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <optional>

// Responsibilities: 插件目录扫描、缓存读取及目录发布；不创建播放插件实例。
// Ownership: 持有完整扫描记录；`Core::stateMutex` 串行化目录读写。
// Concurrency: 扫描子进程处理临时目录，发布阶段才修改本实例目录。
class PluginLibrary {
public:
  // Side effect: 读取插件缓存；无效缓存尝试隔离为独立文件，目录保持空值。
  explicit PluginLibrary(juce::File cacheFile);
  // Preconditions: 调用方持有目录访问锁；保留所有扫描记录及更新时间。
  void copyPluginListTo(juce::KnownPluginList &destination) const;
  // Preconditions: `destination` 由扫描调用独占；取消回调不得抛出异常。
  juce::Result savePluginList(const juce::XmlElement &xml) const;
  void publishPluginList(const juce::XmlElement &xml);
  // Ownership: 返回可装载乐器的值快照；读取时调用方持有目录访问锁。
  juce::Array<juce::PluginDescription> instruments() const;
  std::optional<juce::PluginDescription> findInstrument(
      const juce::String &id) const;

private:
  void loadKnownPluginList();
  static void quarantineCorruptPluginCache(const juce::File &file);
  juce::File file;
  juce::KnownPluginList pluginList;
};
