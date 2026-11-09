#pragma once

// Responsibilities: 将导出参数和进度窗口转换为 `Core::runExport` 调用。
// Ownership: 借用 `Core` 与窗口所有者；调用方必须等待任务结束后再销毁它们。
// Concurrency: `run` 在工作线程写结果，界面只在 `runThread` 返回后读取结果字段。

#include "../AudioEngine/ExportSettings.h"
#include "../Core/Core.h"

#include <juce_gui_basics/juce_gui_basics.h>

class OfflineExportThread final : public juce::ThreadWithProgressWindow {
public:
  OfflineExportThread(midi::Core &coreRef, std::wstring sourcePath,
                      const juce::File &targetFile,
                      const ExportSettings &exportSettings,
                      juce::Component *ownerComponent)
      : ThreadWithProgressWindow(L"正在导出高保真音频...", true, true, -1,
                                 L"安全取消", ownerComponent),
        core(coreRef),
        request{std::move(sourcePath),
                targetFile.getFullPathName().toWideCharPointer(), exportSettings} {}

  void run() override {
    outcome = core.runExport(
        request, [this](float progressValue) { setProgress(progressValue); },
        [this] { return threadShouldExit(); });
  }

  ExportOutcome outcome{ExportStatus::failed, false, L"无法启动导出任务。"};

private:
  midi::Core &core;
  midi::Core::ExportRequest request;
};
