#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../Utils/UserSettings.h"
#include "MidiPlayerProcessor.h"

struct ExportSettings {
  juce::String formatName = "WAV"; // "WAV", "FLAC", "Ogg Vorbis"
  double sampleRate = 96000.0;
  int bitDepth = 24;
  bool autoTail = true;
  double fixedTailSeconds = 3.0;

  juce::String title;
  int qualityIndex = 0;
};

/**
    核心音频引擎，管理音频设备、路由图（AudioProcessorGraph）和插件扫描。

    设计要点：
    - 采用 JUCE 的 AudioProcessorGraph 架构，方便灵活连接音频节点。
    - 提供对音频组件的安全访问，包含必要的空指针检查。
    - 负责保存和恢复音频设备设置，确保用户体验的一连贯性。
*/
class AudioEngine : public juce::AudioProcessor,
                    public juce::ChangeListener,
                    public juce::ChangeBroadcaster {
public:
  AudioEngine()
      : AudioProcessor(BusesProperties().withOutput(
            "Output", juce::AudioChannelSet::stereo(), true)),
        mainGraph(std::make_unique<juce::AudioProcessorGraph>()) {
    // Register VST3 format for plugin hosting
    formatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());

    // Setup Plugin Scanning
    pluginList.addChangeListener(this);
    loadKnownPluginList();

    // Try to restore saved audio device settings
    restoreAudioDeviceSettings();

    // Listen for audio device changes
    deviceManager.addChangeListener(this);

    devicePlayer.setProcessor(this);
    deviceManager.addAudioCallback(&devicePlayer);
  }

  /**
      保存当前音频设备设置，以便下次启动时恢复。
  */
  void saveAudioDeviceSettings() {
    if (auto xml = deviceManager.createStateXml()) {
      auto file =
          UserSettings::getSettingsDirectory().getChildFile("AudioDevice.xml");
      xml->writeTo(file);
    }
  }

  /**
      从保存的配置文件中恢复音频设备设置。
  */
  void restoreAudioDeviceSettings() {
    auto file =
        UserSettings::getSettingsDirectory().getChildFile("AudioDevice.xml");
    juce::String result;

    if (file.existsAsFile()) {
      if (auto xml = juce::XmlDocument::parse(file)) {
        result = deviceManager.initialise(0, 2, xml.get(), true);
        if (result.isEmpty()) {
          DBG("Restored audio device from XML");
          return;
        }
      }
      DBG("Failed to restore saved audio device from XML: " + result);
      // Saved device not found, fallback to default
      result = deviceManager.initialiseWithDefaultDevices(0, 2);
      if (result.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr)
        hadDeviceFallback = true; // Mark for UI notification at startup
    } else {
      // First run: do NOT initialise default device. Leave it null so UI
      // prompts the user.
      return;
    }

    if (result.isNotEmpty()) {
      lastInitError = result;
      DBG("Audio device init error: " + result);
    }
  }

  void prepareForOfflineExport(const ExportSettings &settings) {
    if (mainGraph == nullptr || vst3Node == nullptr) return;
    suspendProcessing(true);
    midiPlayer.setSampleRate(settings.sampleRate);
    midiPlayer.setPlaying(false);
    midiPlayer.seekTo(0.0);
    auto *processor = vst3Node->getProcessor();
    processor->setNonRealtime(true);
    const int offlineBlockSize = 16384;
    mainGraph->setPlayConfigDetails(0, 2, settings.sampleRate, offlineBlockSize);
    mainGraph->prepareToPlay(settings.sampleRate, offlineBlockSize);
  }

  void restoreFromOfflineExport() {
    if (mainGraph == nullptr || vst3Node == nullptr) return;
    auto *processor = vst3Node->getProcessor();
    processor->setNonRealtime(false);
    auto currentSetup = deviceManager.getAudioDeviceSetup();
    mainGraph->prepareToPlay(currentSetup.sampleRate, currentSetup.bufferSize);
    midiPlayer.setSampleRate(currentSetup.sampleRate);
    midiPlayer.setPlaying(false);
    midiPlayer.seekTo(0.0);
    suspendProcessing(false);
  }

  bool runOfflineExport(const juce::File &outputFile, const ExportSettings &settings,
                        std::function<void(float)> progressCallback,
                        std::function<bool()> shouldCancel) {
    lastExportError.clear();

    auto fail = [this](const juce::String &message) {
      lastExportError = message;
      return false;
    };

    if (mainGraph == nullptr || vst3Node == nullptr)
      return fail(L"音频引擎或插件未准备好。");

    const int offlineBlockSize = 16384;
    const double exportSampleRate =
        settings.sampleRate > 0.0 ? settings.sampleRate : 44100.0;

    // Start playing for offline render
    midiPlayer.setPlaying(true);

    // Create format writer
    juce::AudioFormatManager localFormatManager;
    localFormatManager.registerBasicFormats();
    juce::AudioFormat *format = nullptr;
    for (int i = 0; i < localFormatManager.getNumKnownFormats(); ++i) {
        auto* f = localFormatManager.getKnownFormat(i);
        const auto availableName = f->getFormatName();
        const bool wantsWav = settings.formatName.equalsIgnoreCase("WAV");
        const bool wantsFlac = settings.formatName.equalsIgnoreCase("FLAC");
        const bool wantsOgg = settings.formatName.containsIgnoreCase("Ogg");
        if ((wantsWav && availableName.containsIgnoreCase("WAV")) ||
            (wantsFlac && availableName.containsIgnoreCase("FLAC")) ||
            (wantsOgg && (availableName.containsIgnoreCase("Ogg") ||
                          availableName.containsIgnoreCase("Vorbis")))) {
            format = f;
            break;
        }
    }
    if (format == nullptr)
        return fail(L"当前 JUCE 构建不支持导出格式: " + settings.formatName);

    auto possibleBitDepths = format->getPossibleBitDepths();
    if (!possibleBitDepths.isEmpty() &&
        !possibleBitDepths.contains(settings.bitDepth)) {
        return fail(settings.formatName + L" 不支持 " +
                    juce::String(settings.bitDepth) + L"-bit 导出。");
    }

    auto qualityOptions = format->getQualityOptions();
    if (!qualityOptions.isEmpty() &&
        !juce::isPositiveAndBelow(settings.qualityIndex,
                                  qualityOptions.size())) {
        return fail(settings.formatName + L" 的质量/压缩等级无效。");
    }

    auto parentDir = outputFile.getParentDirectory();
    if (!parentDir.exists() && !parentDir.createDirectory())
        return fail(L"无法创建导出目录: " + parentDir.getFullPathName());

    juce::TemporaryFile tempFile(outputFile);
    auto *rawStream = new juce::FileOutputStream(tempFile.getFile());
    std::unique_ptr<juce::OutputStream> outStream(rawStream);
    if (!rawStream->openedOk())
        return fail(L"无法打开临时导出文件: " +
                    rawStream->getStatus().getErrorMessage());
    
    juce::AudioFormatWriterOptions options;
    options = options.withSampleRate(exportSampleRate)
                     .withNumChannels(2)
                     .withBitsPerSample(settings.bitDepth)
                     .withMetadata("software", "MIDI Player");
                     
    if (settings.title.isNotEmpty()) {
        options = options.withMetadata("title", settings.title);
    }
                     
    if (settings.formatName == "FLAC" || settings.formatName.containsIgnoreCase("Ogg")) {
        options = options.withQualityOptionIndex(settings.qualityIndex);
    }

    std::unique_ptr<juce::AudioFormatWriter> writer = format->createWriterFor(outStream, options);

    if (writer == nullptr) {
        return fail(L"无法创建 " + settings.formatName + L" 编码器。");
    }

    juce::AudioBuffer<float> buffer(2, offlineBlockSize);
    juce::MidiBuffer midi;
    
    double totalSamples = midiPlayer.getDurationInSamples();
    if (totalSamples <= 0) totalSamples = exportSampleRate * 60.0;

    int tailSamplesRendered = 0;
    int maxFixedTail = (int)(exportSampleRate * juce::jmax(0.0, settings.fixedTailSeconds));
    int maxAutoTail = (int)(exportSampleRate * 60.0);
    int silentSamples = 0;
    bool finishedSeq = false;
    double currentSample = 0;
    bool result = true;
    uint32_t lastCallbackTime = juce::Time::getMillisecondCounter();

    auto reportProgress = [&](float progress) {
        if (progressCallback)
            progressCallback(juce::jlimit(0.0f, 1.0f, progress));
    };

    while (true) {
        if (shouldCancel && shouldCancel()) {
            result = false;
            lastExportError = L"导出已取消。";
            break;
        }

        if (finishedSeq && !settings.autoTail && maxFixedTail <= 0)
            break;

        buffer.clear();
        midi.clear();

        mainGraph->processBlock(buffer, midi);

        // Fast SIMD hard clipping instead of heavy std::tanh
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
            juce::FloatVectorOperations::clip(buffer.getWritePointer(ch), buffer.getReadPointer(ch), -1.0f, 1.0f, buffer.getNumSamples());
        }

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
            if (midiPlayer.hasFinished() || currentSample >= totalSamples) {
                finishedSeq = true;
            }
        } else {
            if (settings.autoTail) {
                float maxLevel = 0.0f;
                for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                    maxLevel = juce::jmax(maxLevel, buffer.getMagnitude(ch, 0, buffer.getNumSamples()));
                if (maxLevel < 0.00001f) {
                    silentSamples += buffer.getNumSamples();
                    if (silentSamples > exportSampleRate * 0.5) break;
                } else {
                    silentSamples = 0;
                }
                tailSamplesRendered += buffer.getNumSamples();
                if (tailSamplesRendered >= maxAutoTail) break;
                uint32_t now = juce::Time::getMillisecondCounter();
                if (now - lastCallbackTime > 30) {
                    reportProgress(0.9f + 0.1f * ((float)tailSamplesRendered / (float)maxAutoTail));
                    lastCallbackTime = now;
                }
            } else {
                tailSamplesRendered += buffer.getNumSamples();
                uint32_t now = juce::Time::getMillisecondCounter();
                if (now - lastCallbackTime > 30 && maxFixedTail > 0) {
                    reportProgress(0.9f + 0.1f * ((float)tailSamplesRendered / (float)maxFixedTail));
                    lastCallbackTime = now;
                }
                if (tailSamplesRendered >= maxFixedTail) break;
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

    // Real-time state restoration has been moved to restoreFromOfflineExport()
    return result;
  }

  /**
      检查当前是否有有效的音频输出设备。
  */
  bool hasAudioDevice() const {
    auto *device = deviceManager.getCurrentAudioDevice();
    return device != nullptr;
  }

  /**
      Get the last initialization error message
  */
  juce::String getLastInitError() const { return lastInitError; }

  /**
      Get the last plugin loading error message
  */
  juce::String getLastPluginError() const { return lastPluginError; }

  juce::String getLastExportError() const { return lastExportError; }

  /**
      Check if this is the very first run (no AudioDevice.xml config exists)
  */
  bool isFirstRunAudio() const {
    auto file =
        UserSettings::getSettingsDirectory().getChildFile("AudioDevice.xml");
    return !file.existsAsFile();
  }

  /**
      Check if the saved audio device was missing at startup and we fell back
      to the system default. Only true for startup fallback, not runtime.
  */
  bool wasDeviceRestoredWithFallback() const { return hadDeviceFallback; }

  /**
      卸载当前的 VST3 插件。
      必须在消息线程（UI线程）中调用。

      维护注意：
      - 在移除节点前需要先暂停音频处理，防止在处理块中访问无效指针。
      - 必须手动断开图中所有与该插件相关的连接（虽然 removeNode
     理论上会处理，但显式断开更安全且可控）。
  */
  void unloadPlugin() {
    // Thread assertion - unloadPlugin must be called from the UI thread
    JUCE_ASSERT_MESSAGE_THREAD;

    suspendProcessing(true);
    midiPlayer.setPlaying(false);

    if (vst3Node != nullptr && mainGraph != nullptr) {
      // Disconnect all connections involving this node
      auto connections = mainGraph->getConnections();
      for (const auto &connection : connections) {
        if (connection.source.nodeID == vst3Node->nodeID ||
            connection.destination.nodeID == vst3Node->nodeID) {
          mainGraph->removeConnection(connection);
        }
      }
      mainGraph->removeNode(vst3Node->nodeID);
      vst3Node = nullptr;
    }

    suspendProcessing(false);
    sendChangeMessage();
  }

  /**
      Check if a plugin is currently loaded
  */
  bool hasPluginLoaded() const { return vst3Node != nullptr; }

  ~AudioEngine() override {
    // Ensure audio output is silent before tearing down
    suspendProcessing(true);

    // Clean shutdown order is important
    deviceManager.removeChangeListener(this);
    deviceManager.removeAudioCallback(&devicePlayer);
    devicePlayer.setProcessor(nullptr);

    // Release plugin before saving
    if (vst3Node != nullptr && mainGraph != nullptr) {
      mainGraph->removeNode(vst3Node->nodeID);
      vst3Node = nullptr;
    }

    saveKnownPluginList();
  }

  void prepareToPlay(double sampleRate, int samplesPerBlock) override {
    if (mainGraph != nullptr) {
      mainGraph->setPlayConfigDetails(0, 2, sampleRate, samplesPerBlock);
      mainGraph->prepareToPlay(sampleRate, samplesPerBlock);
    }

    // 仅在基础节点不存在时初始化（首次 prepare 时）
    if (audioOutputNode == nullptr || midiInputNode == nullptr ||
        playerNode == nullptr)
      setupNodes();

    // 设备切换后，已加载的 VST3 插件连接可能丢失，需要重新建立
    if (vst3Node != nullptr && mainGraph != nullptr) {
      juce::String routeError;
      if (!reconnectPluginRoutes(routeError)) {
        lastPluginError = routeError;
        mainGraph->removeNode(vst3Node->nodeID);
        vst3Node = nullptr;
      }
    }
  }

  void releaseResources() override {
    if (mainGraph != nullptr)
      mainGraph->releaseResources();
  }

  void processBlock(juce::AudioBuffer<float> &buffer,
                    juce::MidiBuffer &midiMessages) override {
    juce::ScopedNoDenormals noDenormals;

    // Clear any input (we're output only)
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels();
         ++i)
      buffer.clear(i, 0, buffer.getNumSamples());

    if (mainGraph != nullptr)
      mainGraph->processBlock(buffer, midiMessages);

    const int numSamples = buffer.getNumSamples();
    bool isPlaying = midiPlayer.getPlaying();

    // ----- Seek / track-switch crossfade -----
    // When allSoundOff fires inside the graph, the VST hard-kills its
    // voices at sample 0 — a waveform discontinuity.  We completely
    // MUTE this buffer (the artifact becomes inaudible), then fade-in
    // the next buffer from 0→1.  Total gap: one buffer (~5ms at 256
    // samples / 48kHz) — imperceptible in practice.
    if (midiPlayer.consumeSeekOccurred()) {
      // Mute entire buffer — artifact is silenced
      buffer.clear();
      // Go straight to fade-in on the next buffer
      seekCrossfadePhase = 2;
      seekCrossfadeSamples = seekCrossfadeDuration;
    }

    if (seekCrossfadePhase == 2) {
      // Fade-in: ramp gain from 0 → 1
      int toFade = juce::jmin(seekCrossfadeSamples, numSamples);
      float startGain = 1.0f - (float)seekCrossfadeSamples / (float)seekCrossfadeDuration;
      float endGain = 1.0f - (float)(seekCrossfadeSamples - toFade) / (float)seekCrossfadeDuration;
      buffer.applyGainRamp(0, toFade, startGain, endGain);
      seekCrossfadeSamples -= toFade;
      if (seekCrossfadeSamples <= 0)
        seekCrossfadePhase = 0;
    }

    // ----- Stop fade-out -----
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

    // ----- Post-fade cleanup -----
    // Once the fade-out reaches silence, tell MidiPlayer to send
    // allSoundOff to free VST voices.  This is completely inaudible
    // because the audio output is already at zero.
    if (!isPlaying && fadeOutSamples <= 0 && !stopCleanupDone) {
      midiPlayer.triggerStopCleanup();
      stopCleanupDone = true;
    }

    // Mute the single block where the VST processes the allSoundOff click
    if (midiPlayer.consumeCleanupOccurred()) {
      buffer.clear();
    }

    // Apply master volume
    float vol = masterVolume.load();
    if (fadeOutSamples > 0 || isPlaying || seekCrossfadePhase != 0) {
      buffer.applyGain(vol);

      // Soft clipper to prevent hard digital clipping (0dBFS)
      for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        float *channelData = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i) {
          channelData[i] = std::tanh(channelData[i]);
        }
      }
    }
  }

  // --- Graph Logic ---
  void setupNodes() {
    if (mainGraph == nullptr)
      return;

    mainGraph->clear();

    // Reset node pointers
    audioOutputNode = nullptr;
    midiInputNode = nullptr;
    playerNode = nullptr;

    audioOutputNode = mainGraph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    midiInputNode = mainGraph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));

    playerNode =
        mainGraph->addNode(std::make_unique<MidiPlayerProcessor>(midiPlayer));
  }

  // --- Plugin Scanning & Loading ---

  /**
      获取所有 VST3 扫描路径。
      包括：系统目录、用户目录、便携模式本地目录、自定义目录
  */
  juce::FileSearchPath getVst3SearchPaths() {
    juce::FileSearchPath searchPath;

    // 1. 便携模式：程序目录下的 VST3 文件夹（优先）
    auto exeDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();
    // 同时识别 portable.dat 和 portable_debug.dat 作为便携模式标记
    bool isPortable = exeDir.getChildFile("portable.dat").existsAsFile() ||
                      exeDir.getChildFile("portable_debug.dat").existsAsFile();

    if (isPortable) {
      auto localVst3 = exeDir.getChildFile("VST3");
      if (localVst3.isDirectory())
        searchPath.add(localVst3);
    }

    // 2. 标准系统目录：C:\Program Files\Common Files\VST3
    juce::File commonFiles("C:\\Program Files\\Common Files");
    if (commonFiles.isDirectory())
      searchPath.add(commonFiles.getChildFile("VST3"));

    // 3. 用户 AppData 目录
    auto userAppData = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory);
    searchPath.add(userAppData.getChildFile("VST3"));

    // 4. 常见的 C 盘安装目录
    juce::StringArray commonPaths = {
        "C:\\Program Files\\VSTPlugins",
        "C:\\Program Files\\Steinberg\\VSTPlugins",
        "C:\\Program Files\\Native Instruments",
        "C:\\Program Files\\Common Files\\Native Instruments\\VST3",
        "C:\\VST3",
        "C:\\VSTPlugins"};

    for (const auto &path : commonPaths) {
      juce::File dir(path);
      if (dir.isDirectory())
        searchPath.add(dir);
    }

    // 5. 用户自定义目录（从设置读取）
    for (const auto &customPath : customVst3Paths) {
      juce::File dir(customPath);
      if (dir.isDirectory())
        searchPath.add(dir);
    }

    return searchPath;
  }

  /**
      扫描所有 VST3 插件。
  */
  void scanPlugins() {
    juce::FileSearchPath searchPath = getVst3SearchPaths();

    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
      auto *format = formatManager.getFormat(i);
      if (format->getName() == "VST3") {
        juce::PluginDirectoryScanner scanner(pluginList, *format, searchPath,
                                             true, juce::File(), false);

        juce::String name;
        while (scanner.scanNextFile(true, name)) {
          // Progress callback could be added here
        }
      }
    }

    sendChangeMessage();
  }

  /**
      仅扫描指定目录。
      @param directory 要扫描的目录
  */
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

  /**
      添加自定义扫描目录。
  */
  void addCustomVst3Path(const juce::String &path) {
    if (!customVst3Paths.contains(path))
      customVst3Paths.add(path);
  }

  /**
      获取自定义扫描目录列表。
  */
  const juce::StringArray &getCustomVst3Paths() const {
    return customVst3Paths;
  }

  /**
      清除插件列表。
  */
  void clearPluginList() {
    pluginList.clear();
    sendChangeMessage();
  }

  /**
      通过插件描述信息加载 VST3 插件。
      必须从消息线程调用。

      实现逻辑解析：
      1. 检查音频设备是否就绪，若无输出设备则无法承载插件。
      2. 暂停音频回调（suspendProcessing），确保在修改 Graph
     拓扑结构时音频线程不活动。
      3. 清除旧插件：手动移除所有连接并删除旧节点。
      4. 实例化与路径连接：
         - 播放节点 (MidiPlayer) -> VST3 插件 (MIDI 连接)
         - 外部 MIDI 输入 -> VST3 插件 (MIDI 连接，用于实时键盘演奏支持)
         - VST3 插件 -> 系统音频输出 (左/右声道连接)

      @param description 要加载的插件描述
      @return 加载成功返回 true
  */
  bool loadPlugin(const juce::PluginDescription &description) {
    // Thread assertion - loadPlugin must be called from the UI thread
    JUCE_ASSERT_MESSAGE_THREAD;

    // Check if audio device is ready
    if (!hasAudioDevice()) {
      lastPluginError = L"没有可用的音频输出设备，请先在音频设置中选择设备";
      return false;
    }

    if (mainGraph == nullptr || playerNode == nullptr ||
        audioOutputNode == nullptr) {
      lastPluginError = L"音频引擎未准备好，请重启程序";
      return false;
    }

    // Suspend audio processing during plugin switch to prevent race conditions
    suspendProcessing(true);

    // Send All Notes Off to release any sounding notes before switching
    midiPlayer.setPlaying(false);

    juce::String error;
    double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    int bs = getBlockSize() > 0 ? getBlockSize() : 512;

    auto instance =
        formatManager.createPluginInstance(description, sr, bs, error);

    if (instance == nullptr) {
      DBG("Plugin load error: " + error);
      lastPluginError = L"无法加载插件: " + error;
      suspendProcessing(false);
      return false;
    }

    if (!instance->acceptsMidi()) {
      lastPluginError = "Plugin does not accept MIDI input";
      suspendProcessing(false);
      return false;
    }

    const int outputChannels = instance->getTotalNumOutputChannels();
    if (instance->getBusCount(false) < 1 || outputChannels < 1) {
      lastPluginError = "Plugin has no usable audio output bus";
      suspendProcessing(false);
      return false;
    }

    auto previousNode = vst3Node;
    auto newNode = mainGraph->addNode(std::move(instance));

    if (newNode == nullptr) {
      lastPluginError = "Failed to add plugin to the audio graph";
      suspendProcessing(false);
      return false;
    }

    // 建立所有路由连接
    if (previousNode != nullptr)
      removeConnectionsForNode(previousNode->nodeID);

    vst3Node = newNode;
    juce::String routeError;
    if (!reconnectPluginRoutes(routeError)) {
      removeConnectionsForNode(newNode->nodeID);
      mainGraph->removeNode(newNode->nodeID);
      vst3Node = previousNode;

      if (previousNode != nullptr) {
        juce::String restoreError;
        if (!reconnectPluginRoutes(restoreError)) {
          lastPluginError =
              routeError + "; failed to restore previous plugin route: " +
              restoreError;
          removeConnectionsForNode(previousNode->nodeID);
          mainGraph->removeNode(previousNode->nodeID);
          vst3Node = nullptr;
        } else {
          lastPluginError = routeError;
        }
      } else {
        lastPluginError = routeError;
      }

      suspendProcessing(false);
      return false;
    }

    if (previousNode != nullptr)
      mainGraph->removeNode(previousNode->nodeID);

    // Resume audio processing
    suspendProcessing(false);
    lastPluginError.clear();

    return true;
  }

  // --- Volume Control ---
  void setMasterVolume(float volume) {
    masterVolume.store(juce::jlimit(0.0f, 1.0f, volume));
  }

  float getMasterVolume() const { return masterVolume.load(); }

  // --- Persistence ---
  juce::File getSettingsDir() {
    // 便携模式检测
    auto exeDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();
    // 同时识别 portable.dat 和 portable_debug.dat 作为便携模式标记
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
        // Invalid or corrupt XML, delete the file
        DBG("Invalid Plugins.xml format, deleting...");
        xmlFile.deleteFile();
      }
    } catch (...) {
      // If loading fails, delete corrupt file
      xmlFile.deleteFile();
    }
  }

  void saveKnownPluginList() {
    auto xmlFile = getSettingsDir().getChildFile("Plugins.xml");

    try {
      auto xml = pluginList.createXml();
      if (xml == nullptr)
        return;

      // Create backup before overwriting
      auto backupFile = getSettingsDir().getChildFile("Plugins.xml.bak");
      if (xmlFile.existsAsFile()) {
        xmlFile.copyFileTo(backupFile);
      }

      // Write to temporary file first, then rename (atomic operation)
      auto tempFile = getSettingsDir().getChildFile("Plugins.xml.tmp");
      if (xml->writeTo(tempFile, {})) {
        // Successfully written, now replace original
        tempFile.moveFileTo(xmlFile);
        backupFile.deleteFile(); // Clean up backup
        DBG("Saved " + juce::String(pluginList.getNumTypes()) +
            " plugins to cache");
      } else {
        // Write failed, keep backup
        tempFile.deleteFile();
        DBG("Failed to write Plugins.xml");
      }
    } catch (...) {
      DBG("Exception while saving plugin list");
    }
  }

  void changeListenerCallback(juce::ChangeBroadcaster *source) override {
    if (source == &deviceManager) {
      if (isRecoveringDevice)
        return; // Prevent recursion during auto-recovery

      if (deviceManager.getCurrentAudioDevice() != nullptr) {
        // Device is active, save settings normally
        saveAudioDeviceSettings();
      } else if (!isFirstRunAudio()) {
        // Device was lost (not first run) — silently recover to default
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

  /**
      重新建立 VST3 插件的所有路由连接。
      在设备切换或插件加载后调用，确保 MIDI 和音频路由正确。
  */
  void removeConnectionsForNode(juce::AudioProcessorGraph::NodeID nodeID) {
    if (mainGraph == nullptr)
      return;

    auto connections = mainGraph->getConnections();
    for (const auto &conn : connections) {
      if (conn.source.nodeID == nodeID || conn.destination.nodeID == nodeID)
        mainGraph->removeConnection(conn);
    }
  }

  bool reconnectPluginRoutes(juce::String &error) {
    if (vst3Node == nullptr || mainGraph == nullptr || playerNode == nullptr ||
        audioOutputNode == nullptr) {
      error = "Audio graph nodes are incomplete";
      return false;
    }

    // 先移除与插件相关的旧连接
    auto *processor = vst3Node->getProcessor();
    if (processor == nullptr || !processor->acceptsMidi()) {
      error = "Plugin does not accept MIDI input";
      return false;
    }

    const int outputChannels = processor->getTotalNumOutputChannels();
    if (processor->getBusCount(false) < 1 || outputChannels < 1) {
      error = "Plugin has no usable audio output bus";
      return false;
    }

    removeConnectionsForNode(vst3Node->nodeID);

    if (!mainGraph->addConnection(
            {{playerNode->nodeID,
              juce::AudioProcessorGraph::midiChannelIndex},
             {vst3Node->nodeID,
              juce::AudioProcessorGraph::midiChannelIndex}})) {
      error = "Failed to connect MIDI file player to plugin";
      removeConnectionsForNode(vst3Node->nodeID);
      return false;
    }

    if (!mainGraph->addConnection(
            {{vst3Node->nodeID, 0}, {audioOutputNode->nodeID, 0}})) {
      error = "Failed to connect plugin left output";
      removeConnectionsForNode(vst3Node->nodeID);
      return false;
    }

    const int rightSourceChannel = outputChannels > 1 ? 1 : 0;
    if (!mainGraph->addConnection(
            {{vst3Node->nodeID, rightSourceChannel},
             {audioOutputNode->nodeID, 1}})) {
      error = "Failed to connect plugin right output";
      removeConnectionsForNode(vst3Node->nodeID);
      return false;
    }

    return true;
  }

  // --- Getters ---
  juce::AudioDeviceManager &getDeviceManager() { return deviceManager; }
  juce::KnownPluginList &getPluginList() { return pluginList; }

  juce::AudioProcessor *getVst3Instance() {
    return vst3Node != nullptr ? vst3Node->getProcessor() : nullptr;
  }

  MidiPlayer &getMidiPlayer() { return midiPlayer; }

  // --- Required AudioProcessor overrides ---
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
  juce::AudioDeviceManager deviceManager;
  juce::AudioProcessorPlayer devicePlayer;
  juce::AudioPluginFormatManager formatManager;

  juce::KnownPluginList pluginList;

  std::unique_ptr<juce::AudioProcessorGraph> mainGraph;

  juce::AudioProcessorGraph::Node::Ptr audioOutputNode;
  juce::AudioProcessorGraph::Node::Ptr midiInputNode;
  juce::AudioProcessorGraph::Node::Ptr playerNode;
  juce::AudioProcessorGraph::Node::Ptr vst3Node;

  MidiPlayer midiPlayer;

  std::atomic<float> masterVolume{0.8f};
  juce::StringArray customVst3Paths;

  // Audio fade-out to prevent pops when stopping (about 50ms at 44.1kHz)
  int fadeOutDuration = 2048;
  int fadeOutSamples = 2048;
  bool stopCleanupDone = false;

  // Seek/track-switch crossfade: mutes the buffer containing the
  // allSoundOff artifact, then fades in the next clean buffer.
  // Phase: 0=idle, 2=fade-in.  (Phase 1 removed — we now mute
  // the entire buffer immediately instead of fading it out.)
  int seekCrossfadeDuration = 128;
  int seekCrossfadeSamples = 0;
  int seekCrossfadePhase = 0;

  // Error messages for UI guidance
  juce::String lastInitError;
  juce::String lastPluginError;
  juce::String lastExportError;
  bool isRecoveringDevice = false;
  bool hadDeviceFallback = false; // Startup-only: saved device was missing

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
