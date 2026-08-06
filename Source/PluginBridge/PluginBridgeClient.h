#pragma once

#include "PluginBridgeProtocol.h"
#include "PluginBridgeSharedBlock.h"
#include "../Core/WorkerPath.h"

#include <atomic>
#include <cstdint>
#include <vector>
#include <juce_events/juce_events.h>

namespace PluginBridge {

enum class BridgeStatus { stopped, starting, ready, crashed };

struct BridgeState {
  BridgeStatus status = BridgeStatus::stopped;
  juce::String message;

  void markStarting() {
    status = BridgeStatus::starting;
    message.clear();
  }

  void markReady() {
    status = BridgeStatus::ready;
    message.clear();
  }

  void markCrashed(const juce::String &diagnostic) {
    status = BridgeStatus::crashed;
    message = diagnostic;
  }
};

class PluginBridgeClient final : private juce::ChildProcessCoordinator {
public:
  PluginBridgeClient() = default;

  ~PluginBridgeClient() override { stop(); }

  bool start() {
    {
      const juce::ScopedLock lock(stateLock);
      if (state.status == BridgeStatus::ready)
        return true;
      state.markStarting();
      storeStatus(BridgeStatus::starting);
    }

    beginOperation(Command::none, {});
    responseEvent.reset();
    clearPendingReplies();
    const auto exe = midi::WorkerPath::resolve();
    const bool launched = launchWorkerProcess(
        exe, workerCommandLineUid, workerConnectionTimeoutMs, 0);

    if (!launched) {
      clearOperation();
      markCrashed("failed to launch plugin worker");
      return false;
    }

    const auto reply = waitForReply(Command::none, workerConnectionTimeoutMs);
    if (reply.code != StatusCode::ok) {
      clearOperation();
      markCrashed(reply.message);
      return false;
    }
    clearOperation();

    {
      const juce::ScopedLock lock(stateLock);
      if (state.status == BridgeStatus::crashed)
        return false;
      state.markReady();
      storeStatus(BridgeStatus::ready);
    }
    return true;
  }

  void stop() {
    if (isPluginLoaded())
      unloadPlugin();

    pluginReady.store(false, std::memory_order_release);
    nonRealtimeMode.store(false, std::memory_order_release);
    clearLoadedPluginName();
    sharedBlock = nullptr;
    shutdownWorkerProcess();

    const juce::ScopedLock lock(stateLock);
    state = {};
    storeStatus(BridgeStatus::stopped);
  }

  bool loadPlugin(const juce::PluginDescription &description) {
    pluginReady.store(false, std::memory_order_release);
    nonRealtimeMode.store(false, std::memory_order_release);
    clearLoadedPluginName();

    sharedBlockName = makeSharedBlockName();
    sharedBlock = std::make_unique<SharedBlockOwner>(sharedBlockName);
    if (!sharedBlock->isOpen()) {
      rememberCommandFailure(sharedBlock->getLastErrorMessage());
      sharedBlock = nullptr;
      return false;
    }

    if (!start())
      return false;

    const auto request = makeLoadRequest(description, sharedBlockName);
    beginOperation(Command::loadPlugin, description.name);
    responseEvent.reset();
    if (!sendMessageToWorker(makeLoadPluginCommand(request))) {
      clearOperation();
      markCrashed("failed to send plugin load command");
      return false;
    }

    const auto reply = waitForReply(Command::loadPlugin, workerCommandTimeoutMs);
    clearOperation();
    if (reply.code != StatusCode::ok) {
      handleCommandFailure(reply);
      sharedBlock = nullptr;
      return false;
    }

    pluginReady.store(true, std::memory_order_release);
    setLoadedPluginName(description.name);
    return true;
  }

  void unloadPlugin() {
    pluginReady.store(false, std::memory_order_release);
    nonRealtimeMode.store(false, std::memory_order_release);
    clearLoadedPluginName();

    const bool workerWasReady = getStatus() == BridgeStatus::ready;
    if (workerWasReady) {
      responseEvent.reset();
      if (!sendMessageToWorker(makeSimpleCommand(Command::unloadPlugin))) {
        markCrashed("failed to send plugin unload command");
      } else {
        const auto reply =
            waitForReply(Command::unloadPlugin, workerShutdownTimeoutMs);
        if (reply.code != StatusCode::ok)
          handleCommandFailure(reply);
      }
    }
    sharedBlock = nullptr;

    if (workerWasReady && getStatus() != BridgeStatus::crashed) {
      shutdownWorkerProcess();
    }
  }

  bool prepare(double sampleRate, int blockSize, bool nonRealtime = false) {
    if (!isPluginLoaded())
      return false;

    PrepareRequest request;
    request.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    request.blockSize = blockSize > 0 ? blockSize : 512;
    request.nonRealtime = nonRealtime;

    beginOperation(Command::prepare, getLoadedPluginName());
    responseEvent.reset();
    if (!sendMessageToWorker(makePrepareCommand(request))) {
      clearOperation();
      markCrashed("failed to send plugin prepare command");
      return false;
    }

    const auto reply = waitForReply(Command::prepare, workerCommandTimeoutMs);
    clearOperation();
    if (reply.code != StatusCode::ok) {
      handleCommandFailure(reply);
      return false;
    }

    nonRealtimeMode.store(nonRealtime, std::memory_order_release);
    return true;
  }

  bool processBlock(const juce::MidiBuffer &midi,
                    juce::AudioBuffer<float> &buffer, double sampleRate,
                    int midiStartSample = 0) {
    if (!isPluginLoaded() || sharedBlock == nullptr || !sharedBlock->isOpen())
      return false;

    const int numSamples = buffer.getNumSamples();
    if (numSamples > SharedBlockLayout::maxSamples) {
      rememberCommandFailure("audio block is larger than the plugin bridge buffer");
      buffer.clear();
      return false;
    }

    auto &shared = sharedBlock->block();
    shared.header.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    shared.header.blockSize = numSamples;
    shared.header.resultCode = static_cast<int>(StatusCode::renderFailed);

    for (int ch = 0; ch < SharedBlockLayout::maxChannels; ++ch)
      juce::FloatVectorOperations::clear(shared.audio[ch], numSamples);

    const int midiBytes = writeMidiBufferRange(
        midi, shared.midi, SharedBlockLayout::maxMidiBytes, midiStartSample,
        numSamples);
    if (midiBytes < 0) {
      rememberCommandFailure("MIDI block is larger than the plugin bridge buffer");
      buffer.clear();
      return false;
    }
    shared.header.midiBytes = midiBytes;

    if (!sharedBlock->signalRequest()) {
      buffer.clear();
      markCrashed(sharedBlock->getLastErrorMessage());
      return false;
    }

    const int timeoutMs = nonRealtimeMode.load(std::memory_order_acquire)
                              ? workerCommandTimeoutMs
                              : getWorkerRenderTimeoutMs(numSamples, sampleRate);
    const auto waitResult = sharedBlock->waitForResponse(timeoutMs);
    if (waitResult != WaitResult::signalled) {
      buffer.clear();
      markCrashed(waitResult == WaitResult::timedOut
                      ? "plugin worker render timeout"
                      : sharedBlock->getLastErrorMessage());
      return false;
    }

    if (shared.header.resultCode != static_cast<int>(StatusCode::ok)) {
      buffer.clear();
      markCrashed("plugin worker render failed");
      return false;
    }

    buffer.clear();
    const int channelsToCopy =
        juce::jmin(buffer.getNumChannels(), SharedBlockLayout::maxChannels);
    for (int ch = 0; ch < channelsToCopy; ++ch)
      buffer.copyFrom(ch, 0, shared.audio[ch], numSamples);

    return true;
  }

  bool openEditor() {
    if (!isPluginLoaded())
      return false;

    beginOperation(Command::openEditor, getLoadedPluginName());
    responseEvent.reset();
    if (!sendMessageToWorker(makeSimpleCommand(Command::openEditor))) {
      clearOperation();
      markCrashed("failed to send open editor command");
      return false;
    }

    const auto reply = waitForReply(Command::openEditor, workerCommandTimeoutMs);
    clearOperation();
    if (reply.code != StatusCode::ok) {
      handleCommandFailure(reply);
      return false;
    }

    return true;
  }

  void closeEditor() {
    if (getStatus() != BridgeStatus::ready)
      return;

    responseEvent.reset();
    if (!sendMessageToWorker(makeSimpleCommand(Command::closeEditor))) {
      markCrashed("failed to send close editor command");
      return;
    }

    const auto reply = waitForReply(Command::closeEditor, workerCommandTimeoutMs);
    if (reply.code != StatusCode::ok)
      handleCommandFailure(reply);
  }

  bool isPluginLoaded() const {
    return pluginReady.load(std::memory_order_acquire) &&
           getStatus() == BridgeStatus::ready;
  }

  juce::String getLoadedPluginName() const {
    const juce::ScopedLock lock(stateLock);
    return loadedPluginName;
  }

  BridgeState getState() const {
    const juce::ScopedLock lock(stateLock);
    return state;
  }

  BridgeStatus getStatus() const {
    return static_cast<BridgeStatus>(statusAtomic.load(std::memory_order_acquire));
  }

  juce::String getLastError() const {
    const juce::ScopedLock lock(stateLock);
    return state.message;
  }

  void terminateCrashedWorker() {
    if (getStatus() != BridgeStatus::crashed)
      return;

    auto message = getLastError();
    if (message.isEmpty())
      message = "plugin worker connection lost";

    pluginReady.store(false, std::memory_order_release);
    nonRealtimeMode.store(false, std::memory_order_release);
    clearLoadedPluginName();
    sharedBlock = nullptr;
    killWorkerProcess();

    const juce::ScopedLock lock(stateLock);
    state.status = BridgeStatus::stopped;
    state.message = message;
    storeStatus(BridgeStatus::stopped);
  }

  void cancelPendingOperation() {
    if (getStatus() == BridgeStatus::stopped)
      return;
    pluginReady.store(false, std::memory_order_release);
    nonRealtimeMode.store(false, std::memory_order_release);
    markCrashed("plugin operation cancelled");
    responseEvent.signal();
    killWorkerProcess();
  }

private:
  void setLoadedPluginName(const juce::String &name) {
    const juce::ScopedLock lock(stateLock);
    loadedPluginName = name;
  }

  void clearLoadedPluginName() {
    const juce::ScopedLock lock(stateLock);
    loadedPluginName.clear();
  }

  void handleConnectionLost() override {
    pluginReady.store(false, std::memory_order_release);
    nonRealtimeMode.store(false, std::memory_order_release);
    if (expectingWorkerShutdown.load(std::memory_order_acquire)) {
      const juce::ScopedLock lock(stateLock);
      state = {};
      storeStatus(BridgeStatus::stopped);
      responseEvent.signal();
      return;
    }

    markCrashed(makeConnectionLostMessage());
    responseEvent.signal();
  }

  void handleMessageFromWorker(const juce::MemoryBlock &message) override {
    const auto reply = statusReplyFromMemoryBlock(message);
    {
      const juce::ScopedLock lock(responseLock);
      replies.push_back(reply);
    }
    responseEvent.signal();
  }

  StatusReply waitForReply(Command command, int timeoutMs) {
    const auto start = juce::Time::getMillisecondCounter();
    const auto timeout = static_cast<uint32_t>(juce::jmax(1, timeoutMs));

    for (;;) {
      {
        const juce::ScopedLock lock(responseLock);
        for (auto it = replies.begin(); it != replies.end(); ++it) {
          if (it->command == command) {
            const auto reply = *it;
            replies.erase(it);
            return reply;
          }
        }
      }

      if (getStatus() == BridgeStatus::crashed) {
        auto message = getLastError();
        if (message.isEmpty())
          message = makeConnectionLostMessage();
        return {StatusCode::pluginCrashed, command, message};
      }

      const auto elapsed = juce::Time::getMillisecondCounter() - start;
      if (elapsed >= timeout)
        return {StatusCode::pluginCrashed, command,
                "plugin worker command timeout"};

      responseEvent.wait(static_cast<int>(timeout - elapsed));
    }
  }

  void markCrashed(const juce::String &message) {
    pluginReady.store(false, std::memory_order_release);
    const juce::ScopedLock lock(stateLock);
    state.markCrashed(message);
    storeStatus(BridgeStatus::crashed);
  }

  void rememberCommandFailure(const juce::String &message) {
    const juce::ScopedLock lock(stateLock);
    state.message = message;
    storeStatus(state.status);
  }

  void handleCommandFailure(const StatusReply &reply) {
    if (reply.code == StatusCode::pluginCrashed ||
        getStatus() == BridgeStatus::crashed) {
      markCrashed(reply.message);
      return;
    }

    rememberCommandFailure(reply.message);
  }

  void shutdownWorkerProcess() {
    const auto status = getStatus();
    if (status == BridgeStatus::stopped)
      return;

    if (status != BridgeStatus::crashed) {
      expectingWorkerShutdown.store(true, std::memory_order_release);
      responseEvent.reset();
      if (status == BridgeStatus::ready)
        sendMessageToWorker(makeSimpleCommand(Command::shutdown));
      responseEvent.wait(workerShutdownTimeoutMs);
    }

    killWorkerProcess();
    expectingWorkerShutdown.store(false, std::memory_order_release);
    clearPendingReplies();
    if (getStatus() != BridgeStatus::crashed) {
      const juce::ScopedLock lock(stateLock);
      state = {};
      storeStatus(BridgeStatus::stopped);
    }
  }

  void clearPendingReplies() {
    const juce::ScopedLock lock(responseLock);
    replies.clear();
  }

  void storeStatus(BridgeStatus status) {
    statusAtomic.store(static_cast<int>(status), std::memory_order_release);
  }

  void beginOperation(Command command, const juce::String &pluginName) {
    const juce::ScopedLock lock(operationLock);
    activeCommand = command;
    activePluginName = pluginName;
  }

  void clearOperation() {
    const juce::ScopedLock lock(operationLock);
    activeCommand = Command::none;
    activePluginName.clear();
  }

  juce::String makeConnectionLostMessage() const {
    const juce::ScopedLock lock(operationLock);
    juce::String message = "plugin worker connection lost";
    if (activeCommand != Command::none) {
      message += " during ";
      message += commandName(activeCommand);
    }

    if (activePluginName.isNotEmpty()) {
      message += " for ";
      message += activePluginName;
    }

    return message;
  }

  static const char *commandName(Command command) {
    switch (command) {
    case Command::loadPlugin:
      return "loadPlugin";
    case Command::prepare:
      return "prepare";
    case Command::openEditor:
      return "openEditor";
    case Command::closeEditor:
      return "closeEditor";
    case Command::unloadPlugin:
      return "unloadPlugin";
    case Command::shutdown:
      return "shutdown";
    case Command::none:
    default:
      return "startup";
    }
  }

  juce::String makeSharedBlockName() const {
    return "Local\\ModernMidiPlayerPluginBridge-" +
           juce::Uuid().toString().removeCharacters("{}");
  }

  mutable juce::CriticalSection stateLock;
  mutable juce::CriticalSection responseLock;
  mutable juce::CriticalSection operationLock;
  juce::WaitableEvent responseEvent;
  std::vector<StatusReply> replies;
  BridgeState state;
  std::atomic<int> statusAtomic{static_cast<int>(BridgeStatus::stopped)};
  std::atomic<bool> expectingWorkerShutdown{false};
  std::atomic<bool> pluginReady{false};
  std::atomic<bool> nonRealtimeMode{false};
  std::unique_ptr<SharedBlockOwner> sharedBlock;
  juce::String sharedBlockName;
  juce::String loadedPluginName;
  Command activeCommand = Command::none;
  juce::String activePluginName;
};

} // namespace PluginBridge
