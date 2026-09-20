#include "OfflineRenderer.h"
#include "AudioEngine.h"
#include "ExportAudioProcessing.h"
#include "ExportFormatSupport.h"
#include "ExportOutputStream.h"

bool OfflineRenderer::runOfflineExport(
    const juce::File &outputFile, const ExportSettings &settings,
    std::function<void(float)> progressCallback,
    std::function<bool()> shouldCancel) {
  engine.lastExportError.clear();
  engine.lastExportCancelled = false;
  auto fail = [this](const juce::String &message) {
    engine.lastExportError = message;
    return false;
  };

  if (!engine.isOfflineExportActive() || !engine.bridge.isPluginLoaded())
    return fail(L"音频引擎或插件未准备好。");

  const double exportSampleRate = settings.sampleRate;

  juce::AudioFormatManager localFormatManager;
  localFormatManager.registerBasicFormats();
  auto *format = findExportAudioFormat(localFormatManager, settings.formatName);
  if (format == nullptr)
    return fail(L"当前 JUCE 构建不支持导出格式: " + settings.formatName);

  auto parentDir = outputFile.getParentDirectory();
  if (!parentDir.exists() && !parentDir.createDirectory())
    return fail(L"无法创建导出目录: " + parentDir.getFullPathName());

  juce::TemporaryFile tempFile(outputFile);
  auto fileStream = std::make_unique<juce::FileOutputStream>(tempFile.getFile());
  if (!fileStream->openedOk())
    return fail(L"无法打开临时导出文件: " +
                fileStream->getStatus().getErrorMessage());
  auto outputStatus = juce::Result::ok();
  std::unique_ptr<juce::OutputStream> outStream =
      std::make_unique<ExportOutputStream>(*fileStream, outputStatus);

  auto options = createExportWriterOptions(
      exportSampleRate, settings.bitDepth, settings.useFloatingPoint,
      settings.qualityIndex, settings.title);

  std::unique_ptr<juce::AudioFormatWriter> writer =
      format->createWriterFor(outStream, options);

  if (writer == nullptr) {
    return fail(L"无法创建 " + settings.formatName + L" 编码器。");
  }

  // 离线渲染期间由 MIDI 播放器生成事件，worker 进程渲染插件音频。
  engine.midiPlayer.setPlaying(true);

  juce::AudioBuffer<float> buffer(2, exportOfflineBlockSize);
  juce::MidiBuffer midi;
  midi.ensureSize(PluginBridge::SharedBlockLayout::maxMidiBytes);

  const int64_t totalSamples = static_cast<int64_t>(
      std::ceil(engine.midiPlayer.getDurationInSamples()));
  const int latencySamples = engine.bridge.getLatencySamples();

  // 进度前 90% 对应 MIDI 主体，后 10% 留给尾音；自动尾音最多渲染 60 秒，
  // 并要求连续 0.5 秒低于 0.00001 线性电平后结束，避免混响和释放音被截断。
  const auto maxFixedTail =
      static_cast<int64_t>(std::llround(exportSampleRate * settings.fixedTailSeconds));
  const auto maxAutoTail = static_cast<int64_t>(exportSampleRate * 60.0);
  int64_t silentSamples = 0;
  int64_t renderedSamples = 0;
  int64_t writtenSamples = 0;
  const int64_t renderLimit = totalSamples + latencySamples +
      (settings.autoTail ? maxAutoTail : maxFixedTail);
  bool result = true;
  const bool preserveHeadroom = shouldPreserveExportHeadroom(
      settings.formatName, settings.bitDepth, settings.useFloatingPoint);
  const bool applyDither = shouldDitherExport(
      settings.formatName, settings.bitDepth, settings.useFloatingPoint);
  juce::Random ditherRandom;
  uint32_t lastCallbackTime = juce::Time::getMillisecondCounter();

  auto reportProgress = [&](float progress) {
    if (progressCallback)
      progressCallback(juce::jlimit(0.0f, 1.0f, progress));
  };

  while (true) {
    if (shouldCancel && shouldCancel()) {
      result = false;
      engine.lastExportCancelled = true;
      engine.lastExportError = L"导出已取消。";
      break;
    }

    if (renderedSamples >= renderLimit)
      break;

    const int blockSamples = static_cast<int>(juce::jmin<int64_t>(
        exportOfflineBlockSize, renderLimit - renderedSamples));
    buffer.setSize(2, blockSamples, false, false, true);
    buffer.clear();
    midi.clear();

    if (!engine.renderPluginBlock(buffer, midi, exportSampleRate)) {
      result = false;
      const auto pluginError = engine.getLastPluginError();
      engine.lastExportError =
          pluginError.isNotEmpty() ? pluginError : L"插件进程渲染失败。";
      break;
    }
    if (engine.bridge.getLatencySamples() != latencySamples) {
      result = false;
      engine.lastExportError = L"导出期间插件延迟发生变化，请固定插件配置后重新导出。";
      break;
    }
    const int tailStart = static_cast<int>(juce::jlimit<int64_t>(
        0, blockSamples, totalSamples + latencySamples - renderedSamples));
    float tailLevel = 0.0f;
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
      if (tailStart < blockSamples)
        tailLevel = juce::jmax(tailLevel,
                              buffer.getMagnitude(ch, tailStart, blockSamples - tailStart));
    applyMasterOutputStage(buffer, engine.masterVolume.load(), preserveHeadroom);
    if (applyDither)
      applyTpdfDither(buffer, settings.bitDepth, ditherRandom);

    // Ordering: 丢弃插件延迟对应的前导采样；主段、尾音和文件长度分别计量。
    const int skip = static_cast<int>(juce::jlimit<int64_t>(
        0, blockSamples, latencySamples - renderedSamples));
    const int count = blockSamples - skip;
    if (count > 0 && !writer->writeFromAudioSampleBuffer(buffer, skip, count)) {
      result = false;
      engine.lastExportError =
          L"写入音频数据失败，可能是磁盘空间不足或文件不可写。";
      break;
    }
    renderedSamples += blockSamples;
    writtenSamples += count;
    if (settings.autoTail && tailStart < blockSamples) {
      silentSamples = tailLevel < 0.00001f
                          ? silentSamples + blockSamples - tailStart : 0;
      if (silentSamples >= exportSampleRate * 0.5)
        break;
    }
    const auto now = juce::Time::getMillisecondCounter();
    if (now - lastCallbackTime > 30) {
      reportProgress(writtenSamples <= totalSamples
                         ? static_cast<float>(writtenSamples) / totalSamples * 0.9f
                         : 0.9f + 0.1f * static_cast<float>(writtenSamples - totalSamples) /
                                      juce::jmax<int64_t>(1, settings.autoTail ? maxAutoTail : maxFixedTail));
      lastCallbackTime = now;
    }
  }

  writer.reset();
  outStream.reset();
  fileStream->flush();
  if (fileStream->getStatus().failed() && outputStatus.wasOk())
    outputStatus = fileStream->getStatus();
  fileStream.reset();
  if (outputStatus.failed()) {
    if (engine.lastExportError.isNotEmpty())
      engine.lastExportError += L"\n";
    engine.lastExportError += L"音频文件未完成: " + outputStatus.getErrorMessage();
    result = false;
  }

  if (result) {
    if (!tempFile.overwriteTargetFileWithTemporary()) {
      tempFile.deleteTemporaryFile();
      return fail(L"无法替换目标文件，请检查权限或文件是否被占用。");
    }
    reportProgress(1.0f);
  } else {
    tempFile.deleteTemporaryFile();
  }

  return result;
}
