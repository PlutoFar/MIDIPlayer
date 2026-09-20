#pragma once

#include "AudioDeviceState.h"
#include <juce_audio_utils/juce_audio_utils.h>

// Responsibilities: 音频设备枚举、配置、设备回调连接及 XML 持久化。
// Ownership: 持有设备管理器与回调播放器，借用处理器；服务必须先于处理器析构。
// Concurrency: 构造、析构、枚举及修改操作在消息线程执行；状态查询按下面的契约读取。
class AudioDeviceService final : private juce::ChangeListener,
                                 public juce::ChangeBroadcaster {
public:
  // Side effect: 恢复设备或尝试默认设备并连接处理器；无设备时仍构造成功，诊断保存在 `lastError`。
  explicit AudioDeviceService(juce::AudioProcessor &processor);
  ~AudioDeviceService() override;
  // Postconditions: 返回设备属性副本；`rescan=true` 同步重新枚举设备，不转移设备对象所有权。
  AudioDeviceState state(bool rescan);
  // Preconditions: `Core` 已排除导出和插件控制并发；原生控制面板允许嵌套消息循环。
  // Postconditions: 空字符串表示修改与保存成功；错误可能发生在设备已切换之后。
  juce::String setDriver(const juce::String &name);
  juce::String apply(const juce::AudioDeviceManager::AudioDeviceSetup &setup);
  juce::String showControlPanel();
  // Side effect: 向当前输出设备发送 JUCE 测试音；可用性由调用方决定。
  void playTestSound();
  // Concurrency: 以下查询读取原子可用性、构造期标记或加锁诊断，可跨线程使用。
  bool hasDevice() const { return deviceAvailable.load(); }
  bool isFirstRun() const { return firstRun; }
  bool wasRestoredWithFallback() const { return restoredWithFallback; }
  juce::String lastError() const {
    const juce::ScopedLock lock(statusLock);
    return error;
  }

private:
  // Ordering: 临时 XML 写入成功后才替换配置；失败返回路径诊断，不宣告持久化成功。
  juce::String save();
  void restore();
  // Concurrency: 仅消息线程读取实际设备，再发布原子可用性与加锁错误；两者不组成原子快照。
  void publishStatus(const juce::String &diagnostic);
  void changeListenerCallback(juce::ChangeBroadcaster *) override;
  juce::AudioDeviceManager manager;
  juce::AudioProcessorPlayer player;
  std::atomic<bool> deviceAvailable{false};
  mutable juce::CriticalSection statusLock;
  juce::String error;
  bool firstRun = false;
  bool restoredWithFallback = false;
  bool recovering = false;
  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioDeviceService)
};
