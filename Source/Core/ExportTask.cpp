#include "CoreImpl.h"
#include "../AudioEngine/ExportFormatSupport.h"

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
  juce::File sourceFile;
  {
    StateLock lock(self.stateMutex);
    if (self.pluginTaskActive.load() || self.pluginScanActive.load() ||
        self.audioConfigurationActive.load()) {
      self.exportErrorText = L"当前操作尚未结束，无法开始导出。";
      return ExportResult::Failed;
    }
    // Trust Boundary: 在暂停播放和重配置插件之前验证完整请求，内部渲染使用原始有效参数。
    const auto validation = validateExportFormatSettings(
        settings.formatName, settings.sampleRate, settings.bitDepth,
        settings.useFloatingPoint, settings.qualityIndex);
    if (validation.failed()) {
      self.exportErrorText = validation.getErrorMessage();
      return ExportResult::Failed;
    }
    const double tailSamples = settings.sampleRate * settings.fixedTailSeconds;
    if (!std::isfinite(settings.fixedTailSeconds) ||
        settings.fixedTailSeconds < 0.0 ||
        tailSamples > static_cast<double>(std::numeric_limits<int>::max()) ||
        settings.sampleRate * 60.0 >
            static_cast<double>(std::numeric_limits<int>::max())) {
      self.exportErrorText = L"尾音时长超出可导出的范围。";
      return ExportResult::Failed;
    }
    const auto *track = self.playlist.getTrack(trackIndex);
    if (track == nullptr || !track->file.existsAsFile()) {
      self.exportErrorText = L"未找到待导出的 MIDI 文件。";
      return ExportResult::Failed;
    }
    if (!self.engine.hasPluginLoaded()) {
      self.exportErrorText = L"请先加载乐器插件。";
      return ExportResult::Failed;
    }
    if (targetFile.isDirectory() || targetFile == track->file ||
        targetFile == self.currentMidiFile) {
      self.exportErrorText = L"导出目标必须为音频文件，且不能覆盖源 MIDI 文件。";
      return ExportResult::Failed;
    }
    sourceFile = track->file;
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

  ExportResult result = ExportResult::Failed;
  const bool needsSwap = trackIndex != original.trackIndex ||
                         sourceFile != original.file;
  if (needsSwap && !self.loadMidi(sourceFile)) {
    StateLock lock(self.stateMutex);
    self.exportErrorText = L"无法加载待导出的 MIDI 文件。";
  } else if (!self.engine.getMidiPlayer().hasSequence() ||
             !std::isfinite(self.engine.getMidiPlayer().getDurationInSamples()) ||
             self.engine.getMidiPlayer().getDurationInSamples() <= 0.0) {
    StateLock lock(self.stateMutex);
    self.exportErrorText = L"待导出的 MIDI 没有有效时长。";
  } else {
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
        self.exportErrorText = self.engine.getLastExportError();
      } else {
        result = ExportResult::Failed;
        StateLock lock(self.stateMutex);
        self.exportErrorText = self.engine.getLastExportError();
      }
      if (!session.finish()) {
        StateLock lock(self.stateMutex);
        if (self.exportErrorText.isNotEmpty())
          self.exportErrorText += L"\n";
        self.exportErrorText += self.engine.getLastExportError();
        result = ExportResult::Failed;
      }
    } else {
      StateLock lock(self.stateMutex);
      const auto error = self.engine.getLastExportError();
      self.exportErrorText =
          error.isEmpty() ? juce::String(L"无法初始化离线导出环境。") : error;
    }
  }

  // Ordering: 捕获后的所有失败均经过此恢复点；加载错误和恢复错误同时保留。
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
