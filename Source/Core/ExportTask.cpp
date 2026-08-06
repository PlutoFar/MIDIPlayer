#include "CoreImpl.h"

// Core/ExportTask —— 离线导出编排：进入前保存播放现场、可选切到目标曲目、
// 通过 AudioEngine::OfflineExportSession 渲染、结束后恢复播放现场。渲染本身
// 仍由 AudioEngine::runOfflineExport 执行；模态进度窗口留在 UI 层，由调用方
// 提供 onProgress / shouldCancel 回调。runExport 在调用方的工作线程上同步执行。

namespace midi {

Core::Impl::ExportPlaybackState Core::Impl::captureExportPlaybackState() {
  StateLock lock(stateMutex);
  // 导出可能切换到其他曲目；进入导出前保存播放现场并终止待恢复播放。
  // trackSwitchGeneration 同步递增，防止旧的延迟播放回调在导出期间误触发。
  ExportPlaybackState state;
  state.trackIndex = currentTrackIndex;
  state.file = currentMidiFile;
  state.position = engine.getMidiPlayer().getPositionInSamples();
  state.wasPlaying = engine.getMidiPlayer().getPlaying();
  state.hadPendingResume = pendingResumePlayback;
  state.wasHandlingTrackEnd = isHandlingTrackEnd;
  ++trackSwitchGeneration;

  pendingResumePlayback = false;
  isHandlingTrackEnd = false;
  engine.getMidiPlayer().setPlaying(false);
  return state;
}

juce::Result
Core::Impl::restoreExportPlaybackState(const ExportPlaybackState &state) {
  StateLock lock(stateMutex);
  // 导出结束后恢复用户原本的曲目、位置和播放状态。trackSwitchGeneration
  // 递增后，导出过程中排队的旧回调全部过期。
  ++trackSwitchGeneration;
  engine.getMidiPlayer().setPlaying(false);

  if (state.file != juce::File{}) {
    if (!state.file.existsAsFile() || !loadMidi(state.file)) {
      currentTrackIndex = -1;
      pendingResumePlayback = false;
      isHandlingTrackEnd = false;
      notify();
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
  pendingResumePlayback = state.hadPendingResume;
  isHandlingTrackEnd = state.wasHandlingTrackEnd;

  engine.getMidiPlayer().seekTo(state.position, state.wasPlaying);
  engine.getMidiPlayer().setPlaying(state.wasPlaying);
  notify();
  return juce::Result::ok();
}

Core::ExportResult
Core::Impl::runExport(int trackIndex, const ExportSettings &settings,
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

  // 整个导出期间置位 exportActiveFlag：tick() 据此抑制曲目推进与 seek 写入，
  // 避免与捕获/恢复播放现场竞争。
  {
    StateLock lock(self.stateMutex);
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
      const bool ok = self.engine.runOfflineExport(
          targetFile, settings,
          [&self, onProgress](float p) {
            self.exportProgressValue.store(p);
            if (onProgress)
              onProgress(p);
          },
          [shouldCancel]() -> bool {
            return shouldCancel && shouldCancel();
          });

      if (ok) {
        result = ExportResult::Succeeded;
      } else if (self.engine.wasLastExportCancelled()) {
        result = ExportResult::Cancelled;
        StateLock lock(self.stateMutex);
        self.exportCancelledFlag = true;
      } else {
        result = ExportResult::Failed;
      }
    } else {
      StateLock lock(self.stateMutex);
      const auto error = self.engine.getLastExportError();
      self.exportErrorText = error.isEmpty()
                                 ? juce::String(L"无法初始化离线导出环境。")
                                 : error;
    }
  }

  const auto restore = self.restoreExportPlaybackState(original);

  {
    StateLock lock(self.stateMutex);
    if (result == ExportResult::Failed && self.exportErrorText.isEmpty())
      self.exportErrorText = self.engine.getLastExportError();
    if (restore.failed() && self.exportErrorText.isEmpty())
      self.exportErrorText = restore.getErrorMessage();
  }

  return result;
}

} // namespace midi
