#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../Utils/DebugLogger.h"
#include "../Utils/UserSettings.h"
#include "../Midi/MidiPlayer.h"
#include "../PluginBridge/PluginBridgeClient.h"
#include "../PluginBridge/PluginScanProcess.h"
#include "ExportAudioProcessing.h"
#include "ExportFormatSupport.h"
#include "PluginListSupport.h"
#include <atomic>

/**
    离线导出参数。UI 负责文件路径和交互校验；此结构描述编码、采样率和尾音策略。
*/
struct ExportSettings {
  juce::String formatName = "WAV"; // 编码格式："WAV"、"FLAC" 或 "Ogg Vorbis"
  double sampleRate = 96000.0;      // 导出采样率，必须由目标编码器支持
  int bitDepth = 24;                // 目标位深，必须由对应 JUCE AudioFormat 支持
  bool useFloatingPoint = false;    // 32-bit WAV 和 Ogg Vorbis 使用浮点输入
  bool autoTail = true;             // true 时按实际尾音电平结束，false 时使用 fixedTailSeconds
  double fixedTailSeconds = 3.0;    // 固定尾音长度，仅在 autoTail 为 false 时生效

  juce::String title;               // 可选文件元数据标题
  int qualityIndex = 0;             // FLAC/Ogg Vorbis 的质量或压缩等级索引
};

/**
    核心音频引擎，管理音频设备、路由图（AudioProcessorGraph）和插件扫描。

    设计要点：
    - 使用 AudioProcessorGraph 承载 MIDI 文件播放器、VST3 插件和音频输出。
    - 负责音频设备初始化、插件扫描/加载和离线导出。
    - 运行时 MIDI 来源是内置 MidiPlayer，不连接外部 MIDI 输入。
*/
class AudioEngine : public juce::AudioProcessor,
                    public juce::ChangeListener,
                    public juce::ChangeBroadcaster {
public:
  AudioEngine()
      : AudioProcessor(BusesProperties().withOutput(
            "Output", juce::AudioChannelSet::stereo(), true)) {
    formatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
    pluginList.addChangeListener(this);
    loadKnownPluginList();
    restoreAudioDeviceSettings();
    deviceManager.addChangeListener(this);

    devicePlayer.setProcessor(this);
    deviceManager.addAudioCallback(&devicePlayer);
  }

  void saveAudioDeviceSettings() {
    auto xml = deviceManager.createStateXml();
    if (xml == nullptr) {
      auto *device = deviceManager.getCurrentAudioDevice();
      if (device == nullptr)
        return;

      const auto setup = deviceManager.getAudioDeviceSetup();
      xml = std::make_unique<juce::XmlElement>("DEVICESETUP");
      xml->setAttribute("deviceType", device->getTypeName());
      xml->setAttribute("audioOutputDeviceName", setup.outputDeviceName);
      xml->setAttribute("audioInputDeviceName", setup.inputDeviceName);
      xml->setAttribute("audioDeviceRate", device->getCurrentSampleRate());
      xml->setAttribute("audioDeviceBufferSize",
                        device->getCurrentBufferSizeSamples());
      if (!setup.useDefaultInputChannels)
        xml->setAttribute("audioDeviceInChans",
                          setup.inputChannels.toString(2));
      if (!setup.useDefaultOutputChannels)
        xml->setAttribute("audioDeviceOutChans",
                          setup.outputChannels.toString(2));
    }

    auto file =
        UserSettings::getSettingsDirectory().getChildFile("AudioDevice.xml");
    if (!xml->writeTo(file))
      DBG("Failed to save audio device settings: " + file.getFullPathName());
  }

  void restoreAudioDeviceSettings() {
    auto file =
        UserSettings::getSettingsDirectory().getChildFile("AudioDevice.xml");
    firstRunAudio = !file.existsAsFile();
    juce::String result;

    if (!firstRunAudio) {
      if (auto xml = juce::XmlDocument::parse(file)) {
        result = deviceManager.initialise(0, 2, xml.get(), true);
        if (result.isEmpty() &&
            deviceManager.getCurrentAudioDevice() != nullptr) {
          DBG("Restored audio device from XML");
          return;
        }
      }
      DBG("Failed to restore saved audio device from XML: " + result);
    }

    result = deviceManager.initialiseWithDefaultDevices(0, 2);
    if (result.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr) {
      hadDeviceFallback = !firstRunAudio;
      saveAudioDeviceSettings();
      DBG(firstRunAudio ? "Initialised default audio device on first run"
                        : "Fell back to default audio device");
      return;
    }

    lastInitError = result.isNotEmpty()
                        ? result
                        : juce::String("No audio output device could be opened");
    DBG("Audio device init error: " + lastInitError);
  }

  /**
      进入离线导出状态。

      离线导出直接驱动 worker 渲染，实时 AudioProcessorPlayer 回调会被
      offlineExportActive 静音。插件同步切到 non-realtime，并用导出采样率
      重新 prepareToPlay，避免实时设备配置影响文件渲染。

      调用方必须确保没有并发插件加载/卸载；离线导出会暂停实时处理并重配 graph。
  */
  bool prepareForOfflineExport(const ExportSettings &settings) {
    lastExportError.clear();
    if (!bridge.isPluginLoaded()) {
      lastExportError = L"音频引擎或插件未准备好。";
      return false;
    }

    offlineExportActive.store(true, std::memory_order_release);
    suspendProcessing(true);
    const double exportSampleRate =
        settings.sampleRate > 0.0 ? settings.sampleRate : 44100.0;
    midiPlayer.setPlaying(false);
    midiPlayer.setSampleRate(exportSampleRate);
    midiPlayer.seekTo(0.0);
    if (!bridge.prepare(exportSampleRate, exportOfflineBlockSize, true)) {
      lastExportError = bridge.getLastError();
      offlineExportActive.store(false, std::memory_order_release);
      suspendProcessing(false);
      return false;
    }
    return true;
  }

  /**
      离开离线导出状态，恢复实时设备的采样率、块大小和插件 realtime 模式。
  */
  void restoreFromOfflineExport() {
    auto currentSetup = deviceManager.getAudioDeviceSetup();
    const double liveSampleRate =
        currentSetup.sampleRate > 0.0 ? currentSetup.sampleRate : 44100.0;
    const int liveBlockSize =
        currentSetup.bufferSize > 0 ? currentSetup.bufferSize : 512;
    if (bridge.isPluginLoaded() &&
        !bridge.prepare(liveSampleRate, liveBlockSize, false))
      setLastPluginError(bridge.getLastError());
    midiPlayer.setSampleRate(liveSampleRate);
    midiPlayer.setPlaying(false);
    midiPlayer.seekTo(0.0);
    offlineExportActive.store(false, std::memory_order_release);
    suspendProcessing(false);
  }

  /**
      离线导出 RAII 会话。

      构造时切入离线导出环境，析构时无条件恢复实时环境，保证取消、失败或提前
      return 时不会把 VST3 插件留在 non-realtime 配置里。
  */
  class OfflineExportSession {
  public:
    OfflineExportSession(AudioEngine &owner, const ExportSettings &settings)
        : engine(owner) {
      active = engine.prepareForOfflineExport(settings);
    }

    ~OfflineExportSession() {
      if (active)
        engine.restoreFromOfflineExport();
    }

    OfflineExportSession(const OfflineExportSession &) = delete;
    OfflineExportSession &operator=(const OfflineExportSession &) = delete;

    bool isActive() const { return active; }

  private:
    AudioEngine &engine;
    bool active = false;
  };

  bool isOfflineExportActive() const {
    return offlineExportActive.load(std::memory_order_acquire);
  }

  bool runOfflineExport(const juce::File &outputFile, const ExportSettings &settings,
                        std::function<void(float)> progressCallback,
                        std::function<bool()> shouldCancel) {
    lastExportError.clear();
    lastExportCancelled = false;
    auto fail = [this](const juce::String &message) {
      lastExportError = message;
      return false;
    };

    if (!isOfflineExportActive() || !bridge.isPluginLoaded())
      return fail(L"音频引擎或插件未准备好。");

    const double exportSampleRate =
        settings.sampleRate > 0.0 ? settings.sampleRate : 44100.0;

    juce::AudioFormatManager localFormatManager;
    localFormatManager.registerBasicFormats();
    auto *format = findExportAudioFormat(localFormatManager, settings.formatName);
    if (format == nullptr)
        return fail(L"当前 JUCE 构建不支持导出格式: " + settings.formatName);

    const auto validation = validateExportFormatSettings(
        settings.formatName, exportSampleRate, settings.bitDepth,
        settings.useFloatingPoint, settings.qualityIndex);
    if (validation.failed())
      return fail(validation.getErrorMessage());

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

    std::unique_ptr<juce::AudioFormatWriter> writer = format->createWriterFor(outStream, options);

    if (writer == nullptr) {
        return fail(L"无法创建 " + settings.formatName + L" 编码器。");
    }

    // 离线渲染期间由 MIDI 播放器生成事件，worker 进程渲染插件音频。
    midiPlayer.setPlaying(true);

    juce::AudioBuffer<float> buffer(2, exportOfflineBlockSize);
    juce::MidiBuffer midi;

    double totalSamples = midiPlayer.getDurationInSamples();
    if (totalSamples <= 0) totalSamples = exportSampleRate * 60.0;

    // 进度前 90% 对应 MIDI 主体，后 10% 留给尾音；自动尾音最多渲染 60 秒，
    // 并要求连续 0.5 秒低于 0.00001 线性电平后结束，避免混响和释放音被截断。
    int offlineTailSamplesRendered = 0;
    int maxFixedTail = (int)(exportSampleRate * juce::jmax(0.0, settings.fixedTailSeconds));
    int maxAutoTail = (int)(exportSampleRate * 60.0);
    int silentSamples = 0;
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
            lastExportCancelled = true;
            lastExportError = L"导出已取消。";
            break;
        }

        if (finishedSeq && !settings.autoTail && maxFixedTail <= 0)
            break;

        buffer.clear();
        midi.clear();

        if (!renderPluginBlock(buffer, midi, exportSampleRate)) {
            result = false;
            const auto pluginError = getLastPluginError();
            lastExportError = pluginError.isNotEmpty()
                                  ? pluginError
                                  : L"插件进程渲染失败。";
            break;
        }
        applyMasterOutputStage(buffer, masterVolume.load(), preserveHeadroom);
        const float unditheredMaxLevel =
            settings.autoTail ? measureExportTailLevel(buffer) : 0.0f;
        if (applyDither)
          applyTpdfDither(buffer, settings.bitDepth, ditherRandom);

        if (!writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples())) {
            result = false;
            lastExportError = L"写入音频数据失败，可能是磁盘空间不足或文件不可写。";
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
            if (midiPlayer.isWaitingForTail() || currentSample >= totalSamples) {
                finishedSeq = true;
            }
        } else {
            if (settings.autoTail) {
                if (unditheredMaxLevel < 0.00001f) {
                    silentSamples += buffer.getNumSamples();
                    if (silentSamples > exportSampleRate * 0.5) break;
                } else {
                    silentSamples = 0;
                }
                offlineTailSamplesRendered += buffer.getNumSamples();
                if (offlineTailSamplesRendered >= maxAutoTail) break;
                uint32_t now = juce::Time::getMillisecondCounter();
                if (now - lastCallbackTime > 30) {
                    reportProgress(0.9f + 0.1f * ((float)offlineTailSamplesRendered / (float)maxAutoTail));
                    lastCallbackTime = now;
                }
            } else {
                offlineTailSamplesRendered += buffer.getNumSamples();
                uint32_t now = juce::Time::getMillisecondCounter();
                if (now - lastCallbackTime > 30 && maxFixedTail > 0) {
                    reportProgress(0.9f + 0.1f * ((float)offlineTailSamplesRendered / (float)maxFixedTail));
                    lastCallbackTime = now;
                }
                if (offlineTailSamplesRendered >= maxFixedTail) break;
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

  bool hasAudioDevice() const {
    auto *device = deviceManager.getCurrentAudioDevice();
    return device != nullptr;
  }

  juce::String getLastInitError() const { return lastInitError; }

  juce::String getLastPluginError() const {
    const juce::ScopedLock lock(errorLock);
    return lastPluginError;
  }

  juce::String getLastExportError() const { return lastExportError; }
  juce::String getLastPersistenceError() const { return lastPersistenceError; }
  bool wasLastExportCancelled() const { return lastExportCancelled; }

  bool isFirstRunAudio() const {
    return firstRunAudio;
  }

  bool wasDeviceRestoredWithFallback() const { return hadDeviceFallback; }

  // worker 承载插件实例；调用方负责串行化加载、卸载与导出。
  void unloadPlugin() {
    suspendProcessing(true);
    midiPlayer.setPlaying(false);

    bridge.closeEditor();
    flushPluginStateBeforeUnload();
    bridge.unloadPlugin();
    workerCrashNotified.store(false, std::memory_order_release);

    suspendProcessing(false);
    sendChangeMessage();
  }

  bool hasPluginLoaded() const { return bridge.isPluginLoaded(); }

  juce::String getLoadedPluginName() const { return bridge.getLoadedPluginName(); }

  bool hasPluginWorkerCrashed() const {
    return bridge.getStatus() == PluginBridge::BridgeStatus::crashed;
  }

  juce::String getPluginWorkerError() const { return bridge.getLastError(); }

  void terminateCrashedPluginWorker() {
    suspendProcessing(true);
    bridge.terminateCrashedWorker();
    workerCrashNotified.store(false, std::memory_order_release);
    suspendProcessing(false);
    sendChangeMessage();
  }

  void cancelPendingPluginOperation() { bridge.cancelPendingOperation(); }

  bool openPluginEditor() {
    if (!bridge.openEditor()) {
      setLastPluginError(bridge.getLastError());
      return false;
    }
    return true;
  }

  void closePluginEditor() { bridge.closeEditor(); }

  ~AudioEngine() override {
    suspendProcessing(true);
    midiPlayer.setPlaying(false);
    bridge.closeEditor();
    flushPluginStateBeforeUnload();

    deviceManager.removeChangeListener(this);
    deviceManager.removeAudioCallback(&devicePlayer);
    devicePlayer.setProcessor(nullptr);

    bridge.unloadPlugin();
    bridge.stop();

    saveKnownPluginList();
  }

  void prepareToPlay(double sampleRate, int samplesPerBlock) override {
    midiPlayer.setSampleRate(sampleRate > 0.0 ? sampleRate : 44100.0);
    if (bridge.isPluginLoaded() && !bridge.prepare(sampleRate, samplesPerBlock))
      setLastPluginError(bridge.getLastError());
  }

  void releaseResources() override {}

  void processBlock(juce::AudioBuffer<float> &buffer,
                    juce::MidiBuffer &midiMessages) override {
    juce::ScopedNoDenormals noDenormals;

    if (isOfflineExportActive()) {
      buffer.clear();
      midiMessages.clear();
      return;
    }

    const double renderSampleRate =
        getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    if (!renderPluginBlock(buffer, midiMessages, renderSampleRate)) {
      buffer.clear();
      return;
    }

    const int numSamples = buffer.getNumSamples();
    bool isPlaying = midiPlayer.getPlaying();

    if (midiPlayer.isWaitingForTail()) {
      float blockLevel = 0.0f;
      for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        blockLevel =
            juce::jmax(blockLevel, buffer.getMagnitude(ch, 0, numSamples));

      tailSamplesRendered += numSamples;
      tailSilentSamples =
          blockLevel < 0.00001f ? tailSilentSamples + numSamples : 0;
      const int silenceRequired =
          static_cast<int>(juce::jmax(1.0, renderSampleRate * 0.5));
      const int maximumTail =
          static_cast<int>(juce::jmax(1.0, renderSampleRate * 60.0));
      if (tailSilentSamples >= silenceRequired ||
          tailSamplesRendered >= maximumTail) {
        midiPlayer.finishTail();
        isPlaying = false;
      }
    } else {
      tailSamplesRendered = 0;
      tailSilentSamples = 0;
    }

    // allSoundOff 在图内触发时，VST3 插件会在样本 0 立即切断声部并产生波形不连续。
    // 包含该事件的整块 buffer 静音，下一块从 0 淡入到 1；48 kHz、256 samples
    // 下空隙约 5 ms。
    if (midiPlayer.consumeSeekOccurred()) {
      buffer.clear();
      seekCrossfadePhase = 2;
      seekCrossfadeSamples = seekCrossfadeDuration;
    }

    if (seekCrossfadePhase == 2) {
      int toFade = juce::jmin(seekCrossfadeSamples, numSamples);
      float startGain = 1.0f - (float)seekCrossfadeSamples / (float)seekCrossfadeDuration;
      float endGain = 1.0f - (float)(seekCrossfadeSamples - toFade) / (float)seekCrossfadeDuration;
      buffer.applyGainRamp(0, toFade, startGain, endGain);
      seekCrossfadeSamples -= toFade;
      if (seekCrossfadeSamples <= 0)
        seekCrossfadePhase = 0;
    }

    if (!isPlaying && fadeOutSamples > 0) {
      int samplesToFade = juce::jmin(fadeOutSamples, numSamples);
      float startGain = (float)fadeOutSamples / (float)fadeOutDuration;
      float endGain =
          (float)(fadeOutSamples - samplesToFade) / (float)fadeOutDuration;

      buffer.applyGainRamp(0, samplesToFade, startGain, endGain);

      if (samplesToFade < numSamples) {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
          buffer.clear(ch, samplesToFade, numSamples - samplesToFade);
      }

      fadeOutSamples -= samplesToFade;
    } else if (isPlaying && fadeOutSamples != fadeOutDuration) {
      fadeOutSamples = fadeOutDuration;
      stopCleanupDone = false;
    }

    // 淡出到静音后由 MidiPlayer 释放 VST3 按键、踏板和声部状态；
    // 此时输出已为 0，所以清理事件不会被听到。
    if (!isPlaying && fadeOutSamples <= 0 && !stopCleanupDone) {
      midiPlayer.triggerStopCleanup();
      stopCleanupDone = true;
    }

    if (midiPlayer.consumeCleanupOccurred()) {
      buffer.clear();
    }

    applyMasterOutputStage(buffer, masterVolume.load(), false);
  }

  /**
      同步扫描所有 VST3 插件。调用方用模态进度窗承载等待状态，
      当前扫描循环未向 UI 报告逐插件进度。
  */
  void copyPluginListTo(juce::KnownPluginList &destination) const {
    if (auto xml = pluginList.createXml())
      destination.recreateFromXml(*xml);
  }

  bool scanPlugins(juce::KnownPluginList &destination,
                   const std::function<bool()> &shouldCancel = {}) {
    setLastPluginError({});
    juce::String error;
    const bool succeeded = PluginBridge::scanPluginListInChildProcess(
        destination, destination, shouldCancel, error);
    if (!succeeded)
      setLastPluginError(error);
    return succeeded;
  }

  void replacePluginList(const juce::KnownPluginList &source) {
    if (auto xml = source.createXml())
      pluginList.recreateFromXml(*xml);
    saveKnownPluginList();
    sendChangeMessage();
  }

  void scanDirectory(const juce::File &directory) {
    if (!directory.isDirectory())
      return;

    juce::FileSearchPath searchPath;
    searchPath.add(directory);

    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
      auto *format = formatManager.getFormat(i);
      if (format->getName() == "VST3") {
        juce::PluginDirectoryScanner scanner(pluginList, *format, searchPath,
                                             true, juce::File(), false);

        juce::String name;
        while (scanner.scanNextFile(true, name)) {
        }
      }
    }

    sendChangeMessage();
  }

  void clearPluginList() {
    pluginList.clear();
    sendChangeMessage();
  }

  // worker 承载插件实例；主进程加载路径不创建插件 UI。
  bool loadPlugin(const juce::PluginDescription &description) {
    if (!hasAudioDevice()) {
      setLastPluginError(L"没有可用的音频输出设备，请先在音频设置中选择设备");
      return false;
    }

    suspendProcessing(true);

    midiPlayer.setPlaying(false);
    bridge.closeEditor();
    flushPluginStateBeforeUnload();
    bridge.unloadPlugin();

    LOG_DEBUG("Loading plugin in worker: " + description.name);
    if (!bridge.loadPlugin(description)) {
      setLastPluginError(L"无法加载插件: " + bridge.getLastError());
      suspendProcessing(false);
      return false;
    }

    const double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    const int bs = getBlockSize() > 0 ? getBlockSize() : 512;
    if (!bridge.prepare(sr, bs)) {
      setLastPluginError(bridge.getLastError());
      bridge.unloadPlugin();
      suspendProcessing(false);
      return false;
    }

    suspendProcessing(false);
    setLastPluginError({});
    workerCrashNotified.store(false, std::memory_order_release);
    LOG_DEBUG("Plugin loaded in worker: " + description.name);

    return true;
  }

  void setMasterVolume(float volume) {
    masterVolume.store(juce::jlimit(0.0f, 1.0f, volume));
  }

  float getMasterVolume() const { return masterVolume.load(); }

  juce::File getSettingsDir() {
    auto exeDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();
    // 与 UserSettings::getSettingsDirectory() 的便携标记保持一致。
    bool isPortable = exeDir.getChildFile("portable.dat").existsAsFile() ||
                      exeDir.getChildFile("portable_debug.dat").existsAsFile();

    juce::File settingsDir;
    if (isPortable) {
      settingsDir = exeDir.getChildFile("Settings");
    } else {
      settingsDir = juce::File::getSpecialLocation(
                        juce::File::userApplicationDataDirectory)
                        .getChildFile("ModernMidiPlayer");
    }
    settingsDir.createDirectory();
    return settingsDir;
  }

  void loadKnownPluginList() {
    lastPersistenceError.clear();
    auto xmlFile = getSettingsDir().getChildFile("Plugins.xml");
    if (!xmlFile.existsAsFile())
      return;

    try {
      auto tree = juce::XmlDocument::parse(xmlFile);
      if (tree != nullptr && tree->hasTagName("KNOWNPLUGINS")) {
        pluginList.recreateFromXml(*tree);
        DBG("Loaded " + juce::String(pluginList.getNumTypes()) +
            " plugins from cache");
      } else {
        DBG("Invalid Plugins.xml format: " + xmlFile.getFullPathName());
        lastPersistenceError = "Invalid plugin cache format: " +
                               xmlFile.getFullPathName();
        quarantineCorruptPluginCache(xmlFile);
      }
    } catch (...) {
      DBG("Exception while loading Plugins.xml: " +
          xmlFile.getFullPathName());
      lastPersistenceError = "Exception while loading plugin cache: " +
                             xmlFile.getFullPathName();
      quarantineCorruptPluginCache(xmlFile);
    }
  }

  void saveKnownPluginList() {
    lastPersistenceError.clear();
    auto xmlFile = getSettingsDir().getChildFile("Plugins.xml");

    try {
      auto xml = pluginList.createXml();
      if (xml == nullptr) {
        lastPersistenceError = "Unable to create plugin cache XML";
        return;
      }

      auto backupFile = getSettingsDir().getChildFile("Plugins.xml.bak");
      if (xmlFile.existsAsFile()) {
        if (!xmlFile.copyFileTo(backupFile))
          lastPersistenceError = "Unable to create plugin cache backup: " +
                                 backupFile.getFullPathName();
      }

      auto tempFile = getSettingsDir().getChildFile("Plugins.xml.tmp");
      if (xml->writeTo(tempFile, {})) {
        if (!tempFile.moveFileTo(xmlFile)) {
          lastPersistenceError = "Unable to replace plugin cache: " +
                                 xmlFile.getFullPathName();
          tempFile.deleteFile();
          return;
        }
        backupFile.deleteFile(); // 清理备份
        DBG("Saved " + juce::String(pluginList.getNumTypes()) +
            " plugins to cache");
      } else {
        tempFile.deleteFile();
        lastPersistenceError = "Unable to write plugin cache temp file: " +
                               tempFile.getFullPathName();
        DBG("Failed to write Plugins.xml");
      }
    } catch (...) {
      lastPersistenceError = "Exception while saving plugin cache";
      DBG("Exception while saving plugin list");
    }
  }

  void changeListenerCallback(juce::ChangeBroadcaster *source) override {
    if (source == &deviceManager) {
      if (isRecoveringDevice)
        return; // 防止自动恢复期间递归触发

      if (deviceManager.getCurrentAudioDevice() != nullptr) {
        saveAudioDeviceSettings();
      } else if (UserSettings::getSettingsDirectory()
                     .getChildFile("AudioDevice.xml")
                     .existsAsFile()) {
        isRecoveringDevice = true;
        auto result = deviceManager.initialiseWithDefaultDevices(0, 2);
        isRecoveringDevice = false;

        if (result.isEmpty() &&
            deviceManager.getCurrentAudioDevice() != nullptr) {
          DBG("Auto-recovered to default audio device");
          saveAudioDeviceSettings();
        } else {
          DBG("Failed to auto-recover audio device: " + result);
        }
      }
    }
    sendChangeMessage();
  }

  juce::AudioDeviceManager &getDeviceManager() { return deviceManager; }
  juce::KnownPluginList &getPluginList() { return pluginList; }

  MidiPlayer &getMidiPlayer() { return midiPlayer; }

  const juce::String getName() const override {
    return "ModernMidiPlayerEngine";
  }
  bool acceptsMidi() const override { return true; }
  bool producesMidi() const override { return false; }
  double getTailLengthSeconds() const override { return 0.0; }
  int getNumPrograms() override { return 1; }
  int getCurrentProgram() override { return 0; }
  void setCurrentProgram(int) override {}
  const juce::String getProgramName(int) override { return "None"; }
  void changeProgramName(int, const juce::String &) override {}
  bool hasEditor() const override { return false; }
  juce::AudioProcessorEditor *createEditor() override { return nullptr; }
  void getStateInformation(juce::MemoryBlock &) override {}
  void setStateInformation(const void *, int) override {}

private:
  void setLastPluginError(const juce::String &message) {
    const juce::ScopedLock lock(errorLock);
    lastPluginError = message;
  }

  bool renderPluginBlock(juce::AudioBuffer<float> &buffer,
                         juce::MidiBuffer &midiMessages,
                         double sampleRate) {
    midiMessages.clear();
    buffer.clear();

    if (!bridge.isPluginLoaded()) {
      if (midiPlayer.getPlaying()) {
        midiPlayer.setPlaying(false);
        setLastPluginError(L"插件未加载。");
      }
      return false;
    }

    const int totalSamples = buffer.getNumSamples();
    if (totalSamples <= 0)
      return true;

    midiPlayer.processBlock(midiMessages, totalSamples);
    for (int offset = 0; offset < totalSamples;
         offset += PluginBridge::SharedBlockLayout::maxSamples) {
      const int chunkSamples =
          PluginBridge::getSharedBlockChunkSize(totalSamples, offset);
      juce::AudioBuffer<float> chunk(buffer.getArrayOfWritePointers(),
                                     buffer.getNumChannels(), offset,
                                     chunkSamples);
      if (!bridge.processBlock(midiMessages, chunk, sampleRate, offset))
        break;
      if (offset + chunkSamples >= totalSamples)
        return true;
    }

    if (bridge.getStatus() == PluginBridge::BridgeStatus::crashed) {
      midiPlayer.setPlaying(false);
      setLastPluginError(bridge.getLastError());
      if (!workerCrashNotified.exchange(true, std::memory_order_acq_rel))
        sendChangeMessage();
    }

    return false;
  }

  void flushPluginStateBeforeUnload() {
    if (!bridge.isPluginLoaded())
      return;

    const double sampleRate = getSampleRate() > 0.0 ? getSampleRate() : 44100.0;
    const int flushBlockSize = juce::jlimit(1, 512, getBlockSize() > 0
                                                        ? getBlockSize()
                                                        : 512);
    juce::AudioBuffer<float> buffer(PluginBridge::SharedBlockLayout::maxChannels,
                                    flushBlockSize);
    juce::MidiBuffer midi;

    for (int channel = 1; channel <= 16; ++channel) {
      midi.addEvent(juce::MidiMessage::controllerEvent(channel, 64, 0), 0);
      midi.addEvent(juce::MidiMessage::controllerEvent(channel, 123, 0), 0);
      midi.addEvent(juce::MidiMessage::controllerEvent(channel, 120, 0), 0);
    }

    buffer.clear();
    if (!bridge.processBlock(midi, buffer, sampleRate))
      return;

    for (int i = 0; i < 3 && bridge.isPluginLoaded(); ++i) {
      midi.clear();
      buffer.clear();
      if (!bridge.processBlock(midi, buffer, sampleRate))
        return;
    }
  }

  static void quarantineCorruptPluginCache(const juce::File &xmlFile) {
    if (!xmlFile.existsAsFile())
      return;

    auto corruptFile = xmlFile.getSiblingFile(xmlFile.getFileName() + ".corrupt");
    if (corruptFile.existsAsFile())
      corruptFile.deleteFile();

    if (!xmlFile.moveFileTo(corruptFile)) {
      juce::Logger::writeToLog(
          "Failed to quarantine corrupt plugin cache: " +
          xmlFile.getFullPathName());
    }
  }

  juce::AudioDeviceManager deviceManager;
  juce::AudioProcessorPlayer devicePlayer;
  juce::AudioPluginFormatManager formatManager;

  juce::KnownPluginList pluginList;
  PluginBridge::PluginBridgeClient bridge;

  MidiPlayer midiPlayer;

  std::atomic<float> masterVolume{0.8f};
  std::atomic<bool> offlineExportActive{false};
  std::atomic<bool> workerCrashNotified{false};
  // 停止播放时的音频淡出，用于避免爆音（44.1 kHz 下约 50 ms）
  int fadeOutDuration = 2048;
  int fadeOutSamples = 2048;
  bool stopCleanupDone = false;
  int tailSamplesRendered = 0;
  int tailSilentSamples = 0;

  // seek/切曲交叉淡入：静音包含 allSoundOff 瞬态的 buffer，
  // 下一块干净 buffer 执行淡入。阶段值：0=空闲，2=淡入。
  int seekCrossfadeDuration = 128;
  int seekCrossfadeSamples = 0;
  int seekCrossfadePhase = 0;

  juce::String lastInitError;
  mutable juce::CriticalSection errorLock;
  juce::String lastPluginError;
  juce::String lastExportError;
  juce::String lastPersistenceError;
  bool lastExportCancelled = false;
  bool isRecoveringDevice = false;
  bool firstRunAudio = false;
  bool hadDeviceFallback = false; // 仅启动期使用：保存的设备缺失并已回退

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
