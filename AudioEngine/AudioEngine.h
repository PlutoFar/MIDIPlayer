#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include "../Utils/UserSettings.h"
#include "MidiPlayerProcessor.h"

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
      reconnectPluginRoutes();
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

    // Throttled logging for audio callback
    static int logCounter = 0;
    if (++logCounter >= 2000) {
      // LOG_DEBUG("AudioEngine::processBlock - Audio thread alive");
      logCounter = 0;
    }

    // 处理淡出效果以防止音频爆音
    // 当停止播放时，应用一个简短的增益衰减坡度（Gain
    // Ramp），避免波形突变产生的咔哒声。
    bool isPlaying = midiPlayer.getPlaying();
    if (!isPlaying && fadeOutSamples > 0) {
      // Apply fade-out ramp
      int samplesToFade = juce::jmin(fadeOutSamples, buffer.getNumSamples());
      float startGain = (float)fadeOutSamples / (float)fadeOutDuration;
      float endGain =
          (float)(fadeOutSamples - samplesToFade) / (float)fadeOutDuration;

      buffer.applyGainRamp(0, samplesToFade, startGain, endGain);

      // Clear remaining samples if fade is complete
      if (samplesToFade < buffer.getNumSamples()) {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
          buffer.clear(ch, samplesToFade,
                       buffer.getNumSamples() - samplesToFade);
      }

      fadeOutSamples -= samplesToFade;
    } else if (isPlaying && fadeOutSamples != fadeOutDuration) {
      // Reset fade-out counter when playing
      fadeOutSamples = fadeOutDuration;
    }

    // Apply master volume
    float vol = masterVolume.load();
    if (fadeOutSamples > 0 || isPlaying)
      buffer.applyGain(vol);
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

    // Remove old plugin first - disconnect all connections before removing node
    if (vst3Node != nullptr) {
      // Remove connections to prevent dangling references
      for (auto &connection : mainGraph->getConnections()) {
        if (connection.source.nodeID == vst3Node->nodeID ||
            connection.destination.nodeID == vst3Node->nodeID) {
          mainGraph->removeConnection(connection);
        }
      }
      mainGraph->removeNode(vst3Node->nodeID);
      vst3Node = nullptr;
    }

    // Prepare the new instance before adding to graph
    instance->prepareToPlay(sr, bs);

    vst3Node = mainGraph->addNode(std::move(instance));

    if (vst3Node == nullptr) {
      suspendProcessing(false);
      return false;
    }

    // 建立所有路由连接
    reconnectPluginRoutes();

    // Resume audio processing
    suspendProcessing(false);

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
  void reconnectPluginRoutes() {
    if (vst3Node == nullptr || mainGraph == nullptr)
      return;

    // 先移除与插件相关的旧连接
    auto connections = mainGraph->getConnections();
    for (const auto &conn : connections) {
      if (conn.source.nodeID == vst3Node->nodeID ||
          conn.destination.nodeID == vst3Node->nodeID)
        mainGraph->removeConnection(conn);
    }

    // Player -> VST3 (MIDI)
    if (playerNode != nullptr)
      mainGraph->addConnection(
          {{playerNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
           {vst3Node->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});

    // External MIDI In -> VST3
    if (midiInputNode != nullptr)
      mainGraph->addConnection(
          {{midiInputNode->nodeID, juce::AudioProcessorGraph::midiChannelIndex},
           {vst3Node->nodeID, juce::AudioProcessorGraph::midiChannelIndex}});

    // VST3 -> Audio Out
    if (audioOutputNode != nullptr) {
      mainGraph->addConnection(
          {{vst3Node->nodeID, 0}, {audioOutputNode->nodeID, 0}});
      mainGraph->addConnection(
          {{vst3Node->nodeID, 1}, {audioOutputNode->nodeID, 1}});
    }
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

  // Audio fade-out to prevent pops (about 50ms at 44.1kHz)
  int fadeOutDuration = 2048;
  int fadeOutSamples = 2048;

  // Error messages for UI guidance
  juce::String lastInitError;
  juce::String lastPluginError;
  bool isRecoveringDevice = false;
  bool hadDeviceFallback = false; // Startup-only: saved device was missing

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AudioEngine)
};
