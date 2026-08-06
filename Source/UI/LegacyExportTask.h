#pragma once

#include "../AudioEngine/AudioEngine.h"
#include "../Core/Core.h"

#include <juce_gui_basics/juce_gui_basics.h>

class OfflineExportThread final : public juce::ThreadWithProgressWindow {
public:
  OfflineExportThread(midi::Core &coreRef, int trackIndexValue,
                      const juce::File &targetFile,
                      const ExportSettings &exportSettings)
      : ThreadWithProgressWindow(L"正在导出高保真音频...", true, true, -1,
                                 L"安全取消"),
        core(coreRef), trackIndex(trackIndexValue), file(targetFile),
        settings(exportSettings) {}

  void run() override {
    midi::Core::ExportRequest request;
    request.trackIndex = trackIndex;
    request.targetPath =
        std::wstring(file.getFullPathName().toWideCharPointer());
    request.formatName = std::wstring(settings.formatName.toWideCharPointer());
    request.sampleRate = settings.sampleRate;
    request.bitDepth = settings.bitDepth;
    request.useFloatingPoint = settings.useFloatingPoint;
    request.autoTail = settings.autoTail;
    request.fixedTailSeconds = settings.fixedTailSeconds;
    request.title = std::wstring(settings.title.toWideCharPointer());
    request.qualityIndex = settings.qualityIndex;

    const auto result = core.runExport(
        request, [this](float progressValue) { setProgress(progressValue); },
        [this] { return threadShouldExit(); });

    exportSucceeded = result == midi::Core::ExportResult::Succeeded;
    exportCancelled = result == midi::Core::ExportResult::Cancelled;
    exportFailed = result == midi::Core::ExportResult::Failed;
    errorMessage = juce::String(core.lastExportError().c_str());
  }

  bool exportSucceeded = false;
  bool exportCancelled = false;
  bool exportFailed = false;
  juce::String errorMessage;

private:
  midi::Core &core;
  int trackIndex = -1;
  juce::File file;
  ExportSettings settings;
};
