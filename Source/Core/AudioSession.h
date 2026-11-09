#pragma once

#include <juce_core/juce_core.h>
#include <optional>
#include <utility>

namespace midi {

// Core 在同一把锁内受理操作和更新播放状态。
class AudioSession {
public:
  enum class Operation {
    idle, loadMidi, changePlugin, pluginEditor, scanPlugins,
    configureDevice, prepareDevice, exportAudio, recoverWorker
  };

  bool begin(Operation requested) {
    if (!isIdle())
      return false;
    active = requested;
    return true;
  }

  void finish() { active = Operation::idle; }
  bool isIdle() const { return active == Operation::idle; }
  bool isScanning() const { return active == Operation::scanPlugins; }
  bool isExporting() const { return active == Operation::exportAudio; }
  bool hasControlTask() const { return !isIdle() && !isExporting(); }
  bool isChangingAudio() const {
    switch (active) {
    case Operation::loadMidi:
    case Operation::changePlugin:
    case Operation::configureDevice:
    case Operation::prepareDevice:
    case Operation::recoverWorker:
      return true;
    default:
      return false;
    }
  }
  bool allowsSequenceChange() const { return !isChangingAudio() && !isExporting(); }

  bool canPrepare(unsigned revision) const {
    return isIdle() && failedRevision != revision;
  }

  void preparationCompleted(unsigned revision, const juce::Result &result) {
    if (result.failed()) {
      failedRevision = revision;
      preparationError = result.getErrorMessage();
    } else {
      failedRevision.reset();
      preparationError.clear();
    }
  }

  juce::String deviceError(unsigned revision) const {
    return failedRevision == revision ? preparationError : juce::String{};
  }

  void requestEditorClose() { editorCloseRequested = true; }
  bool takeEditorCloseRequest() { return std::exchange(editorCloseRequested, false); }

  void noteWorkerFailure(juce::String diagnostic) {
    if (!workerDiagnostic)
      workerDiagnostic = std::move(diagnostic);
  }
  void clearWorkerFailure() { workerDiagnostic.reset(); }
  const std::optional<juce::String> &workerFailure() const { return workerDiagnostic; }

private:
  Operation active = Operation::idle;
  std::optional<unsigned> failedRevision;
  std::optional<juce::String> workerDiagnostic;
  juce::String preparationError;
  bool editorCloseRequested = false;
};

} // namespace midi
