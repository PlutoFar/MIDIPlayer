#include "AudioEngine.h"
#include "../Utils/DebugLogger.h"
#include "ExportAudioProcessing.h"

AudioEngine::AudioEngine()
    : AudioProcessor(BusesProperties().withOutput(
          "Output", juce::AudioChannelSet::stereo(), true)) {}

AudioEngine::~AudioEngine() {
  suspendProcessing(true);
  midiPlayer.setPlaying(false);
  bridge.stop();
}

// Device callbacks publish configuration only; IPC belongs to the core task.
void AudioEngine::prepareToPlay(double sampleRate, int blockSize) {
  // MIDI snapshots have one producer, protected by Core::stateMutex. Device
  // callbacks must not publish another snapshot concurrently with seek/export.
  deviceSampleRate.store(sampleRate > 0.0 ? sampleRate : 44100.0);
  deviceBlockSize.store(blockSize > 0 ? blockSize : 512);
  deviceRevision.fetch_add(1);
}

bool AudioEngine::preparePlugin() {
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
  const bool ok = bridge.prepare(sampleRate, blockSize);
  if (ok)
    preparedRevision.store(revision);
  else
    setLastPluginError(bridge.getLastError());
  suspendProcessing(wasSuspended);
  return ok;
}

bool AudioEngine::prepareForOfflineExport(const ExportSettings &settings) {
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
    midiPlayer.setSampleRate(deviceSampleRate.load());
    offlineExportActive.store(false, std::memory_order_release);
    suspendProcessing(false);
    return false;
  }
  return true;
}

bool AudioEngine::restoreFromOfflineExport() {
  const double liveSampleRate = deviceSampleRate.load();
  const bool restored = bridge.isPluginLoaded() && preparePlugin();
  if (!restored)
    lastExportError = L"无法恢复实时音频: " + bridge.getLastError();
  midiPlayer.setSampleRate(liveSampleRate);
  midiPlayer.setPlaying(false);
  midiPlayer.seekTo(0.0);
  offlineExportActive.store(false, std::memory_order_release);
  suspendProcessing(false);
  return restored;
}

void AudioEngine::unloadPlugin() {
  suspendProcessing(true);
  midiPlayer.setPlaying(false);

  bridge.closeEditor();
  flushPluginStateBeforeUnload();
  bridge.unloadPlugin();

  suspendProcessing(false);
}

void AudioEngine::terminateCrashedPluginWorker() {
  suspendProcessing(true);
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
  juce::ScopedNoDenormals noDenormals;

  if (isOfflineExportActive() || requiresPrepare()) {
    buffer.clear();
    midiMessages.clear();
    return;
  }

  const double renderSampleRate = deviceSampleRate.load();
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

  // allSoundOff 在图内触发时，VST3 插件会在样本 0
  // 立即切断声部并产生波形不连续。 包含该事件的整块 buffer 静音，下一块从 0
  // 淡入到 1；48 kHz、256 samples 下空隙约 5 ms。
  if (midiPlayer.consumeSeekOccurred()) {
    buffer.clear();
    seekCrossfadePhase = 2;
    seekCrossfadeSamples = seekCrossfadeDuration;
  }

  if (seekCrossfadePhase == 2) {
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

bool AudioEngine::loadPlugin(const juce::PluginDescription &description) {

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

  if (!preparePlugin()) {
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

bool AudioEngine::renderPluginBlock(juce::AudioBuffer<float> &buffer,
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
