#include "../PluginBridge/PluginScanProcess.h"
#include "../PluginBridge/PluginWorkerProcess.h"

#include <juce_gui_extra/juce_gui_extra.h>

/**
    MidiWorker.exe —— VST3 插件 worker 子进程入口。

    由主程序（WinUI 的 MidiPlayer.exe 或 Legacy 的 MidiLegacy.exe）通过
    juce::ChildProcessCoordinator 启动，命令行带 worker UID。本进程只承载
    插件实例、editor 和渲染；不加载主 UI、播放列表或音频设备。

    若未带 worker 命令行（被直接双击运行），立即退出。worker 与
    coordinator 断连时由 PluginWorkerProcess::handleConnectionLost() 调用
    JUCEApplicationBase::quit() 退出。
*/
class MidiWorkerApplication : public juce::JUCEApplication {
public:
  MidiWorkerApplication() = default;

  const juce::String getApplicationName() override { return "MidiWorker"; }
  const juce::String getApplicationVersion() override { return "1.0.0"; }
  bool moreThanOneInstanceAllowed() override { return true; }

  void initialise(const juce::String &commandLine) override {
    if (PluginBridge::runPluginScanIfRequested(commandLine))
      return;

    if (PluginBridge::runWorkerIfRequested(commandLine))
      return;

    // 非 worker 命令行：没有要做的事。
    juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
  }

  void shutdown() override { PluginBridge::workerInstance() = nullptr; }

  void systemRequestedQuit() override { quit(); }
};

START_JUCE_APPLICATION(MidiWorkerApplication)
