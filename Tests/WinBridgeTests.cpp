#include "TestHarness.h"

#include "Core/WinBridge.h"

#include <juce_core/juce_core.h>

namespace miditest {
namespace {

std::wstring filePath(const juce::File &file) {
  return std::wstring(file.getFullPathName().toWideCharPointer());
}

juce::File makeMidiFile(const juce::File &dir, const juce::String &name) {
  auto file = dir.getChildFile(name + ".mid");
  file.replaceWithText("MThd");
  return file;
}

} // namespace

int runWinBridgeTests() {
  int failures = 0;

  midi::WinCore bridge;
  if (!bridge.init(false)) {
    expect(failures, false, "WinBridge init should succeed");
    return failures;
  }

  const auto tempDir =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getChildFile("midi-player-winbridge-tests-" +
                        juce::Uuid().toString());
  expect(failures, tempDir.createDirectory(),
         "WinBridge temp directory should exist");

  const auto first = makeMidiFile(tempDir, "bridge-alpha");
  const auto second = makeMidiFile(tempDir, "bridge-beta");

  expect(failures, bridge.addToPlaylist(filePath(first)),
         "WinBridge should forward addToPlaylist");
  expect(failures,
         bridge.addFiles({filePath(first), filePath(second)}) == 1,
         "WinBridge should forward addFiles and preserve duplicate handling");

  auto state = bridge.state();
  expect(failures, state.tracks.size() == 2,
         "WinBridge state should expose Core playlist tracks");
  expect(failures, state.tracks[0] == L"bridge-alpha" &&
                       state.tracks[1] == L"bridge-beta",
         "WinBridge state should preserve track names");

  bridge.setPlayMode(4);
  expect(failures, bridge.state().playMode == 4,
         "WinBridge should forward play mode changes");

  expect(failures, bridge.moveTrack(0, 1),
         "WinBridge should forward playlist reordering");
  state = bridge.state();
  expect(failures, state.tracks[0] == L"bridge-beta" &&
                       state.tracks[1] == L"bridge-alpha",
         "WinBridge state should reflect reordered tracks");

  expect(failures, bridge.removeTrack(1),
         "WinBridge should forward playlist removal");
  expect(failures, bridge.state().tracks.size() == 1,
         "WinBridge state should reflect removal");

  midi::WinExportRequest exportRequest;
  expect(failures, !bridge.exportAudio(exportRequest),
         "WinBridge should reject export without a target path");

  auto waitForExport = [&bridge]() {
    const auto deadline = juce::Time::getMillisecondCounter() + 3000u;
    midi::WinState result;
    do {
      result = bridge.state();
      if (result.exportFinished)
        return result;
      juce::Thread::sleep(5);
    } while (juce::Time::getMillisecondCounter() < deadline);
    return result;
  };
  exportRequest.targetPath = filePath(tempDir.getChildFile("export-one.wav"));
  expect(failures, bridge.exportAudio(exportRequest),
         "WinBridge should start the first export session");
  const auto firstExport = waitForExport();
  expect(failures, firstExport.exportFinished &&
                       firstExport.exportSessionId == 1,
         "WinBridge should publish the first export session result");
  exportRequest.targetPath = filePath(tempDir.getChildFile("export-two.wav"));
  expect(failures, bridge.exportAudio(exportRequest),
         "WinBridge should start the second export session");
  const auto secondExport = waitForExport();
  expect(failures, secondExport.exportFinished &&
                       secondExport.exportSessionId == 2,
         "WinBridge should publish each export under a distinct session id");

  expect(failures, !bridge.saveList(filePath(tempDir)),
         "WinBridge should report a playlist save failure");
  expect(failures, !bridge.state().playlistError.empty(),
         "WinBridge state should expose the playlist save error");

  expect(failures, bridge.loadAsync(L"missing-plugin-id-1"),
         "WinBridge should start an asynchronous plugin load request");
  auto waitForLoad = [&bridge]() {
    const auto deadline = juce::Time::getMillisecondCounter() + 3000u;
    midi::WinState result;
    do {
      result = bridge.state();
      if (result.loadFinished)
        return result;
      juce::Thread::sleep(5);
    } while (juce::Time::getMillisecondCounter() < deadline);
    return result;
  };
  const auto firstLoad = waitForLoad();
  expect(failures, firstLoad.loadFinished && !firstLoad.loadSucceeded &&
                       firstLoad.loadSessionId == 1,
         "WinBridge should publish the first asynchronous load result");
  expect(failures, bridge.loadAsync(L"missing-plugin-id-2"),
         "WinBridge should start a second asynchronous plugin load request");
  const auto secondLoad = waitForLoad();
  expect(failures, secondLoad.loadFinished && !secondLoad.loadSucceeded &&
                       secondLoad.loadSessionId == 2,
         "WinBridge should publish each load under a distinct session id");

  bridge.clearPlaylist();
  expect(failures, bridge.state().tracks.empty(),
         "WinBridge should forward playlist clear");

  tempDir.deleteRecursively();
  return failures;
}

} // namespace miditest
