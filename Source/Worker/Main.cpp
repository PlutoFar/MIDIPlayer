#include "../PluginBridge/PluginScanProcess.h"
#include "../PluginBridge/PluginWorkerProcess.h"

#include <juce_gui_extra/juce_gui_extra.h>

/**
    Responsibilities: 扫描或插件工作进程入口，不创建桌面主界面及音频设备。
    Preconditions: 父进程传入扫描标志或约定 worker UID；无匹配参数时安排消息循环退出。
    Ownership: 入口持有 `workerInstance`，消息循环结束后释放插件实例和渲染资源。
    Ordering: 断连由工作进程安排卸载及退出，关闭入口再清空全局实例。
*/
class MidiWorkerApplication : public juce::JUCEApplication {
public:
  MidiWorkerApplication() = default;

  const juce::String getApplicationName() override { return "MidiWorker"; }
  const juce::String getApplicationVersion() override { return "1.1.0"; }
  bool moreThanOneInstanceAllowed() override { return true; }

  void initialise(const juce::String &commandLine) override {
    if (PluginBridge::runPluginScanIfRequested(commandLine))
      return;

    if (PluginBridge::runWorkerIfRequested(commandLine))
      return;

    juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
  }

  void shutdown() override { PluginBridge::workerInstance() = nullptr; }

  void systemRequestedQuit() override { quit(); }
};

START_JUCE_APPLICATION(MidiWorkerApplication)
