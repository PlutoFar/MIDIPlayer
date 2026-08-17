#pragma once

#include "PluginBridgeProtocol.h"
#include "PluginBridgeSharedBlock.h"

#include <cstdlib>
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/detail/juce_WindowingHelpers.h>
#include <juce_gui_extra/juce_gui_extra.h>

namespace PluginBridge {

class PluginWorkerProcess final : private juce::ChildProcessWorker {
public:
  PluginWorkerProcess()
      : terminateAfterFirstRender(
            juce::SystemStats::getEnvironmentVariable(
                "MIDI_PLAYER_PLUGIN_BRIDGE_WORKER_TERMINATE_AFTER_FIRST_RENDER",
                "") == "1"),
        blockThread(*this) {
    addFormats();
  }

  ~PluginWorkerProcess() override { unloadPlugin(); }

  bool initialiseFromCommandLine(const juce::String &commandLine) {
    return juce::ChildProcessWorker::initialiseFromCommandLine(
        commandLine, workerCommandLineUid, workerConnectionTimeoutMs);
  }

private:
  class WorkerEditorWindow final : public juce::DocumentWindow {
  public:
    WorkerEditorWindow(const juce::String &name,
                       juce::AudioProcessorEditor *editor, int width,
                       int height, const juce::Image &windowIcon)
        : juce::DocumentWindow(name, juce::Colour(0xff1a1a1a),
                               juce::DocumentWindow::allButtons, true) {
      setUsingNativeTitleBar(true);

      if (editor != nullptr) {
        const bool canResize = editor->isResizable();
        setResizable(canResize, canResize);
        if (canResize)
          setResizeLimits(160, 120, 8192, 8192);
      }

      setContentOwned(editor, true);
      centreWithSize(width, height);
      setVisible(true);
      applyNativeWindowIcon(windowIcon);
      toFront(true);
    }

    void closeButtonPressed() override { setVisible(false); }

  private:
    void applyNativeWindowIcon(const juce::Image &imageToUse) {
      if (!imageToUse.isValid())
        return;

      setIcon(imageToUse);
      if (getPeer() != nullptr)
        getPeer()->setIcon(imageToUse);
    }
  };

  class BlockProcessingThread final : public juce::Thread {
  public:
    explicit BlockProcessingThread(PluginWorkerProcess &owner)
        : juce::Thread("Plugin bridge block processor"), worker(owner) {}

    void run() override { worker.processBlocksUntilStopped(); }

  private:
    PluginWorkerProcess &worker;
  };

  void addFormats() {
    if (formatManager.getNumFormats() == 0)
      formatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
  }

  static juce::Image
  resolvePluginWindowIcon(const juce::String &fileOrIdentifier) {
    const auto pluginFile = juce::File(fileOrIdentifier);
    if (!pluginFile.exists())
      return {};

    const auto icon =
        juce::detail::WindowingHelpers::createIconForFile(pluginFile);
    return hasMeaningfulPluginIconColor(icon) ? icon : juce::Image{};
  }

  static bool hasMeaningfulPluginIconColor(const juce::Image &icon) {
    if (!icon.isValid())
      return false;

    int opaquePixels = 0;
    int colorfulPixels = 0;

    for (int y = 0; y < icon.getHeight(); ++y) {
      for (int x = 0; x < icon.getWidth(); ++x) {
        const auto pixel = icon.getPixelAt(x, y);
        if (pixel.getAlpha() < 32)
          continue;

        ++opaquePixels;
        const int red = pixel.getRed();
        const int green = pixel.getGreen();
        const int blue = pixel.getBlue();
        const int high = juce::jmax(red, green, blue);
        const int low = juce::jmin(red, green, blue);

        if (high > 64 && high - low > 32)
          ++colorfulPixels;
      }
    }

    return opaquePixels > 0 &&
           colorfulPixels >= juce::jmax(8, opaquePixels / 50);
  }

  void handleConnectionMade() override {
    sendStatus(StatusCode::ok, "worker ready", Command::none);
  }

  void handleConnectionLost() override {
    juce::MessageManager::callAsync([this] {
      unloadPlugin();
      juce::JUCEApplicationBase::quit();
    });
  }

  void handleMessageFromCoordinator(const juce::MemoryBlock &message) override {
    const auto tree = blockToValueTree(message);
    const auto command = commandFromValueTree(tree);

    switch (command) {
    case Command::loadPlugin: {
      const auto request = loadRequestFromValueTree(tree);
      juce::MessageManager::callAsync([this, request] { loadPlugin(request); });
      break;
    }
    case Command::unloadPlugin:
      juce::MessageManager::callAsync([this] {
        unloadPlugin();
        sendStatus(StatusCode::ok, "plugin unloaded", Command::unloadPlugin);
      });
      break;
    case Command::prepare: {
      const auto request = prepareRequestFromValueTree(tree);
      juce::MessageManager::callAsync([this, request] { prepare(request); });
      break;
    }
    case Command::openEditor:
      juce::MessageManager::callAsync([this] { openEditor(); });
      break;
    case Command::closeEditor:
      juce::MessageManager::callAsync([this] {
        editorWindow = nullptr;
        sendStatus(StatusCode::ok, "editor closed", Command::closeEditor);
      });
      break;
    case Command::shutdown:
      juce::MessageManager::callAsync([this] {
        unloadPlugin();
        sendStatus(StatusCode::ok, "worker shutdown", Command::shutdown);
        juce::JUCEApplicationBase::quit();
      });
      break;
    case Command::none:
    default:
      sendStatus(StatusCode::invalidCommand, "invalid worker command",
                 Command::none);
      break;
    }
  }

  void loadPlugin(const PluginLoadRequest &request) {
    unloadPlugin();

    if (request.sharedBlockName.isEmpty()) {
      sendStatus(StatusCode::sharedBlockFailed, "empty shared block name",
                 Command::loadPlugin);
      return;
    }

    sharedBlock = std::make_unique<SharedBlockOwner>(request.sharedBlockName);
    if (!sharedBlock->isOpen()) {
      sendStatus(StatusCode::sharedBlockFailed,
                 sharedBlock->getLastErrorMessage(), Command::loadPlugin);
      sharedBlock = nullptr;
      return;
    }

    const auto desc = toPluginDescription(request);
    const auto windowIcon = resolvePluginWindowIcon(desc.fileOrIdentifier);
    juce::String error;
    auto instance =
        formatManager.createPluginInstance(desc, 44100.0, 512, error);
    if (instance == nullptr) {
      sharedBlock = nullptr;
      sendStatus(StatusCode::pluginLoadFailed, error, Command::loadPlugin);
      return;
    }

    if (!instance->acceptsMidi()) {
      sharedBlock = nullptr;
      sendStatus(StatusCode::pluginRouteFailed,
                 "Plugin does not accept MIDI input", Command::loadPlugin);
      return;
    }

    if (instance->getBusCount(false) < 1 ||
        instance->getTotalNumOutputChannels() < 1) {
      sharedBlock = nullptr;
      sendStatus(StatusCode::pluginRouteFailed,
                 "Plugin has no usable audio output bus",
                 Command::loadPlugin);
      return;
    }

    {
      const juce::ScopedLock lock(pluginLock);
      processChannelCount =
          getWorkerProcessChannelCount(instance->getTotalNumOutputChannels());
      plugin = std::move(instance);
      pluginWindowIcon = windowIcon;
    }

    blockThread.startThread(juce::Thread::Priority::high);
    sendStatus(StatusCode::ok, "plugin loaded", Command::loadPlugin);
  }

  void unloadPlugin() {
    stopBlockThread();
    editorWindow = nullptr;

    const juce::ScopedLock lock(pluginLock);
    plugin = nullptr;
    pluginWindowIcon = {};
    sharedBlock = nullptr;
    processBuffer.setSize(0, 0);
    processChannelCount = SharedBlockLayout::maxChannels;
  }

  void prepare(const PrepareRequest &request) {
    const juce::ScopedLock lock(pluginLock);
    if (plugin == nullptr) {
      sendStatus(StatusCode::pluginLoadFailed, "plugin not loaded",
                 Command::prepare);
      return;
    }

    const int pluginOutputChannels = plugin->getTotalNumOutputChannels();
    if (shouldUseExplicitStereoHostConfig(pluginOutputChannels)) {
      plugin->setPlayConfigDetails(0, SharedBlockLayout::maxChannels,
                                   request.sampleRate, request.blockSize);
    } else {
      // Match AudioProcessorGraph hosting for multi-output instruments: keep
      // the plugin's default bus layout and only update the running
      // sample-rate/block-size. Re-declaring all outputs as one large main bus
      // can crash plugins such as Vienna Synchron Pianos during prepareToPlay().
      plugin->setRateAndBufferSizeDetails(request.sampleRate,
                                          request.blockSize);
    }

    processChannelCount =
        getWorkerProcessChannelCount(plugin->getTotalNumOutputChannels());
    processBuffer.setSize(processChannelCount, SharedBlockLayout::maxSamples,
                          false, false, true);
    plugin->setNonRealtime(request.nonRealtime);
    plugin->prepareToPlay(request.sampleRate, request.blockSize);
    sendStatus(StatusCode::ok, "plugin prepared", Command::prepare);
  }

  void openEditor() {
    juce::AudioProcessor *processor = nullptr;
    {
      const juce::ScopedLock lock(pluginLock);
      processor = plugin.get();
    }

    if (processor == nullptr) {
      sendStatus(StatusCode::pluginLoadFailed, "plugin not loaded",
                 Command::openEditor);
      return;
    }

    if (editorWindow != nullptr) {
      editorWindow->setVisible(true);
      editorWindow->toFront(true);
      sendStatus(StatusCode::ok, "editor shown", Command::openEditor);
      return;
    }

    auto *editor = processor->createEditorAndMakeActive();
    if (editor == nullptr) {
      sendStatus(StatusCode::pluginLoadFailed, "plugin has no editor",
                 Command::openEditor);
      return;
    }

    auto bounds = editor->getBounds();
    const int width = bounds.getWidth() >= 100 ? bounds.getWidth() : 800;
    const int height = bounds.getHeight() >= 100 ? bounds.getHeight() : 600;
    editorWindow = std::make_unique<WorkerEditorWindow>(
        processor->getName(), editor, width, height, pluginWindowIcon);
    sendStatus(StatusCode::ok, "editor opened", Command::openEditor);
  }

  void processBlocksUntilStopped() {
    while (!blockThread.threadShouldExit()) {
      auto *blockOwner = sharedBlock.get();
      if (blockOwner == nullptr)
        return;

      const auto waitResult = blockOwner->waitForRequest(-1);
      if (blockThread.threadShouldExit())
        return;

      if (waitResult != WaitResult::signalled) {
        blockOwner->block().header.resultCode =
            static_cast<int>(StatusCode::sharedBlockFailed);
        blockOwner->signalResponse();
        return;
      }

      processSharedBlock(blockOwner->block());
      blockOwner->signalResponse();
      if (terminateAfterFirstRender && ++renderBlocksProcessed == 1)
        std::_Exit(42);
    }
  }

  void processSharedBlock(SharedBlock &shared) {
    const juce::ScopedLock lock(pluginLock);
    if (plugin == nullptr) {
      shared.header.resultCode = static_cast<int>(StatusCode::pluginLoadFailed);
      return;
    }

    if (shared.header.blockSize <= 0 ||
        shared.header.blockSize > SharedBlockLayout::maxSamples) {
      shared.header.resultCode = static_cast<int>(StatusCode::invalidCommand);
      return;
    }

    processBuffer.setSize(processChannelCount, shared.header.blockSize, false,
                          false, true);
    processBuffer.clear();

    juce::MidiBuffer midi;
    readMidiBuffer(shared.midi, shared.header.midiBytes, midi);
    plugin->processBlock(processBuffer, midi);

    for (int ch = 0; ch < SharedBlockLayout::maxChannels; ++ch) {
      const int sourceChannel =
          processBuffer.getNumChannels() > 1 ? ch : 0;
      juce::FloatVectorOperations::copy(
          shared.audio[ch], processBuffer.getReadPointer(sourceChannel),
          shared.header.blockSize);
    }

    shared.header.resultCode = static_cast<int>(StatusCode::ok);
  }

  void stopBlockThread() {
    if (!blockThread.isThreadRunning())
      return;

    blockThread.signalThreadShouldExit();
    if (sharedBlock != nullptr)
      sharedBlock->signalRequest();
    if (!blockThread.stopThread(workerShutdownTimeoutMs))
      std::_Exit(EXIT_FAILURE);
  }

  void sendStatus(StatusCode code, const juce::String &message,
                  Command command) {
    sendMessageToCoordinator(makeStatusReply(code, message, command));
  }

  juce::AudioPluginFormatManager formatManager;
  std::unique_ptr<juce::AudioPluginInstance> plugin;
  std::unique_ptr<SharedBlockOwner> sharedBlock;
  juce::Image pluginWindowIcon;
  std::unique_ptr<juce::DocumentWindow> editorWindow;
  juce::AudioBuffer<float> processBuffer;
  int processChannelCount = SharedBlockLayout::maxChannels;
  const bool terminateAfterFirstRender = false;
  int renderBlocksProcessed = 0;
  juce::CriticalSection pluginLock;
  BlockProcessingThread blockThread;
};

inline std::unique_ptr<PluginWorkerProcess> &workerInstance() {
  static std::unique_ptr<PluginWorkerProcess> instance;
  return instance;
}

inline bool runWorkerIfRequested(const juce::String &commandLine) {
  if (!isPluginWorkerCommandLine(commandLine))
    return false;

  auto worker = std::make_unique<PluginWorkerProcess>();
  if (!worker->initialiseFromCommandLine(commandLine)) {
    juce::JUCEApplicationBase::quit();
    return true;
  }

  workerInstance() = std::move(worker);
  return true;
}

} // namespace PluginBridge
