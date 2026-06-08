#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../Utils/UserSettings.h"
#include "MidiPlayerProcessor.h"
#include <atomic>

/**
    离线导出参数。UI 负责文件路径和交互校验，这里只描述编码、采样率和尾音策略。
*/
struct ExportSettings {
  juce::String formatName = "WAV"; // 编码格式："WAV"、"FLAC" 或 "Ogg Vorbis"
  double sampleRate = 96000.0;      // 导出采样率；无效值在导出入口回退到 44100 Hz
  int bitDepth = 24;                // 目标位深，必须由对应 JUCE AudioFormat 支持
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
    - 运行时 MIDI 来源是内置 MidiPlayerProcessor，不连接外部 MIDI 输入。
*/
class AudioEngine : public juce::AudioProcessor,
                    public juce::ChangeListener,
                    public juce::ChangeBroadcaster {
public:
  AudioEngine()
      : AudioProcessor(BusesProperties().withOutput(
            "Output", juce::AudioChannelSet::stereo(), true)),
        mainGraph(std::make_unique<juce::AudioProcessorGraph>()) {
    // 初始化插件格式、扫描缓存和音频设备回调。
    formatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
    pluginList.addChangeListener(this);
    loadKnownPluginList();
    restoreAudioDeviceSettings();
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
      // 已保存设备不可用时回退到系统默认设备
      result = deviceManager.initialiseWithDefaultDevices(0, 2);
      if (result.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr)
        hadDeviceFallback = true; // 仅用于启动时提示 UI：已发生设备回退
    } else {
      // 首次运行不自动初始化默认设备，保留空状态让 UI 引导用户选择
      return;
    }

    if (result.isNotEmpty()) {
      lastInitError = result;
      DBG("Audio device init error: " + result);
    }
  }

  /**
      进入离线导出状态。

      离线导出直接驱动 mainGraph 渲染，实时 AudioProcessorPlayer 回调会被
      offlineExportActive 静音。这里同时把插件切到 non-realtime，并用导出采样率
      重新 prepareToPlay，避免实时设备配置影响文件渲染。

      调用方必须确保没有并发插件加载/卸载；离线导出会暂停实时处理并重配 graph。
  */
  void prepareForOfflineExport(const ExportSettings &settings) {
    if (mainGraph == nullptr || vst3Node == nullptr) return;
    offlineExportActive.store(true, std::memory_order_release);
    suspendProcessing(true);
    const double exportSampleRate =
        settings.sampleRate > 0.0 ? settings.sampleRate : 44100.0;
    midiPlayer.setPlaying(false);
    midiPlayer.setSampleRate(exportSampleRate);
    midiPlayer.seekTo(0.0);
    auto *processor = vst3Node->getProcessor();
    if (processor == nullptr) {
      offlineExportActive.store(false, std::memory_order_release);
      suspendProcessing(false);
      return;
    }
    processor->setNonRealtime(true);
    // 大块离线渲染减少宿主调度开销；实时线程不使用这个块大小
    const int offlineBlockSize = 16384;
    mainGraph->setPlayConfigDetails(0, 2, exportSampleRate, offlineBlockSize);
    mainGraph->prepareToPlay(exportSampleRate, offlineBlockSize);
  }

  /**
      离开离线导出状态，恢复实时设备的采样率、块大小和插件 realtime 模式。
  */
  void restoreFromOfflineExport() {
    if (mainGraph == nullptr || vst3Node == nullptr) {
      offlineExportActive.store(false, std::memory_order_release);
      suspendProcessing(false);
      return;
    }
    auto *processor = vst3Node->getProcessor();
    processor->setNonRealtime(false);
    auto currentSetup = deviceManager.getAudioDeviceSetup();
    const double liveSampleRate =
        currentSetup.sampleRate > 0.0 ? currentSetup.sampleRate : 44100.0;
    const int liveBlockSize =
        currentSetup.bufferSize > 0 ? currentSetup.bufferSize : 512;
    mainGraph->prepareToPlay(liveSampleRate, liveBlockSize);
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
      engine.prepareForOfflineExport(settings);
      active = true;
    }

    ~OfflineExportSession() {
      if (active)
        engine.restoreFromOfflineExport();
    }

    OfflineExportSession(const OfflineExportSession &) = delete;
    OfflineExportSession &operator=(const OfflineExportSession &) = delete;

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
    // 应在 OfflineExportSession 生效后调用，保证采样率和插件 non-realtime 状态一致。

    auto fail = [this](const juce::String &message) {
      lastExportError = message;
      return false;
    };

    if (mainGraph == nullptr || vst3Node == nullptr)
      return fail(L"音频引擎或插件未准备好。");

    const int offlineBlockSize = 16384;
    const double exportSampleRate =
        settings.sampleRate > 0.0 ? settings.sampleRate : 44100.0;

    // 离线渲染期间由 mainGraph 主动拉取 MIDI 事件
    midiPlayer.setPlaying(true);

    // 解析导出格式并校验编码参数。
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

    // 进度前 90% 对应 MIDI 主体，后 10% 留给尾音；自动尾音最多渲染 60 秒，
    // 并要求连续 0.5 秒低于 0.00001 线性电平后结束，避免混响和释放音被截断。
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

        // 离线导出用 JUCE SIMD clip 代替实时路径的 std::tanh，避免长文件导出过慢
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
      获取最近一次音频设备初始化错误。
  */
  juce::String getLastInitError() const { return lastInitError; }

  /**
      获取最近一次插件加载错误。
  */
  juce::String getLastPluginError() const { return lastPluginError; }

  juce::String getLastExportError() const { return lastExportError; }

  /**
      判断是否首次运行音频设置（不存在 AudioDevice.xml）。
  */
  bool isFirstRunAudio() const {
    auto file =
        UserSettings::getSettingsDirectory().getChildFile("AudioDevice.xml");
    return !file.existsAsFile();
  }

  /**
      判断启动时保存的音频设备是否缺失，并已回退到系统默认设备。
      仅表示启动阶段的回退，不表示运行时设备恢复。
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
    // unloadPlugin 必须从消息线程（UI线程）调用
    JUCE_ASSERT_MESSAGE_THREAD;

    suspendProcessing(true);
    midiPlayer.setPlaying(false);

    if (vst3Node != nullptr && mainGraph != nullptr) {
      // 断开所有涉及当前插件节点的连接
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
      判断当前是否已加载插件。
  */
  bool hasPluginLoaded() const { return vst3Node != nullptr; }

  ~AudioEngine() override {
    // 析构前先让音频输出静音
    suspendProcessing(true);

    // 先移除回调和监听，再释放图节点
    deviceManager.removeChangeListener(this);
    deviceManager.removeAudioCallback(&devicePlayer);
    devicePlayer.setProcessor(nullptr);

    // 保存插件列表前释放当前插件节点
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

    if (isOfflineExportActive()) {
      buffer.clear();
      midiMessages.clear();
      return;
    }

    // 本引擎只输出音频，清空任何输入通道
    for (auto i = getTotalNumInputChannels(); i < getTotalNumOutputChannels();
         ++i)
      buffer.clear(i, 0, buffer.getNumSamples());

    if (mainGraph != nullptr)
      mainGraph->processBlock(buffer, midiMessages);

    const int numSamples = buffer.getNumSamples();
    bool isPlaying = midiPlayer.getPlaying();

    // ----- seek/切曲交叉淡入 -----
    // allSoundOff 在图内触发时，VST3 插件会在样本 0 立即切断声部并产生波形不连续。
    // 这里把包含该事件的整块 buffer 静音，再让下一块从 0 淡入到 1；在 48 kHz、
    // 256 samples 下空隙约 5 ms，换取无爆音的 seek/切曲。
    if (midiPlayer.consumeSeekOccurred()) {
      // 整块静音，屏蔽 allSoundOff 造成的瞬态
      buffer.clear();
      // 下一块直接进入淡入阶段
      seekCrossfadePhase = 2;
      seekCrossfadeSamples = seekCrossfadeDuration;
    }

    if (seekCrossfadePhase == 2) {
      // 淡入：增益从 0 线性爬升到 1
      int toFade = juce::jmin(seekCrossfadeSamples, numSamples);
      float startGain = 1.0f - (float)seekCrossfadeSamples / (float)seekCrossfadeDuration;
      float endGain = 1.0f - (float)(seekCrossfadeSamples - toFade) / (float)seekCrossfadeDuration;
      buffer.applyGainRamp(0, toFade, startGain, endGain);
      seekCrossfadeSamples -= toFade;
      if (seekCrossfadeSamples <= 0)
        seekCrossfadePhase = 0;
    }

    // ----- 停止淡出 -----
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

    // ----- 淡出后清理 -----
    // 淡出到静音后再让 MidiPlayer 发送 allSoundOff 释放 VST3 声部；
    // 此时输出已为 0，所以清理事件不会被听到。
    if (!isPlaying && fadeOutSamples <= 0 && !stopCleanupDone) {
      midiPlayer.triggerStopCleanup();
      stopCleanupDone = true;
    }

    // 静音 VST3 处理 allSoundOff 的单个音频块，屏蔽释放瞬态
    if (midiPlayer.consumeCleanupOccurred()) {
      buffer.clear();
    }

    // 应用主音量
    float vol = masterVolume.load();
    if (fadeOutSamples > 0 || isPlaying || seekCrossfadePhase != 0) {
      buffer.applyGain(vol);

      // 软削波器：用 std::tanh 避免超过 0 dBFS 的硬削波
      for (int ch = 0; ch < buffer.getNumChannels(); ++ch) {
        float *channelData = buffer.getWritePointer(ch);
        for (int i = 0; i < numSamples; ++i) {
          channelData[i] = std::tanh(channelData[i]);
        }
      }
    }
  }

  // --- AudioProcessorGraph ---
  void setupNodes() {
    if (mainGraph == nullptr)
      return;

    mainGraph->clear();

    // 重置节点指针
    audioOutputNode = nullptr;
    midiInputNode = nullptr;
    playerNode = nullptr;

    audioOutputNode = mainGraph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    // 保留 JUCE 图 I/O 节点给未来外部 MIDI 路由；当前播放只把内置
    // MIDI 文件播放器路由到乐器插件。
    midiInputNode = mainGraph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));

    playerNode =
        mainGraph->addNode(std::make_unique<MidiPlayerProcessor>(midiPlayer));
  }

  // --- VST3 插件扫描与加载 ---

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
      同步扫描所有 VST3 插件。调用方用模态进度窗承载等待状态，
      当前扫描循环未向 UI 报告逐插件进度。
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

      实现逻辑：
      1. 检查音频设备是否就绪，若无输出设备则无法承载插件。
      2. 暂停处理并停止 MIDI 播放，降低切换插件时的音频回调竞争。
      3. 清除旧插件节点及其连接。
      4. 实例化与路径连接：
         - MidiPlayerProcessor -> VST3 插件 (MIDI)
         - VST3 插件 -> 系统音频输出 (左/右声道连接)

      @param description 要加载的插件描述
      @return 加载成功返回 true
  */
  bool loadPlugin(const juce::PluginDescription &description) {
    // loadPlugin 必须从消息线程（UI线程）调用
    JUCE_ASSERT_MESSAGE_THREAD;

    // 插件宿主需要可用音频输出设备
    if (!hasAudioDevice()) {
      lastPluginError = L"没有可用的音频输出设备，请先在音频设置中选择设备";
      return false;
    }

    if (mainGraph == nullptr || playerNode == nullptr ||
        audioOutputNode == nullptr) {
      lastPluginError = L"音频引擎未准备好，请重启程序";
      return false;
    }

    // 切换插件期间暂停音频处理，避免回调访问正在变更的 AudioProcessorGraph
    suspendProcessing(true);

    // 这里只停止 MIDI 序列推进，不会立即发送 All Notes Off 或 allSoundOff
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
      // 新插件路由失败时优先恢复旧节点；旧节点也恢复失败才移除它。
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

    // 恢复音频处理
    suspendProcessing(false);
    lastPluginError.clear();

    return true;
  }

  // --- 音量控制 ---
  void setMasterVolume(float volume) {
    masterVolume.store(juce::jlimit(0.0f, 1.0f, volume));
  }

  float getMasterVolume() const { return masterVolume.load(); }

  // --- 设备设置 ---
  juce::File getSettingsDir() {
    auto exeDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();
    // 这里需与 UserSettings::getSettingsDirectory() 的便携标记保持一致。
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
        // XML 无效或损坏时删除缓存文件
        DBG("Invalid Plugins.xml format, deleting...");
        xmlFile.deleteFile();
      }
    } catch (...) {
      // 读取失败时删除损坏的缓存文件
      xmlFile.deleteFile();
    }
  }

  void saveKnownPluginList() {
    auto xmlFile = getSettingsDir().getChildFile("Plugins.xml");

    try {
      auto xml = pluginList.createXml();
      if (xml == nullptr)
        return;

      // 覆盖前先创建备份
      auto backupFile = getSettingsDir().getChildFile("Plugins.xml.bak");
      if (xmlFile.existsAsFile()) {
        xmlFile.copyFileTo(backupFile);
      }

      // 先写临时文件，再重命名替换，降低写入中断导致缓存损坏的概率
      auto tempFile = getSettingsDir().getChildFile("Plugins.xml.tmp");
      if (xml->writeTo(tempFile, {})) {
        // 写入成功后替换原文件
        tempFile.moveFileTo(xmlFile);
        backupFile.deleteFile(); // 清理备份
        DBG("Saved " + juce::String(pluginList.getNumTypes()) +
            " plugins to cache");
      } else {
        // 写入失败时保留备份
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
        return; // 防止自动恢复期间递归触发

      if (deviceManager.getCurrentAudioDevice() != nullptr) {
        // 设备有效时正常保存设置
        saveAudioDeviceSettings();
      } else if (!isFirstRunAudio()) {
        // 非首次运行时设备丢失，静默恢复到默认设备
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
      移除 AudioProcessorGraph 中与指定节点相关的所有连接。
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

  /**
      重新建立 VST3 插件的所有路由连接。
      在设备切换或插件加载后调用，确保 MIDI 和音频输出连接正确。
  */
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

  // --- 访问器 ---
  juce::AudioDeviceManager &getDeviceManager() { return deviceManager; }
  juce::KnownPluginList &getPluginList() { return pluginList; }

  juce::AudioProcessor *getVst3Instance() {
    return vst3Node != nullptr ? vst3Node->getProcessor() : nullptr;
  }

  MidiPlayer &getMidiPlayer() { return midiPlayer; }

  // --- JUCE AudioProcessor 必需覆写 ---
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
  std::atomic<bool> offlineExportActive{false};
  juce::StringArray customVst3Paths;

  // 停止播放时的音频淡出，用于避免爆音（44.1 kHz 下约 50 ms）
  int fadeOutDuration = 2048;
  int fadeOutSamples = 2048;
  bool stopCleanupDone = false;

  // seek/切曲交叉淡入：静音包含 allSoundOff 瞬态的 buffer，
  // 再对下一块干净 buffer 淡入。阶段值：0=空闲，2=淡入。
  int seekCrossfadeDuration = 128;
  int seekCrossfadeSamples = 0;
  int seekCrossfadePhase = 0;

  // 提供给 UI 的错误信息
  juce::String lastInitError;
  juce::String lastPluginError;
  juce::String lastExportError;
  bool isRecoveringDevice = false;
  bool hadDeviceFallback = false; // 仅启动期使用：保存的设备缺失并已回退

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
