#include "TestHarness.h"

#include "Core/WorkerPath.h"
#include "PluginBridge/PluginScanProcess.h"

#include <juce_core/juce_core.h>

namespace miditest {

// Worker executable path resolution, fallback, and scanner crash containment.
int runWorkerPathTests() {
  int failures = 0;

  const auto exe =
      juce::File::getSpecialLocation(juce::File::currentExecutableFile);
  const auto resolved = midi::WorkerPath::resolve();

  expect(failures, resolved.existsAsFile(),
         "WorkerPath::resolve() should return an existing executable");
  expect(failures, resolved.getParentDirectory() == exe.getParentDirectory(),
         "worker executable should sit in the application directory");

  const auto sibling =
      exe.getParentDirectory().getChildFile("MidiWorker.exe");
  if (sibling.existsAsFile())
    expect(failures, resolved == sibling,
           "resolve() must prefer a dedicated MidiWorker.exe when present");
  else
    expect(failures, resolved == exe,
           "resolve() must fall back to the current executable (self-host)");

#if JUCE_WINDOWS
  juce::KnownPluginList source;
  juce::KnownPluginList destination;
  juce::String scanError;
  const juce::File failingChild("C:\\Windows\\System32\\where.exe");
  expect(failures, failingChild.existsAsFile(),
         "scanner crash test child should exist");
  const bool scanSucceeded = PluginBridge::scanPluginListInChildProcess(
      source, destination, {}, scanError, failingChild);
  expect(failures, !scanSucceeded && scanError.isNotEmpty(),
         "an abnormal scanner child exit should be contained and diagnosed");
#endif

  return failures;
}

} // namespace miditest
