#include "TestHarness.h"

#include "Core/Core.h"

#include <juce_core/juce_core.h>

namespace miditest {

// Core/ExportTask state: progress, cancellation, result, and visible errors.
// Encoder and format coverage remains in ExportAndHintTests.cpp.
int runExportTaskTests() {
  int failures = 0;

  midi::Core core;
  expect(failures, core.init(), "Core export test init should succeed");

  const auto tempDir =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("midi-player-export-task-tests-" +
                        juce::Uuid().toString());
  expect(failures, tempDir.createDirectory(),
         "ExportTask temp directory should exist");
  const auto target = tempDir.getChildFile("missing-plugin.wav");

  midi::Core::ExportRequest request;
  request.trackIndex = 0;
  request.targetPath = std::wstring(target.getFullPathName().toWideCharPointer());
  request.formatName = L"WAV";
  request.sampleRate = 44100.0;
  request.bitDepth = 24;
  request.useFloatingPoint = false;
  request.autoTail = true;

  bool progressCalled = false;
  const auto result = core.runExport(
      request, [&progressCalled](float) { progressCalled = true; },
      [] { return false; });

  expect(failures, result == midi::Core::ExportResult::Failed,
         "ExportTask should fail when no exportable track/plugin is available");
  expect(failures, !core.state().task.exportActive,
         "ExportTask should clear exportActive after failure");
  expect(failures, !core.lastExportError().empty(),
         "ExportTask failure should preserve a visible error");
  expect(failures, !progressCalled,
         "ExportTask should not report render progress before export starts");

  core.shutdown();
  tempDir.deleteRecursively();
  return failures;
}

} // namespace miditest
