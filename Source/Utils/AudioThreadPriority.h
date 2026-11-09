#pragma once

#include <juce_core/juce_core.h>

// Responsibilities: 将当前音频线程注册到 Windows 多媒体调度，退出时释放注册。
// Ownership: 对象在同一线程内创建、启用和销毁；普通线程优先级不替代 MMCSS 注册。
class AudioThreadPriority {
public:
  AudioThreadPriority() = default;
  ~AudioThreadPriority();

  // Preconditions: 每个对象仅启用一次；离线导出线程不调用。
  // Failures: 返回原生错误，调用方停止启动并向用户报告。
  [[nodiscard]] juce::Result enableRealtime();

private:
  void *taskHandle = nullptr;
  JUCE_DECLARE_NON_COPYABLE(AudioThreadPriority)
};
