#pragma once

#include "../Core/WorkerPath.h"
#include "PluginBridgeProtocol.h"
#include "PluginBridgeSharedBlock.h"

#include <atomic>
#include <cstdint>
#include <juce_events/juce_events.h>
#include <vector>

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

// Responsibilities: 插件控制命令、进程状态与音频共享块通信。
// Ownership: 控制线程持有进程和共享内存寿命；JUCE 连接回调只发布回复或状态。
// Concurrency: 控制命令互相串行；加载、卸载、准备和回收前必须停止渲染调用。
// `processBlock` 只有一个渲染调用方，可与不更改共享块的编辑器命令并行。
class PluginBridgeClient final : private juce::ChildProcessCoordinator {
public:
  PluginBridgeClient() = default;

  ~PluginBridgeClient() override { stop(); }

  // Postconditions: 启动子进程并等待 ready 回复；失败返回 `false` 并记录进程诊断。
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
    const bool launched = launchWorkerProcess(exe, workerCommandLineUid,
                                              workerConnectionTimeoutMs, 0);

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

  // Preconditions: 已结束渲染与其他控制命令；释放映射、关闭子进程并清空会话状态。
  void stop() {
    if (isPluginLoaded())
      unloadPlugin();

    pluginReady.store(false, std::memory_order_release);
    nonRealtimeMode.store(false, std::memory_order_release);
    clearLoadedPluginName();
    sharedBlock = nullptr;
    resetRenderState();
    shutdownWorkerProcess();

    const juce::ScopedLock lock(stateLock);
    state = {};
    storeStatus(BridgeStatus::stopped);
  }

  // Preconditions: 描述来自扫描目录，旧实例已卸载；控制线程独占调用。
  // Postconditions: 建立共享块并收到成功回复后才发布已加载状态；失败须读取 `getLastError`。
  bool loadPlugin(const juce::PluginDescription &description) {
    pluginReady.store(false, std::memory_order_release);
    nonRealtimeMode.store(false, std::memory_order_release);
    clearLoadedPluginName();
    resetRenderState();

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

    const auto reply =
        waitForReply(Command::loadPlugin, workerCommandTimeoutMs);
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

  // Ordering: 先撤销可渲染状态，再等待卸载确认，最后释放映射和正常关闭进程。
  // Failures: 超时/断连保留崩溃状态；调用方通过 `getStatus` 识别异常。
  void unloadPlugin() {
    pluginReady.store(false, std::memory_order_release);
    nonRealtimeMode.store(false, std::memory_order_release);
    clearLoadedPluginName();
    resetRenderState();

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

  // Preconditions: 插件已加载且渲染已暂停；采样率单位为 Hz，块大小单位为采样。
  // Postconditions: 成功回复后切换实时/离线等待策略；失败返回 `false` 并保留诊断。
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

  // Preconditions: 块大小不超过 `SharedBlockLayout::maxSamples`，MIDI 时间戳为宿主块采样偏移。
  // Ordering: 同时最多一个未完成渲染请求；晚到回复必须核对并消费后才能复用共享块。
  // Postconditions: `true` 表示已复制该请求的音频；`false` 不保证进程已崩溃，调用方须静音失败块。
  // Failures: 实时超时保留未完成请求；离线超时、序列不符和传输失败记录崩溃状态。
  bool processBlock(const juce::MidiBuffer &midi,
                    juce::AudioBuffer<float> &buffer, double sampleRate,
                    int midiStartSample = 0) {
    if (!isPluginLoaded() || sharedBlock == nullptr || !sharedBlock->isOpen())
      return false;

    const bool nonRealtime = nonRealtimeMode.load(std::memory_order_acquire);
    if (renderRequestOutstanding) {
      const auto lateResult = sharedBlock->waitForResponse(0);
      if (lateResult == WaitResult::signalled) {
        if (!completeOutstandingRender()) {
          buffer.clear();
          return false;
        }
      } else if (lateResult == WaitResult::failed) {
        buffer.clear();
        resetOutstandingRender();
        markCrashed(sharedBlock->getLastErrorMessage());
        return false;
      } else {
        const auto elapsed =
            juce::Time::getMillisecondCounter() - outstandingRenderStartTime;
        buffer.clear();
        if (elapsed >= static_cast<uint32_t>(workerRenderHangTimeoutMs))
          markCrashed("plugin worker render unresponsive");
        return false;
      }
    }

    const int numSamples = buffer.getNumSamples();
    if (numSamples > SharedBlockLayout::maxSamples) {
      rememberCommandFailure(
          "audio block is larger than the plugin bridge buffer");
      buffer.clear();
      return false;
    }

    auto &shared = sharedBlock->block();
    const auto requestSequence = nextRenderSequence++;
    shared.header.sampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
    shared.header.requestSequence = requestSequence;
    shared.header.responseSequence = 0;
    shared.header.blockSize = numSamples;
    shared.header.resultCode = static_cast<int>(StatusCode::renderFailed);

    for (int ch = 0; ch < SharedBlockLayout::maxChannels; ++ch)
      juce::FloatVectorOperations::clear(shared.audio[ch], numSamples);

    const int midiBytes =
        writeMidiBufferRange(midi, shared.midi, SharedBlockLayout::maxMidiBytes,
                             midiStartSample, numSamples);
    if (midiBytes < 0) {
      rememberCommandFailure(
          "MIDI block is larger than the plugin bridge buffer");
      buffer.clear();
      return false;
    }
    shared.header.midiBytes = midiBytes;

    if (!sharedBlock->signalRequest()) {
      buffer.clear();
      markCrashed(sharedBlock->getLastErrorMessage());
      return false;
    }

    renderRequestOutstanding = true;
    outstandingRenderSequence = requestSequence;
    outstandingRenderStartTime = juce::Time::getMillisecondCounter();

    const int timeoutMs =
        nonRealtime ? workerCommandTimeoutMs
                    : getWorkerRenderTimeoutMs(numSamples, sampleRate);
    const auto waitResult = sharedBlock->waitForResponse(timeoutMs);
    if (waitResult != WaitResult::signalled) {
      buffer.clear();
      if (waitResult == WaitResult::failed) {
        resetRenderState();
        markCrashed(sharedBlock->getLastErrorMessage());
      } else if (nonRealtime) {
        resetRenderState();
        markCrashed("plugin worker offline render timeout");
      }
      return false;
    }

    if (!completeOutstandingRender()) {
      buffer.clear();
      return false;
    }

    buffer.clear();
    const int channelsToCopy =
        juce::jmin(buffer.getNumChannels(), SharedBlockLayout::maxChannels);
    for (int ch = 0; ch < channelsToCopy; ++ch)
      buffer.copyFrom(ch, 0, shared.audio[ch], numSamples);

    return true;
  }

  // Preconditions: 已加载插件；阻塞等待工作进程在消息线程创建窗口，调用方不得为主界面线程。
  // Failures: 打开返回 `false`；关闭失败通过 `getStatus`/`getLastError` 查询。
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

    const auto reply =
        waitForReply(Command::openEditor, workerCommandTimeoutMs);
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

    const auto reply =
        waitForReply(Command::closeEditor, workerCommandTimeoutMs);
    if (reply.code != StatusCode::ok)
      handleCommandFailure(reply);
  }

  // Concurrency: 以下状态查询返回原子值或加锁副本，不借用共享内存。
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
    return static_cast<BridgeStatus>(
        statusAtomic.load(std::memory_order_acquire));
  }

  juce::String getLastError() const {
    const juce::ScopedLock lock(stateLock);
    return state.message;
  }

  // Preconditions: 控制线程独占，渲染已停止；只回收崩溃会话，并保留崩溃诊断供界面读取。
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
    resetRenderState();
    killWorkerProcess();

    {
      const juce::ScopedLock lock(stateLock);
      state.status = BridgeStatus::stopped;
      state.message = message;
      storeStatus(BridgeStatus::stopped);
    }
    clearPendingReplies();
    clearOperation();
  }

  void cancelPendingOperation() {
    // Concurrency: 可跨线程置位并唤醒等待；进程和映射仍由控制线程回收。
    cancellationRequested.store(true, std::memory_order_release);
    responseEvent.signal();
  }

  // Ordering: 仅在受理新控制任务、启动任务线程之前清除上一任务的取消标记。
  void resetCancellation() {
    cancellationRequested.store(false, std::memory_order_release);
  }

private:
  bool completeOutstandingRender() {
    if (!renderRequestOutstanding || sharedBlock == nullptr)
      return false;

    const auto &header = sharedBlock->block().header;
    const auto expectedSequence = outstandingRenderSequence;
    resetOutstandingRender();

    if (header.responseSequence != expectedSequence) {
      markCrashed("plugin worker render sequence mismatch");
      return false;
    }

    if (header.resultCode != static_cast<int>(StatusCode::ok)) {
      markCrashed("plugin worker render failed");
      return false;
    }

    return true;
  }

  void resetOutstandingRender() {
    renderRequestOutstanding = false;
    outstandingRenderSequence = 0;
    outstandingRenderStartTime = 0;
  }

  void resetRenderState() {
    resetOutstandingRender();
    nextRenderSequence = 1;
  }

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

  // Concurrency: 仅控制线程等待；连接线程在 `responseLock` 内发布回复并唤醒。
  // Invariant: 命令串行化保证按 `Command` 匹配回复时不会与另一同类命令混淆。
  StatusReply waitForReply(Command command, int timeoutMs) {
    const auto start = juce::Time::getMillisecondCounter();
    const auto timeout = static_cast<uint32_t>(juce::jmax(1, timeoutMs));

    for (;;) {
      if (cancellationRequested.load(std::memory_order_acquire))
        return {StatusCode::pluginCrashed, command,
                "plugin operation cancelled"};
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

    if (status != BridgeStatus::crashed &&
        !cancellationRequested.load(std::memory_order_acquire)) {
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
  std::atomic<bool> cancellationRequested{false};
  std::atomic<bool> nonRealtimeMode{false};
  std::unique_ptr<SharedBlockOwner> sharedBlock;
  juce::String sharedBlockName;
  juce::String loadedPluginName;
  std::uint64_t nextRenderSequence = 1;
  std::uint64_t outstandingRenderSequence = 0;
  uint32_t outstandingRenderStartTime = 0;
  bool renderRequestOutstanding = false;
  Command activeCommand = Command::none;
  juce::String activePluginName;
};

} // namespace PluginBridge
