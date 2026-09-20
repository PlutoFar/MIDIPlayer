#include "OfflineRenderer.h"
#include "AudioEngine.h"
#include "ExportAudioProcessing.h"
#include "ExportFormatSupport.h"

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
  auto *rawStream = new juce::FileOutputStream(tempFile.getFile());
  std::unique_ptr<juce::OutputStream> outStream(rawStream);
  if (!rawStream->openedOk())
    return fail(L"无法打开临时导出文件: " +
                rawStream->getStatus().getErrorMessage());

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

  const double totalSamples = engine.midiPlayer.getDurationInSamples();

  // 进度前 90% 对应 MIDI 主体，后 10% 留给尾音；自动尾音最多渲染 60 秒，
  // 并要求连续 0.5 秒低于 0.00001 线性电平后结束，避免混响和释放音被截断。
  int64_t offlineTailSamplesRendered = 0;
  const auto maxFixedTail =
      static_cast<int64_t>(exportSampleRate * settings.fixedTailSeconds);
  const auto maxAutoTail = static_cast<int64_t>(exportSampleRate * 60.0);
  int64_t silentSamples = 0;
  bool finishedSeq = false;
  double currentSample = 0;
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

    if (finishedSeq && !settings.autoTail && maxFixedTail <= 0)
      break;

    buffer.clear();
    midi.clear();

    if (!engine.renderPluginBlock(buffer, midi, exportSampleRate)) {
      result = false;
      const auto pluginError = engine.getLastPluginError();
      engine.lastExportError =
          pluginError.isNotEmpty() ? pluginError : L"插件进程渲染失败。";
      break;
    }
    applyMasterOutputStage(buffer, engine.masterVolume.load(),
                           preserveHeadroom);
    const float unditheredMaxLevel =
        settings.autoTail ? measureExportTailLevel(buffer) : 0.0f;
    if (applyDither)
      applyTpdfDither(buffer, settings.bitDepth, ditherRandom);

    if (!writer->writeFromAudioSampleBuffer(buffer, 0,
                                            buffer.getNumSamples())) {
      result = false;
      engine.lastExportError =
          L"写入音频数据失败，可能是磁盘空间不足或文件不可写。";
      break;
    }
    currentSample += buffer.getNumSamples();

    if (!finishedSeq) {
      if (currentSample <= totalSamples) {
        uint32_t now = juce::Time::getMillisecondCounter();
        if (now - lastCallbackTime > 30) {
          reportProgress((float)(currentSample / totalSamples) * 0.9f);
          lastCallbackTime = now;
        }
      }
      if (engine.midiPlayer.isWaitingForTail() ||
          currentSample >= totalSamples) {
        finishedSeq = true;
      }
    } else {
      if (settings.autoTail) {
        if (unditheredMaxLevel < 0.00001f) {
          silentSamples += buffer.getNumSamples();
          if (silentSamples > exportSampleRate * 0.5)
            break;
        } else {
          silentSamples = 0;
        }
        offlineTailSamplesRendered += buffer.getNumSamples();
        if (offlineTailSamplesRendered >= maxAutoTail)
          break;
        uint32_t now = juce::Time::getMillisecondCounter();
        if (now - lastCallbackTime > 30) {
          reportProgress(0.9f + 0.1f * ((float)offlineTailSamplesRendered /
                                        (float)maxAutoTail));
          lastCallbackTime = now;
        }
      } else {
        offlineTailSamplesRendered += buffer.getNumSamples();
        uint32_t now = juce::Time::getMillisecondCounter();
        if (now - lastCallbackTime > 30 && maxFixedTail > 0) {
          reportProgress(0.9f + 0.1f * ((float)offlineTailSamplesRendered /
                                        (float)maxFixedTail));
          lastCallbackTime = now;
        }
        if (offlineTailSamplesRendered >= maxFixedTail)
          break;
      }
    }
  }

  writer.reset();
  outStream.reset();

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
