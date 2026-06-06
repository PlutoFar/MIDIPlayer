#pragma once

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>

#include <functional>

#include "../Utils/UserSettings.h"
#include "MidiPlayerProcessor.h"

class AudioEngine : public juce::AudioProcessor,
                    private juce::AsyncUpdater,
                    public juce::ChangeListener,
                    public juce::ChangeBroadcaster {
public:
  AudioEngine()
      : AudioProcessor(BusesProperties().withOutput(
            "Output", juce::AudioChannelSet::stereo(), true)),
        mainGraph(std::make_unique<juce::AudioProcessorGraph>()) {
    formatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
    pluginList.addChangeListener(this);
    loadKnownPluginList();
    restoreAudioDeviceSettings();
    deviceManager.addChangeListener(this);

    devicePlayer.setProcessor(this);
    deviceManager.addAudioCallback(&devicePlayer);
  }

  void saveAudioDeviceSettings() {
    if (auto xml = deviceManager.createStateXml()) {
      auto file =
          UserSettings::getSettingsDirectory().getChildFile("AudioDevice.xml");
      xml->writeTo(file);
    }
  }

  void restoreAudioDeviceSettings() {
    auto file =
        UserSettings::getSettingsDirectory().getChildFile("AudioDevice.xml");
    juce::String result;

    if (file.existsAsFile()) {
      if (auto xml = juce::XmlDocument::parse(file)) {
        result = deviceManager.initialise(0, 2, xml.get(), true);
        if (result.isEmpty())
          return;
      }
      result = deviceManager.initialiseWithDefaultDevices(0, 2);
      if (result.isEmpty() && deviceManager.getCurrentAudioDevice() != nullptr)
        hadDeviceFallback = true;
    } else {
      return;
    }

    if (result.isNotEmpty())
      lastInitError = result;
  }

  bool hasAudioDevice() const {
    auto *device = deviceManager.getCurrentAudioDevice();
    return device != nullptr;
  }

  juce::String getLastInitError() const { return lastInitError; }

  juce::String getLastPluginError() const { return lastPluginError; }

  bool isFirstRunAudio() const {
    auto file =
        UserSettings::getSettingsDirectory().getChildFile("AudioDevice.xml");
    return !file.existsAsFile();
  }

  bool wasDeviceRestoredWithFallback() const { return hadDeviceFallback; }

  void unloadPlugin() {
    JUCE_ASSERT_MESSAGE_THREAD;

    suspendProcessing(true);
    midiPlayer.setPlaying(false);

    if (vst3Node != nullptr && mainGraph != nullptr) {
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
    broadcastChange();
  }

  bool hasPluginLoaded() const { return vst3Node != nullptr; }

  ~AudioEngine() override {
    cancelPendingUpdate();
    pluginList.removeChangeListener(this);
    deviceManager.removeChangeListener(this);
    deviceManager.removeAudioCallback(&devicePlayer);
    devicePlayer.setProcessor(nullptr);
    midiPlayer.collectRetiredResources();

    if (vst3Node != nullptr && mainGraph != nullptr) {
      mainGraph->removeNode(vst3Node->nodeID);
      vst3Node = nullptr;
    }

    saveKnownPluginList();
  }

  void prepareToPlay(double sampleRate, int samplesPerBlock) override {
    fadeOutDuration = juce::jmax(1, juce::roundToInt(sampleRate * 0.03));
    fadeOutSamples = fadeOutDuration;

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


    const bool isPlaying = midiPlayer.getPlaying();
    if (isPlaying) {
      fadeOutSamples = fadeOutDuration;
    } else if (fadeOutSamples > 0) {
      const int samplesToFade =
          juce::jmin(fadeOutSamples, buffer.getNumSamples());
      const float startGain =
          static_cast<float>(fadeOutSamples) / fadeOutDuration;
      const float endGain =
          static_cast<float>(fadeOutSamples - samplesToFade) / fadeOutDuration;
      buffer.applyGainRamp(0, samplesToFade, startGain, endGain);

      if (samplesToFade < buffer.getNumSamples())
        buffer.clear(samplesToFade, buffer.getNumSamples() - samplesToFade);

      fadeOutSamples -= samplesToFade;
    } else {
      buffer.clear();
    }

    buffer.applyGain(masterVolume.load());
  }

  void setupNodes() {
    if (mainGraph == nullptr)
      return;

    mainGraph->clear();

    audioOutputNode = nullptr;
    midiInputNode = nullptr;
    playerNode = nullptr;
    vst3Node = nullptr;

    audioOutputNode = mainGraph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::audioOutputNode));

    midiInputNode = mainGraph->addNode(
        std::make_unique<juce::AudioProcessorGraph::AudioGraphIOProcessor>(
            juce::AudioProcessorGraph::AudioGraphIOProcessor::midiInputNode));

    playerNode =
        mainGraph->addNode(std::make_unique<MidiPlayerProcessor>(midiPlayer));
  }

  juce::FileSearchPath getVst3SearchPaths() {
    juce::FileSearchPath searchPath;

    auto exeDir =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile)
            .getParentDirectory();
    bool isPortable = exeDir.getChildFile("portable.dat").existsAsFile() ||
                      exeDir.getChildFile("portable_debug.dat").existsAsFile();

    if (isPortable) {
      auto localVst3 = exeDir.getChildFile("VST3");
      if (localVst3.isDirectory())
        searchPath.add(localVst3);
    }

    juce::File commonFiles("C:\\Program Files\\Common Files");
    if (commonFiles.isDirectory())
      searchPath.add(commonFiles.getChildFile("VST3"));

    auto userAppData = juce::File::getSpecialLocation(
        juce::File::userApplicationDataDirectory);
    searchPath.add(userAppData.getChildFile("VST3"));

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

    for (const auto &customPath : customVst3Paths) {
      juce::File dir(customPath);
      if (dir.isDirectory())
        searchPath.add(dir);
    }

    return searchPath;
  }

  void scanPlugins(const std::function<bool()> &shouldCancel = {}) {
    juce::FileSearchPath searchPath = getVst3SearchPaths();

    for (int i = 0; i < formatManager.getNumFormats(); ++i) {
      auto *format = formatManager.getFormat(i);
      if (format->getName() == "VST3") {
        juce::PluginDirectoryScanner scanner(pluginList, *format, searchPath,
                                             true, juce::File(), false);

        juce::String name;
        while ((!shouldCancel || !shouldCancel()) &&
               scanner.scanNextFile(true, name)) {
        }
      }
    }

    saveKnownPluginList();
    broadcastChange();
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

    saveKnownPluginList();
    broadcastChange();
  }

  void addCustomVst3Path(const juce::String &path) {
    if (!customVst3Paths.contains(path))
      customVst3Paths.add(path);
  }

  const juce::StringArray &getCustomVst3Paths() const {
    return customVst3Paths;
  }

  void clearPluginList() {
    pluginList.clear();
    saveKnownPluginList();
    broadcastChange();
  }

  bool loadPlugin(const juce::PluginDescription &description) {
    JUCE_ASSERT_MESSAGE_THREAD;

    if (!hasAudioDevice()) {
      lastPluginError = L"没有可用的音频输出设备，请先在音频设置中选择设备";
      return false;
    }

    if (mainGraph == nullptr || playerNode == nullptr ||
        audioOutputNode == nullptr) {
      lastPluginError = L"音频引擎未准备好，请重启程序";
      return false;
    }

    suspendProcessing(true);
    midiPlayer.setPlaying(false);

    juce::String error;
    double sr = getSampleRate() > 0 ? getSampleRate() : 44100.0;
    int bs = getBlockSize() > 0 ? getBlockSize() : 512;

    auto instance =
        formatManager.createPluginInstance(description, sr, bs, error);

    if (instance == nullptr) {
      lastPluginError = L"无法加载插件: " + error;
      suspendProcessing(false);
      return false;
    }

    if (vst3Node != nullptr) {
      for (auto &connection : mainGraph->getConnections()) {
        if (connection.source.nodeID == vst3Node->nodeID ||
            connection.destination.nodeID == vst3Node->nodeID) {
          mainGraph->removeConnection(connection);
        }
      }
      mainGraph->removeNode(vst3Node->nodeID);
      vst3Node = nullptr;
    }

    vst3Node = mainGraph->addNode(std::move(instance));

    if (vst3Node == nullptr) {
      suspendProcessing(false);
      return false;
    }

    reconnectPluginRoutes();
    suspendProcessing(false);
    lastPluginError.clear();
    broadcastChange();

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
      } else {
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
      } else {
        // Write failed, keep backup
        tempFile.deleteFile();
      }
    } catch (...) {
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
          saveAudioDeviceSettings();
        } else {
          lastInitError = result;
        }
      }
    }
    broadcastChange();
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
  void broadcastChange() {
    auto *messageManager = juce::MessageManager::getInstanceWithoutCreating();
    if (messageManager == nullptr)
      return;

    if (messageManager->isThisTheMessageThread())
      sendChangeMessage();
    else
      triggerAsyncUpdate();
  }

  void handleAsyncUpdate() override { sendChangeMessage(); }

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
