#include "CoreImpl.h"
#include "../AudioEngine/ExportFormatSupport.h"
#include "../AudioEngine/OfflineRenderer.h"
#include "../Midi/MidiFileLoader.h"

namespace midi {

ExportOutcome Core::runExport(const ExportRequest &request,
                              std::function<void(float)> onProgress,
                              std::function<bool()> shouldCancel) {
  auto fail = [](const juce::String &error) -> ExportOutcome {
    return {ExportStatus::failed, false, error};
  };
  const juce::String sourcePath(request.sourcePath.c_str());
  const juce::String targetPath(request.targetPath.c_str());
  if (!juce::File::isAbsolutePath(sourcePath) ||
      !juce::File::isAbsolutePath(targetPath))
    return fail(L"导出源文件和目标文件必须使用完整路径。");

  const auto &settings = request.settings;
  const auto validation = validateExportFormatSettings(
      settings.formatName, settings.sampleRate, settings.bitDepth,
      settings.useFloatingPoint, settings.qualityIndex);
  if (validation.failed())
    return fail(validation.getErrorMessage());
  if (!std::isfinite(settings.fixedTailSeconds) ||
      settings.fixedTailSeconds < 0.0 ||
      settings.sampleRate * settings.fixedTailSeconds >
          static_cast<double>(std::numeric_limits<int>::max()))
    return fail(L"尾音时长超出可导出的范围。");

  const juce::File sourceFile(sourcePath);
  const juce::File targetFile(targetPath);
  if (targetFile.isDirectory() || targetFile == sourceFile)
    return fail(L"导出目标必须为音频文件，且不能覆盖源 MIDI 文件。");

  auto &core = *impl;
  {
    Impl::StateLock lock(core.stateMutex);
    if (!core.engine.hasPluginLoaded())
      return fail(L"请先加载乐器插件。");
    if (targetFile == core.loadedTrack.source)
      return fail(L"导出目标不能覆盖正在播放的 MIDI 文件。");
    if (!core.audioSession.begin(Impl::Operation::exportAudio))
      return fail(L"当前操作尚未结束，无法开始导出。");
  }
  Impl::SessionOperation exporting{core};

  std::unique_ptr<juce::MidiMessageSequence> sequence;
  if (const auto parsed = readMidiSequence(sourceFile, sequence); parsed.failed())
    return fail(parsed.getErrorMessage());
  if (shouldCancel && shouldCancel())
    return {ExportStatus::cancelled, false, {}};

  AudioEngine::OfflineExportSession session(
      core.engine, std::move(sequence), settings.sampleRate);
  if (const auto prepared = session.preparationResult(); prepared.failed())
    return fail(prepared.getErrorMessage());

  auto outcome = renderOfflineExport(session, targetFile, settings,
                                     std::move(onProgress), std::move(shouldCancel));
  if (const auto restored = session.finish(); restored.failed()) {
    if (outcome.error.isNotEmpty())
      outcome.error += L"\n";
    outcome.error += restored.getErrorMessage();
    outcome.status = ExportStatus::failed;
  }
  return outcome;
}

} // namespace midi
