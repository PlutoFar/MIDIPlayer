#include "TestHarness.h"

#include "Core/Core.h"
#include "Core/PluginLoadNotification.h"
#include "Core/State.h"
#include "Midi/MidiPlayer.h"

#include <atomic>
#include <memory>
#include <thread>

namespace miditest {
namespace {

std::wstring filePath(const juce::File &file) {
  return std::wstring(file.getFullPathName().toWideCharPointer());
}

juce::File makeTestMidiFile(const juce::File &dir, const juce::String &name) {
  auto file = dir.getChildFile(name + ".mid");
  juce::MidiMessageSequence track;
  track.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100));
  track.addEvent(juce::MidiMessage::noteOff(1, 60), 480.0);
  juce::MidiFile midi;
  midi.setTicksPerQuarterNote(480);
  midi.addTrack(track);
  if (auto stream = file.createOutputStream())
    midi.writeTo(*stream);
  return file;
}

} // namespace

// Coverage: 核心默认状态、无插件播放拒绝、列表命令及并发快照一致性。
// Boundary: 这些检查不加载真实插件，也不验证窗口交互。
int runCoreTests() {
  int failures = 0;

  midi::AppState s;
  expect(failures, !s.transport.playing, "default AppState: not playing");
  expect(failures, !s.transport.hasSequence, "default AppState: no sequence");
  expect(failures, !s.plugin.loaded, "default AppState: no plugin loaded");
  expect(failures, !s.task.exportActive, "default AppState: no export active");
  expect(failures, midi::pluginLoadSuccessToastDurationMs == 3000,
         "plugin load success toast should stay open for 3 seconds");
  expect(failures,
         midi::makePluginLoadSuccessToastTitle(L"Ivory VST") ==
             L"插件加载成功: Ivory VST",
         "plugin load success toast should include the plugin name");
  expect(failures,
         midi::makePluginLoadSuccessToastTitle(L"") == L"插件加载成功",
         "plugin load success toast should handle an empty plugin name");

  {
    midi::Core core;
    const auto directory =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("midi-player-playback-" + juce::Uuid().toString());
    directory.createDirectory();
    const auto file = makeTestMidiFile(directory, "playback");
    core.loadMidiFile(filePath(file));
    core.play();
    expect(
        failures, !core.state().transport.playing,
        "Core::play should require both a MIDI sequence and a loaded plugin");
    directory.deleteRecursively();
  }

  {
    midi::Core core;

    const auto tempDir =
        juce::File::getSpecialLocation(juce::File::tempDirectory)
            .getChildFile("midi-player-core-command-tests-" +
                          juce::Uuid().toString());
    expect(failures, tempDir.createDirectory(),
           "Core playlist command temp directory should exist");

    const auto first = makeTestMidiFile(tempDir, "alpha");
    const auto second = makeTestMidiFile(tempDir, "beta");

    expect(failures, core.addToPlaylist(filePath(first)),
           "Core should add a MIDI file to the playlist");
    expect(failures, !core.addToPlaylist(filePath(first)),
           "Core should reject duplicate playlist entries by default");
    expect(failures,
           core.addFilesToPlaylist({filePath(first), filePath(second)}) == 1,
           "Core should report only newly added playlist files");

    auto playlistState = core.playlistState();
    expect(failures, playlistState.trackNames.size() == 2,
           "Core playlist state should expose added tracks");
    expect(failures,
           playlistState.trackNames[0] == L"alpha" &&
               playlistState.trackNames[1] == L"beta",
           "Core playlist state should preserve track display names");

    expect(failures, core.moveTrack(0, 1),
           "Core should move playlist tracks by index");
    playlistState = core.playlistState();
    expect(failures,
           playlistState.trackNames[0] == L"beta" &&
               playlistState.trackNames[1] == L"alpha",
           "Core playlist state should reflect reordering");

    core.setPlayMode(4);
    expect(failures, core.playlistState().playMode == 4,
           "Core should expose playback mode changes");
    core.setPlayMode(99);
    expect(failures, core.playlistState().playMode == 1,
           "Core should normalise unsupported playback modes");

    expect(failures, core.removeTrack(1),
           "Core should remove playlist tracks by index");
    expect(failures, core.playlistState().trackNames.size() == 1,
           "Core playlist state should reflect removals");

    std::atomic<bool> readerDone{false};
    std::atomic<bool> invalidSnapshot{false};
    std::thread reader([&]() {
      for (int i = 0; i < 2000; ++i) {
        const auto snapshot = core.playlistState();
        if (snapshot.trackNames.size() != snapshot.trackAvailable.size())
          invalidSnapshot = true;
      }
      readerDone = true;
    });
    for (int i = 0; i < 500; ++i) {
      core.setPlayMode((i % 4) + 1);
      core.volume(static_cast<float>(i % 101) / 100.0f);
    }
    reader.join();
    expect(failures, readerDone && !invalidSnapshot,
           "Core state snapshots should remain consistent during concurrent "
           "writes");

    core.clearPlaylist();
    expect(failures, core.playlistState().trackNames.empty(),
           "Core playlist state should reflect clear");
    tempDir.deleteRecursively();
  }

  return failures;
}

} // namespace miditest
