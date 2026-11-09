#include "AudioEngine.h"
#include "../Utils/AudioThreadPriority.h"
#include "../Utils/DebugLogger.h"
#include "ExportAudioProcessing.h"

AudioEngine::AudioEngine()
    : AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)),
      juce::Thread("MIDI audio renderer") {}

AudioEngine::~AudioEngine() {
  suspendProcessing(true);
  stopRealtimeRenderer();
  midiPlayer.setPlaying(false);
  bridge.stop();
}

// Concurrency: 设备线程只发布原子配置，阻塞 IPC 由核心插件任务执行。
void AudioEngine::prepareToPlay(double sampleRate, int blockSize) {
  const juce::ScopedLock lock(getCallbackLock());
  // Invariant: MIDI 序列只有一个生产方；设备线程不得与 seek/导出并发发布序列快照。
  deviceSampleRate.store(sampleRate > 0.0 ? sampleRate : 44100.0);
  deviceBlockSize.store(blockSize > 0 ? blockSize : 512);
  deviceRevision.fetch_add(1);
}

bool AudioEngine::preparePlugin(std::function<void(double)> prepareMidi) {
  double sampleRate;
  int blockSize;
  unsigned revision;
  bool wasSuspended;
  {
    const juce::ScopedLock lock(getCallbackLock());
    sampleRate = deviceSampleRate.load();
    blockSize = deviceBlockSize.load();
    revision = deviceRevision.load();
    wasSuspended = isSuspended();
  }
  suspendProcessing(true);
  stopRealtimeRenderer();
  const bool ok = bridge.prepare(sampleRate, blockSize);
  if (ok)
    preparedRevision.store(revision);
  else
    setLastPluginError(bridge.getLastError());
  if (ok && prepareMidi)
    prepareMidi(sampleRate);
  const bool started = ok && startRealtimeRenderer(sampleRate, blockSize);
  suspendProcessing(wasSuspended);
  return started;
}

AudioEngine::OfflineExportSession::OfflineExportSession(
    AudioEngine &owner, std::unique_ptr<juce::MidiMessageSequence> sequence,
    double sampleRate)
    : engine(owner), rate(sampleRate) {
  player.setSequence(std::move(sequence), rate);
  if (!std::isfinite(durationSamples()) || durationSamples() <= 0.0) {
    preparation = juce::Result::fail(L"待导出的 MIDI 没有有效时长。");
    return;
  }
  player.resetForOfflineRender(rate);
  player.setPlaying(true);
  preparation = engine.prepareForOfflineExport(rate);
  active = preparation.wasOk();
}

AudioEngine::OfflineExportSession::~OfflineExportSession() { finish(); }

juce::Result AudioEngine::OfflineExportSession::finish() {
  if (!std::exchange(active, false))
    return juce::Result::ok();
  return engine.restoreFromOfflineExport();
}

juce::Result AudioEngine::OfflineExportSession::render(
    juce::AudioBuffer<float> &audio, juce::MidiBuffer &midi) {
  if (engine.renderPluginBlock(player, MidiPlayer::ProcessMode::offline,
                               audio, midi, rate))
    return juce::Result::ok();
  const auto error = engine.bridge.getLastError();
  return juce::Result::fail(error.isEmpty() ? L"插件进程渲染失败。" : error);
}

juce::Result AudioEngine::prepareForOfflineExport(double sampleRate) {
  if (!bridge.isPluginLoaded())
    return juce::Result::fail(L"音频引擎或插件未准备好。");

  suspendProcessing(true);
  stopRealtimeRenderer();
  if (bridge.prepare(sampleRate, exportOfflineBlockSize, true,
                     PluginBridge::Command::beginExport))
    return juce::Result::ok();

  auto error = juce::String(L"无法初始化离线导出: ") + bridge.getLastError();
  const auto restored = restoreFromOfflineExport();
  if (restored.failed())
    error += L"\n" + restored.getErrorMessage();
  return juce::Result::fail(error);
}

juce::Result AudioEngine::restoreFromOfflineExport() {
  double liveSampleRate;
  int blockSize;
  unsigned revision;
  {
    const juce::ScopedLock lock(getCallbackLock());
    liveSampleRate = deviceSampleRate.load();
    blockSize = deviceBlockSize.load();
    revision = deviceRevision.load();
  }
  const bool restored = bridge.isPluginLoaded() &&
      bridge.prepare(liveSampleRate, blockSize, false,
                     PluginBridge::Command::endExport);
  if (restored) {
    midiPlayer.setSampleRate(liveSampleRate);
    // 插件已重新初始化。续播前恢复当前定位的控制器和踏板状态。
    if (midiPlayer.getPlaying())
      midiPlayer.seekTo(midiPlayer.getPositionInSamples(), true);
    preparedRevision.store(revision);
  }
  const bool started =
      restored && startRealtimeRenderer(liveSampleRate, blockSize);
  if (!started)
    midiPlayer.setPlaying(false);
  suspendProcessing(false);
  if (started)
    return juce::Result::ok();
  const auto diagnostic = restored ? getLastPluginError() : bridge.getLastError();
  if (!restored && bridge.isPluginLoaded())
    bridge.failRenderSession(diagnostic);
  return juce::Result::fail(L"无法恢复实时音频: " + diagnostic);
}

void AudioEngine::unloadPlugin() {
  suspendProcessing(true);
  stopRealtimeRenderer();
  midiPlayer.setPlaying(false);

  bridge.closeEditor();
  flushPluginStateBeforeUnload();
  bridge.unloadPlugin();

  suspendProcessing(false);
}

void AudioEngine::terminateCrashedPluginWorker() {
  suspendProcessing(true);
  stopRealtimeRenderer();
  bridge.terminateCrashedWorker();
  suspendProcessing(false);
}

bool AudioEngine::openPluginEditor() {
  if (!bridge.openEditor()) {
    setLastPluginError(bridge.getLastError());
    return false;
  }
  return true;
}

void AudioEngine::processBlock(juce::AudioBuffer<float> &buffer,
                               juce::MidiBuffer &midiMessages) {
  midiMessages.clear();
  if (requiresPrepare() || !bridge.isPluginLoaded()) {
    buffer.clear();
    return;
  }
  if (realtimeAudio.read(buffer) < buffer.getNumSamples())
    underrunCount.fetch_add(1, std::memory_order_relaxed);
  notify();
}

void AudioEngine::stopRealtimeRenderer() {
  signalThreadShouldExit();
  notify();
  // Ordering: IPC 自身有会话失败期限；线程结束前禁止释放其缓冲或插件映射。
  waitForThreadToExit(-1);
}

bool AudioEngine::startRealtimeRenderer(double rate, int devicePeriod) {
  preparedRenderSampleRate = rate;
  renderQuantum = juce::jmin(devicePeriod, PluginBridge::SharedBlockLayout::maxSamples);
  // Reason: 缓存两个设备周期，分别用于当前输出和下一周期的跨进程渲染。
  realtimeAudio.prepare(devicePeriod * 2);
  realtimeBlock.setSize(2, renderQuantum);
  realtimeMidi.ensureSize(PluginBridge::SharedBlockLayout::maxMidiBytes);
  outputGain.reset(rate, 0.01);
  outputGain.setCurrentAndTargetValue(masterVolume.load());
  fadeOutDuration = juce::jmax(1, juce::roundToInt(rate * 0.05));
  fadeOutSamples = fadeOutDuration;
  seekCrossfadeDuration = juce::jmax(1, juce::roundToInt(rate * 0.003));
  seekCrossfadeSamples = seekCrossfadePhase = 0;
  tailSamplesRendered = tailSilentSamples = 0;
  stopCleanupDone = false;
  renderLatencySamples.store(devicePeriod * 2 + bridge.getLatencySamples());
  setLatencySamples(renderLatencySamples.load());
  renderThreadStarted.reset();
  juce::String error = L"无法启动音频渲染线程。";
  if (startThread(juce::Thread::Priority::high)) {
    renderThreadStarted.wait(-1);
    if (renderThreadStartResult.wasOk())
      return true;
    error = renderThreadStartResult.getErrorMessage();
    stopRealtimeRenderer();
  }
  setLastPluginError(error);
  bridge.failRenderSession(error);
  return false;
}

void AudioEngine::run() {
  AudioThreadPriority priority;
  renderThreadStartResult = priority.enableRealtime();
  renderThreadStarted.signal();
  if (renderThreadStartResult.failed())
    return;

  try {
    while (!threadShouldExit()) {
      if (requiresPrepare()) {
        wait(-1);
        continue;
      }
      const int count = juce::jmin(renderQuantum, realtimeAudio.freeSamples());
      if (count == 0) {
        wait(-1);
        continue;
      }
      realtimeBlock.setSize(2, count, false, false, true);
      renderRealtimeBlock(realtimeBlock, realtimeMidi);
      if (bridge.getStatus() == PluginBridge::BridgeStatus::crashed)
        return;
      realtimeAudio.write(realtimeBlock);
    }
  } catch (const std::exception &error) {
    setLastPluginError(juce::String(L"音频渲染失败: ") + error.what());
    bridge.failRenderSession(getLastPluginError());
    midiPlayer.setPlaying(false);
  }
}

void AudioEngine::renderRealtimeBlock(juce::AudioBuffer<float> &buffer,
                                      juce::MidiBuffer &midiMessages) {
  juce::ScopedNoDenormals noDenormals;

  if (requiresPrepare()) {
    buffer.clear();
    midiMessages.clear();
    return;
  }

  const double renderSampleRate = preparedRenderSampleRate;
  if (!renderPluginBlock(midiPlayer, MidiPlayer::ProcessMode::realtime,
                         buffer, midiMessages, renderSampleRate)) {
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

  // Reason: `allSoundOff` 会立即切断声部；清理事件所在块静音，后续块按淡入采样数恢复增益。
  const bool resetBlock = midiPlayer.consumeSeekOccurred();
  if (resetBlock) {
    buffer.clear();
    seekCrossfadePhase = 2;
    seekCrossfadeSamples = seekCrossfadeDuration;
  }

  if (!resetBlock && seekCrossfadePhase == 2) {
    int toFade = juce::jmin(seekCrossfadeSamples, numSamples);
    float startGain =
        1.0f - (float)seekCrossfadeSamples / (float)seekCrossfadeDuration;
    float endGain = 1.0f - (float)(seekCrossfadeSamples - toFade) /
                               (float)seekCrossfadeDuration;
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
  } else if (!isPlaying) {
    buffer.clear();
  } else if (isPlaying && fadeOutSamples != fadeOutDuration) {
    fadeOutSamples = fadeOutDuration;
    stopCleanupDone = false;
  }

  // Ordering: 停止淡出完成后才要求 `MidiPlayer` 在下一消费块发送声部释放事件。
  if (!isPlaying && fadeOutSamples <= 0 && !stopCleanupDone) {
    midiPlayer.triggerStopCleanup();
    stopCleanupDone = true;
  }

  if (midiPlayer.consumeCleanupOccurred()) {
    buffer.clear();
  }

  outputGain.setTargetValue(masterVolume.load());
  for (int i = 0; i < numSamples; ++i) {
    const float gain = outputGain.getNextValue();
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
      buffer.setSample(ch, i, buffer.getSample(ch, i) * gain);
  }
}

bool AudioEngine::loadPlugin(const juce::PluginDescription &description,
                             std::function<void(double)> prepareMidi) {

  suspendProcessing(true);
  stopRealtimeRenderer();

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

  if (!preparePlugin(std::move(prepareMidi))) {
    setLastPluginError(bridge.getLastError());
    bridge.unloadPlugin();
    suspendProcessing(false);
    return false;
  }

  suspendProcessing(false);
  setLastPluginError({});
  LOG_DEBUG("Plugin loaded in worker: " + description.name);

  return true;
}

bool AudioEngine::renderPluginBlock(MidiPlayer &player, MidiPlayer::ProcessMode mode,
                                    juce::AudioBuffer<float> &buffer,
                                    juce::MidiBuffer &midiMessages,
                                    double sampleRate) {
  midiMessages.clear();
  buffer.clear();

  if (!bridge.isPluginLoaded()) {
    if (player.getPlaying()) {
      player.setPlaying(false);
      setLastPluginError(L"插件未加载。");
    }
    return false;
  }

  const int totalSamples = buffer.getNumSamples();
  if (totalSamples <= 0)
    return true;

  player.processBlock(midiMessages, totalSamples, mode,
                      PluginBridge::SharedBlockLayout::maxMidiBytes);
  for (int offset = 0; offset < totalSamples;
       offset += PluginBridge::SharedBlockLayout::maxSamples) {
    const int chunkSamples =
        PluginBridge::getSharedBlockChunkSize(totalSamples, offset);
    juce::AudioBuffer<float> chunk(buffer.getArrayOfWritePointers(),
                                   buffer.getNumChannels(), offset,
                                   chunkSamples);
    if (!bridge.processBlock(midiMessages, chunk, sampleRate, offset,
                             player.getMusicalPosition(offset)))
      break;
    if (offset + chunkSamples >= totalSamples)
      return true;
  }

  if (bridge.getStatus() == PluginBridge::BridgeStatus::crashed) {
    player.setPlaying(false);
    setLastPluginError(bridge.getLastError());
  }

  return false;
}

void AudioEngine::flushPluginStateBeforeUnload() {
  if (!bridge.isPluginLoaded())
    return;

  const double sampleRate = deviceSampleRate.load();
  const int flushBlockSize = juce::jlimit(1, 512, deviceBlockSize.load());
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
