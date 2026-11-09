#include "TestHarness.h"
#include "TestMidiEvents.h"
#include "PluginBridge/PluginBridgeClient.h"
#include "PluginBridge/PluginListSupport.h"
#include "PluginBridge/PluginScanProcess.h"
#include <array>
#include <atomic>
#include <cstdlib>
#include <thread>

namespace {

bool waitForBridgeStatus(PluginBridge::PluginBridgeClient &bridge,
                         PluginBridge::BridgeStatus status, int timeoutMs) {
  const auto start = juce::Time::getMillisecondCounter();
  const auto timeout = static_cast<uint32_t>(juce::jmax(1, timeoutMs));

  do {
    if (bridge.getStatus() == status)
      return true;

    juce::Thread::sleep(10);
  } while (juce::Time::getMillisecondCounter() - start < timeout);

  return bridge.getStatus() == status;
}

class TestPluginBridgeWorker final : private juce::ChildProcessWorker {
public:
  TestPluginBridgeWorker() : renderThread(*this) {}

  ~TestPluginBridgeWorker() override { stopRenderThread(); }

  bool run(const juce::String &commandLine) {
    if (!initialiseFromCommandLine(commandLine,
                                   PluginBridge::workerCommandLineUid,
                                   PluginBridge::workerConnectionTimeoutMs))
      return false;

    while (!stopped.wait(0))
      juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
    return true;
  }

private:
  class RenderThread final : public juce::Thread {
  public:
    explicit RenderThread(TestPluginBridgeWorker &owner)
        : juce::Thread("Test plugin bridge renderer"), owner(owner) {}

    void run() override { owner.runRenderLoop(); }

  private:
    TestPluginBridgeWorker &owner;
  };

  void handleConnectionMade() override {
    sendMessageToCoordinator(PluginBridge::makeStatusReply(
        PluginBridge::StatusCode::ok, "test worker ready",
        PluginBridge::Command::none));
    if (juce::SystemStats::getEnvironmentVariable(
            "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_AFTER_READY", "") == "1")
      std::_Exit(1);
  }

  void handleConnectionLost() override { stopped.signal(); }

  void handleMessageFromCoordinator(const juce::MemoryBlock &message) override {
    const auto tree = PluginBridge::blockToValueTree(message);
    const auto command = PluginBridge::commandFromValueTree(tree);
    if (command == PluginBridge::Command::loadPlugin &&
        juce::SystemStats::getEnvironmentVariable(
            "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_IGNORE_LOAD", "") == "1")
      return;
    if (command == PluginBridge::Command::loadPlugin &&
        juce::SystemStats::getEnvironmentVariable(
            "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_FAIL", "") == "1") {
      sendMessageToCoordinator(PluginBridge::makeStatusReply(
          PluginBridge::StatusCode::pluginLoadFailed, "test load failed",
          PluginBridge::Command::loadPlugin));
      return;
    }

    if (command == PluginBridge::Command::loadPlugin &&
        juce::SystemStats::getEnvironmentVariable(
            "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_ON_LOAD", "") == "1")
      std::_Exit(1);

    if (command == PluginBridge::Command::loadPlugin) {
      const auto request = PluginBridge::loadRequestFromValueTree(tree);
      const int renderDelayMs =
          juce::SystemStats::getEnvironmentVariable(
              "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_RENDER_DELAY_MS", "0")
              .getIntValue();
      sharedBlock =
          std::make_unique<PluginBridge::SharedBlockOwner>(request.sharedBlockName);
      firstRenderDelayMs = renderDelayMs;
      firstRenderPending = renderDelayMs > 0;
      if (!sharedBlock->isOpen() ||
          !renderThread.startThread(juce::Thread::Priority::highest)) {
        sendMessageToCoordinator(PluginBridge::makeStatusReply(
            PluginBridge::StatusCode::renderFailed, "test worker could not start rendering",
            command));
        return;
      }
      sendMessageToCoordinator(PluginBridge::makeStatusReply(
          PluginBridge::StatusCode::ok, "test plugin loaded",
          PluginBridge::Command::loadPlugin));
      return;
    }

    if ((command == PluginBridge::Command::prepare ||
         command == PluginBridge::Command::beginExport ||
         command == PluginBridge::Command::endExport) &&
        sharedBlock != nullptr) {
      sendMessageToCoordinator(PluginBridge::makeStatusReply(
          PluginBridge::StatusCode::ok, "test plugin prepared",
          command));
      return;
    }

    if (command == PluginBridge::Command::unloadPlugin) {
      stopRenderThread();
      const int delayMs = juce::SystemStats::getEnvironmentVariable(
                              "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_UNLOAD_DELAY_MS",
                              "0")
                              .getIntValue();
      if (delayMs > 0)
        juce::Thread::sleep(delayMs);
      sendMessageToCoordinator(PluginBridge::makeStatusReply(
          PluginBridge::StatusCode::ok, "test plugin unloaded",
          PluginBridge::Command::unloadPlugin));
      return;
    }

    if (command == PluginBridge::Command::shutdown) {
      if (juce::SystemStats::getEnvironmentVariable(
              "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_IGNORE_SHUTDOWN", "") == "1")
        return;
      sendMessageToCoordinator(PluginBridge::makeStatusReply(
          PluginBridge::StatusCode::ok, "test worker shutdown",
          PluginBridge::Command::shutdown));
      stopped.signal();
    }
  }

  void runRenderLoop() {
    juce::MidiBuffer midi;
    while (!renderThread.threadShouldExit() && sharedBlock != nullptr) {
      if (sharedBlock->waitForRequest(-1) != PluginBridge::WaitResult::signalled)
        return;
      if (renderThread.threadShouldExit())
        return;

      auto &block = sharedBlock->block();
      if (firstRenderPending) {
        firstRenderPending = false;
        juce::Thread::sleep(firstRenderDelayMs);
      }

      for (auto &channel : block.audio)
        juce::FloatVectorOperations::clear(channel, block.header.blockSize);
      const bool decoded = PluginBridge::readMidiBuffer(
          block.midi, block.header.midiBytes, midi, block.header.blockSize);
      if (decoded)
        for (const auto event : midi)
          if (event.getMessage().isNoteOn())
            for (auto &channel : block.audio)
              channel[event.samplePosition] = 0.5f;
      block.header.resultCode = static_cast<int>(
          decoded ? PluginBridge::StatusCode::ok : PluginBridge::StatusCode::renderFailed);
      block.header.responseSequence = block.header.requestSequence;
      sharedBlock->signalResponse();
    }
  }

  void stopRenderThread() {
    if (!renderThread.isThreadRunning()) {
      sharedBlock = nullptr;
      return;
    }
    renderThread.signalThreadShouldExit();
    if (sharedBlock != nullptr)
      sharedBlock->signalRequest();
    renderThread.stopThread(PluginBridge::workerShutdownTimeoutMs);
    sharedBlock = nullptr;
  }

  juce::WaitableEvent stopped;
  std::unique_ptr<PluginBridge::SharedBlockOwner> sharedBlock;
  RenderThread renderThread;
  int firstRenderDelayMs = 0;
  bool firstRenderPending = false;
};

} // namespace

namespace miditest {

int runTestWorker(const juce::String &commandLine) {
  TestPluginBridgeWorker worker;
  return worker.run(commandLine) ? 0 : 1;
}

int runPluginBridgeTests() {
  int failures = 0;
  const auto expect = [&](bool condition, const char *message) {
    miditest::expect(failures, condition, message);
  };

  {
    std::cout << "  Worker path and scanner exit" << std::endl;
    const TestDirectory directory;
    const auto player = directory.path.getChildFile("MidiPlayer.exe");
    const auto worker = directory.path.getChildFile("MidiWorker.exe");
    expect(PluginBridge::WorkerPath::resolve(player) == player,
           "worker resolution should support self-hosting");
    expect(worker.replaceWithText("worker fixture") &&
               PluginBridge::WorkerPath::resolve(player) == worker,
           "worker resolution should prefer the adjacent executable");

    juce::KnownPluginList source, destination;
    juce::String diagnostic;
    const auto testExe = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    expect(!PluginBridge::scanPluginListInChildProcess(
               source, destination, {}, diagnostic, testExe) && diagnostic.isNotEmpty(),
           "scanner process failure should return a diagnostic");
  }

  auto pluginTempDir =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getNonexistentChildFile("midi-player-plugin-list-tests", "");
  expect(pluginTempDir.createDirectory(),
         "plugin-list temporary directory should exist");
  const auto existingVst3 = pluginTempDir.getChildFile("Existing.vst3");
  expect(existingVst3.createDirectory(),
         "existing VST3 test bundle should exist");
  const auto missingVst3 = pluginTempDir.getChildFile("Missing.vst3");

  juce::PluginDescription existingPlugin;
  existingPlugin.name = "Existing";
  existingPlugin.pluginFormatName = "VST3";
  existingPlugin.fileOrIdentifier = existingVst3.getFullPathName();
  existingPlugin.uniqueId = 1;

  juce::PluginDescription missingPlugin;
  missingPlugin.name = "Missing";
  missingPlugin.pluginFormatName = "VST3";
  missingPlugin.fileOrIdentifier = missingVst3.getFullPathName();
  missingPlugin.uniqueId = 2;

  juce::KnownPluginList knownPlugins;
  expect(knownPlugins.addType(existingPlugin),
         "existing plugin should be added to the test list");
  expect(knownPlugins.addType(missingPlugin),
         "missing plugin should be added to the test list");

  juce::AudioPluginFormatManager pluginFormatManager;
  pluginFormatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
  expect(removeMissingPluginTypes(knownPlugins, pluginFormatManager) == 1,
         "full scan cleanup should remove exactly one missing plugin");
  expect(knownPlugins.getNumTypes() == 1,
         "full scan cleanup should retain existing plugins");
  expect(knownPlugins.getTypes()[0].fileOrIdentifier ==
             existingVst3.getFullPathName(),
         "full scan cleanup should retain the existing plugin path");
  pluginTempDir.deleteRecursively();

  {
    PluginBridge::PluginLoadRequest request;
    request.name = "Ivory VST";
    request.format = "VST3";
    request.fileOrIdentifier =
        "C:\\Program Files\\Common Files\\VST3\\Synthogy\\Ivory.vst3\\Contents\\x86_64-win\\Ivory.vst3";
    request.uniqueId = 0xe6abd61b;
    request.sharedBlockName = "Local\\ModernMidiPlayerPluginBridge-test";

    const auto block = PluginBridge::toMemoryBlock(request);
    const auto decoded = PluginBridge::loadRequestFromMemoryBlock(block);

    expect(decoded.name == request.name,
           "plugin bridge load request should preserve plugin name");
    expect(decoded.fileOrIdentifier == request.fileOrIdentifier,
           "plugin bridge load request should preserve plugin path");
    expect(decoded.uniqueId == request.uniqueId,
           "plugin bridge load request should preserve unique ID");
    expect(decoded.sharedBlockName == request.sharedBlockName,
           "plugin bridge load request should preserve shared block name");
  }

  {
    const auto reply =
        PluginBridge::makeStatusReply(PluginBridge::StatusCode::pluginCrashed,
                                      "worker process exited",
                                      PluginBridge::Command::loadPlugin);
    const auto decoded = PluginBridge::statusReplyFromMemoryBlock(reply);
    expect(decoded.code == PluginBridge::StatusCode::pluginCrashed,
           "plugin bridge status reply should preserve crash status");
    expect(decoded.command == PluginBridge::Command::loadPlugin,
           "plugin bridge status reply should preserve command context");
    expect(decoded.message == "worker process exited",
           "plugin bridge status reply should preserve diagnostic text");
  }

  expect(PluginBridge::isPluginWorkerCommandLine(
             juce::String("--worker ") + PluginBridge::workerCommandLineUid),
         "plugin worker command line should be recognised");
  expect(!PluginBridge::isPluginWorkerCommandLine(
             "--worker modern-midi-player-plugin-worker-v1"),
         "stale v1 workers should be rejected by the v2 host protocol");
  expect(!PluginBridge::isPluginWorkerCommandLine("C:\\song.mid"),
         "regular MIDI file command line should not start worker mode");

  {
    std::cout << "  Worker handshake and shutdown" << std::endl;
    PluginBridge::PluginBridgeClient bridgeClient;
    expect(bridgeClient.start(),
           "plugin bridge client should launch and connect to a worker process");
    expect(bridgeClient.getState().status == PluginBridge::BridgeStatus::ready,
           "plugin bridge client should expose connected worker state");
    bridgeClient.stop();
  }

  {
    std::cout << "  Late realtime block" << std::endl;
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_RENDER_DELAY_MS", L"75");
    PluginBridge::PluginBridgeClient bridgeClient;
    juce::PluginDescription desc;
    desc.name = "Late render recovery test plugin";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "C:\\test\\LateRenderPlugin.vst3";
    expect(bridgeClient.loadPlugin(desc),
           "plugin bridge should load the delayed-render test worker");
    expect(bridgeClient.prepare(48000.0, 64),
           "plugin bridge should prepare the delayed-render test worker");

    juce::AudioBuffer<float> buffer(2, 64);
    juce::MidiBuffer midi;
    expect(bridgeClient.processBlock(midi, buffer, 48000.0),
           "the render thread should wait for a delayed block without dropping it");
    expect(bridgeClient.getStatus() == PluginBridge::BridgeStatus::ready,
           "one late realtime render should keep the worker recoverable");
    expect(bridgeClient.processBlock(midi, buffer, 48000.0),
           "the next request should render after the previous request completes");
    expect(bridgeClient.getStatus() == PluginBridge::BridgeStatus::ready,
           "late render recovery should keep the bridge ready");
    bridgeClient.unloadPlugin();
    bridgeClient.stop();
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_RENDER_DELAY_MS");
  }

  {
    std::cout << "  Unresponsive shutdown" << std::endl;
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_IGNORE_SHUTDOWN", L"1");
    PluginBridge::PluginBridgeClient bridgeClient;
    expect(bridgeClient.start(),
           "plugin bridge should connect to a shutdown-hang test worker");
    const auto stopStarted = juce::Time::getMillisecondCounter();
    bridgeClient.stop();
    const auto stopElapsed =
        juce::Time::getMillisecondCounter() - stopStarted;
    expect(stopElapsed < 5000,
           "hung worker shutdown should be forcibly bounded");
    expect(bridgeClient.getStatus() == PluginBridge::BridgeStatus::stopped,
           "forced worker shutdown should leave the bridge stopped");
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_IGNORE_SHUTDOWN");
  }

  {
    std::cout << "  Disconnect after startup" << std::endl;
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_AFTER_READY", L"1");
    PluginBridge::PluginBridgeClient bridgeClient;
    const bool started = bridgeClient.start();
    const bool crashed = waitForBridgeStatus(
        bridgeClient, PluginBridge::BridgeStatus::crashed, 20000);
    expect(started || crashed,
           "plugin bridge client should either start or report a worker crash");
    expect(crashed,
           "plugin bridge client should mark crashed when the worker exits");
    expect(bridgeClient.getLastError().contains("connection lost"),
           "plugin bridge client should preserve the worker disconnect error");
    bridgeClient.terminateCrashedWorker();
    expect(bridgeClient.getStatus() == PluginBridge::BridgeStatus::stopped,
           "terminating a crashed worker should return the bridge to reloadable state");
    expect(bridgeClient.getLastError().contains("connection lost"),
           "terminating a crashed worker should keep the crash diagnostic");
    bridgeClient.stop();
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_AFTER_READY");
  }

  {
    std::cout << "  Disconnect during load" << std::endl;
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_ON_LOAD", L"1");
    PluginBridge::PluginBridgeClient bridgeClient;
    juce::PluginDescription desc;
    desc.name = "Ivory VST";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "C:\\test\\Ivory.vst3";
    const bool loaded = bridgeClient.loadPlugin(desc);
    expect(!loaded,
           "plugin bridge client should report worker exits during plugin load");
    expect(bridgeClient.getLastError().contains("loadPlugin") &&
               bridgeClient.getLastError().contains("Ivory VST"),
           "worker disconnect diagnostics should include the active load command and plugin name");
    bridgeClient.terminateCrashedWorker();
    bridgeClient.stop();
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_ON_LOAD");
  }

  {
    std::cout << "  Cancel a pending load" << std::endl;
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_IGNORE_LOAD", L"1");
    PluginBridge::PluginBridgeClient bridgeClient;
    juce::PluginDescription desc;
    desc.name = "Cancelled test plugin";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "C:\\test\\CancelledPlugin.vst3";
    std::atomic<bool> loadResult{true};
    std::thread loadThread(
        [&] { loadResult = bridgeClient.loadPlugin(desc); });
    const auto deadline = juce::Time::getMillisecondCounter() + 5000u;
    while (bridgeClient.getStatus() != PluginBridge::BridgeStatus::ready &&
           juce::Time::getMillisecondCounter() < deadline)
      juce::Thread::sleep(5);
    const auto cancelStarted = juce::Time::getMillisecondCounter();
    bridgeClient.cancelPendingOperation();
    loadThread.join();
    expect(!loadResult.load(),
           "cancelling a pending plugin load should return failure");
    expect(juce::Time::getMillisecondCounter() - cancelStarted < 5000,
           "pending plugin load cancellation should be bounded");
    bridgeClient.terminateCrashedWorker();
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_IGNORE_LOAD");
  }

  {
    std::cout << "  Recoverable load rejection" << std::endl;
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_FAIL", L"1");
    PluginBridge::PluginBridgeClient bridgeClient;
    juce::PluginDescription desc;
    desc.name = "Failing test plugin";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "C:\\missing\\FailingTestPlugin.vst3";
    const bool loaded = bridgeClient.loadPlugin(desc);
    expect(!loaded,
           "plugin bridge client should report worker plugin load failures");
    expect(bridgeClient.getStatus() == PluginBridge::BridgeStatus::ready,
           "plugin load failure replies should keep the worker ready");
    expect(bridgeClient.getLastError().contains("test load failed"),
           "plugin load failure replies should preserve diagnostics");
    bridgeClient.stop();
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_FAIL");
  }

  {
    std::cout << "  Wait for unload acknowledgement" << std::endl;
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_UNLOAD_DELAY_MS", L"200");
    PluginBridge::PluginBridgeClient bridgeClient;
    juce::PluginDescription desc;
    desc.name = "Unload wait test plugin";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "C:\\test\\UnloadWaitPlugin.vst3";
    expect(bridgeClient.loadPlugin(desc),
           "plugin bridge client should load the test worker plugin");
    const auto unloadStart = juce::Time::getMillisecondCounter();
    bridgeClient.unloadPlugin();
    const auto unloadElapsed =
        juce::Time::getMillisecondCounter() - unloadStart;
    expect(unloadElapsed >= 150,
           "plugin bridge unload should wait for the worker acknowledgement");
    expect(bridgeClient.getStatus() == PluginBridge::BridgeStatus::stopped,
           "plugin bridge unload should stop the worker after a clean unload");
    bridgeClient.stop();
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_UNLOAD_DELAY_MS");
  }

  expect(PluginBridge::getSharedBlockChunkCount(1024) == 1,
         "a 1024-sample host block should use one bridge chunk");
  expect(PluginBridge::getSharedBlockChunkCount(2048) == 2,
         "a 2048-sample host block should use two bridge chunks");
  expect(PluginBridge::getSharedBlockChunkCount(2560) == 3,
         "a DirectSound-sized host block should use three bridge chunks");
  expect(PluginBridge::getSharedBlockChunkCount(4096) == 4,
         "a 4096-sample host block should use four bridge chunks");
  expect(PluginBridge::getSharedBlockChunkSize(2560, 2048) == 512,
         "the final bridge chunk should contain the remaining samples");
  expect(PluginBridge::getWorkerProcessChannelCount(22) == 22,
         "multi-output instruments should process with their full output count");
  expect(PluginBridge::getWorkerProcessChannelCount(1) ==
             PluginBridge::SharedBlockLayout::maxChannels,
         "mono instruments should still render into the stereo bridge output");
  expect(PluginBridge::shouldUseExplicitStereoHostConfig(2),
         "stereo instruments should use the explicit stereo host layout");
  expect(!PluginBridge::shouldUseExplicitStereoHostConfig(22),
         "multi-output instruments should keep their default bus layout");

  juce::MidiBuffer bridgeMidi;
  bridgeMidi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 7);
  bridgeMidi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 11);
  unsigned char midiBytes[PluginBridge::SharedBlockLayout::maxMidiBytes]{};
  const int written = PluginBridge::writeMidiBuffer(
      bridgeMidi, midiBytes, PluginBridge::SharedBlockLayout::maxMidiBytes);
  expect(written > 0, "bridge MIDI serialization should write bytes");
  juce::MidiBuffer decodedBridgeMidi;
  const bool decodedBridgeOk = PluginBridge::readMidiBuffer(
      midiBytes, written, decodedBridgeMidi, 1024);
  expect(decodedBridgeOk &&
             findNoteSamplePosition(decodedBridgeMidi, true, 1, 60) == 7,
         "bridge MIDI serialization should preserve note sample position");
  expect(findControllerSamplePosition(decodedBridgeMidi, 1, 64, 127) == 11,
         "bridge MIDI serialization should preserve controller sample position");

  juce::MidiBuffer rangedBridgeMidi;
  rangedBridgeMidi.addEvent(
      juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 100);
  rangedBridgeMidi.addEvent(
      juce::MidiMessage::noteOn(1, 61, (juce::uint8)100), 1100);
  rangedBridgeMidi.addEvent(
      juce::MidiMessage::noteOn(1, 62, (juce::uint8)100), 2500);
  const int rangedBytes = PluginBridge::writeMidiBufferRange(
      rangedBridgeMidi, midiBytes, PluginBridge::SharedBlockLayout::maxMidiBytes,
      1024, 1024);
  juce::MidiBuffer decodedRange;
  const bool decodedRangeOk = PluginBridge::readMidiBuffer(
      midiBytes, rangedBytes, decodedRange, 1024);
  expect(decodedRangeOk &&
             findNoteSamplePosition(decodedRange, true, 1, 61) == 76,
         "bridge chunk serialization should rebase MIDI sample positions");
  expect(findNoteSamplePosition(decodedRange, true, 1, 60) < 0 &&
             findNoteSamplePosition(decodedRange, true, 1, 62) < 0,
         "bridge chunk serialization should exclude adjacent chunk events");

  PluginBridge::PrepareRequest prepareRequest;
  prepareRequest.sampleRate = 96000.0;
  prepareRequest.blockSize = 4096;
  prepareRequest.nonRealtime = true;
  const auto parsedPrepare = PluginBridge::prepareRequestFromValueTree(
      PluginBridge::blockToValueTree(
          PluginBridge::makePrepareCommand(prepareRequest)));
  expect(parsedPrepare.sampleRate == prepareRequest.sampleRate &&
             parsedPrepare.blockSize == prepareRequest.blockSize &&
             parsedPrepare.nonRealtime,
         "bridge prepare protocol should preserve non-realtime mode");

  return failures;
}

} // namespace miditest
