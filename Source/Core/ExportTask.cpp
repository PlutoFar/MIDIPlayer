#include "CoreImpl.h"

// Responsibilities: 独占导出、曲目切换及播放现场恢复；文件编码交给 `OfflineRenderer`。
// Concurrency: `runExport` 在调用线程同步执行，界面只通过进度/取消回调交换任务状态。
// Ordering: 持有 `exportMutex` 至恢复结束；`stateMutex` 仅覆盖元数据访问，不覆盖编码过程。

namespace midi {

Core::Impl::ExportPlaybackState Core::Impl::captureExportPlaybackState() {
  StateLock lock(stateMutex);
  // Ordering: 在切换导出曲目前保存秒级位置，并递增 `trackSwitchGeneration` 取消旧播放请求。
  ExportPlaybackState state;
  state.trackIndex = currentTrackIndex;
  state.file = currentMidiFile;
  state.positionSeconds =
      engine.getMidiPlayer().getPositionInSamples() / sampleRate();
  state.wasPlaying = engine.getMidiPlayer().getPlaying();
  ++trackSwitchGeneration;

  isHandlingTrackEnd = false;
  engine.getMidiPlayer().setPlaying(false);
  return state;
}

juce::Result
Core::Impl::restoreExportPlaybackState(const ExportPlaybackState &state) {
  StateLock lock(stateMutex);
  // Ordering: 恢复曲目之前再次递增代次；恢复完成不重新启用已经失效的切曲回调。
  ++trackSwitchGeneration;
  engine.getMidiPlayer().setPlaying(false);

  if (state.file != juce::File{}) {
    if (!state.file.existsAsFile() || !loadMidi(state.file)) {
      currentTrackIndex = -1;
      currentMidiFile = {};
      currentMidiName = {};
      engine.getMidiPlayer().setSequence(nullptr, sampleRate());
      isHandlingTrackEnd = false;
      return juce::Result::fail(L"无法重新加载原曲目: " +
                                state.file.getFullPathName());
    }
  } else {
    engine.getMidiPlayer().setSequence(
        std::make_unique<juce::MidiMessageSequence>(), sampleRate());
    currentMidiFile = {};
    currentMidiName = {};
  }

  currentTrackIndex = state.trackIndex;
  isHandlingTrackEnd = false;

  engine.getMidiPlayer().seekTo(state.positionSeconds * sampleRate(),
                                state.wasPlaying);
  engine.getMidiPlayer().setPlaying(state.wasPlaying);
  return juce::Result::ok();
}

Core::ExportResult Core::Impl::runExport(int trackIndex,
                                         const ExportSettings &settings,
                                         const juce::File &targetFile,
                                         std::function<void(float)> onProgress,
                                         std::function<bool()> shouldCancel) {
  auto &self = *this;
  std::unique_lock<std::mutex> operationLock(self.exportMutex,
                                             std::try_to_lock);
  if (!operationLock.owns_lock()) {
    StateLock lock(self.stateMutex);
    self.exportErrorText = L"已有导出任务正在运行。";
    return ExportResult::Failed;
  }

  // Invariant: `exportActiveFlag` 覆盖捕获、渲染和恢复整个区间，禁止实时控制修改导出现场。
  {
    StateLock lock(self.stateMutex);
    if (self.pluginTaskActive.load() || self.pluginScanActive.load() ||
        self.audioConfigurationActive.load()) {
      self.exportErrorText = L"当前操作尚未结束，无法开始导出。";
      return ExportResult::Failed;
    }
    self.exportActiveFlag.store(true);
    self.exportProgressValue.store(0.0f);
    self.exportCancelledFlag = false;
    self.exportErrorText.clear();
  }

  struct FlagGuard {
    std::atomic<bool> &flag;
    ~FlagGuard() { flag.store(false); }
  } guard{self.exportActiveFlag};

  const auto original = self.captureExportPlaybackState();

  const bool needsSwap = trackIndex != original.trackIndex;
  if (needsSwap) {
    juce::File targetFile;
    {
      StateLock lock(self.stateMutex);
      if (const auto *target = self.playlist.getTrack(trackIndex))
        targetFile = target->file;
    }
    if (targetFile == juce::File{} || !self.loadMidi(targetFile)) {
      self.restoreExportPlaybackState(original);
      StateLock lock(self.stateMutex);
      self.exportErrorText =
          (targetFile == juce::File{})
              ? juce::String(L"未找到待导出的曲目。")
              : juce::String(L"无法加载待导出的 MIDI 文件。");
      return ExportResult::Failed;
    }
  }

  ExportResult result = ExportResult::Failed;
  {
    AudioEngine::OfflineExportSession session(self.engine, settings);
    if (session.isActive()) {
      const bool ok = self.renderer.runOfflineExport(
          targetFile, settings,
          [&self, onProgress](float p) {
            self.exportProgressValue.store(p);
            if (onProgress)
              onProgress(p);
          },
          [shouldCancel]() -> bool { return shouldCancel && shouldCancel(); });

      if (ok) {
        result = ExportResult::Succeeded;
      } else if (self.engine.wasLastExportCancelled()) {
        result = ExportResult::Cancelled;
        StateLock lock(self.stateMutex);
        self.exportCancelledFlag = true;
      } else {
        result = ExportResult::Failed;
      }
      if (!session.finish()) {
        StateLock lock(self.stateMutex);
        self.exportErrorText = self.engine.getLastExportError();
        result = ExportResult::Failed;
      }
    } else {
      StateLock lock(self.stateMutex);
      const auto error = self.engine.getLastExportError();
      self.exportErrorText =
          error.isEmpty() ? juce::String(L"无法初始化离线导出环境。") : error;
    }
  }

  const auto restore = self.restoreExportPlaybackState(original);

  {
    StateLock lock(self.stateMutex);
    if (result == ExportResult::Failed && self.exportErrorText.isEmpty())
      self.exportErrorText = self.engine.getLastExportError();
    if (restore.failed()) {
      if (self.exportErrorText.isNotEmpty())
        self.exportErrorText += L"\n";
      self.exportErrorText += restore.getErrorMessage();
      result = ExportResult::Failed;
    }
  }

  return result;
}

} // namespace midi
