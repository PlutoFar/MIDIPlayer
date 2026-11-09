#include "TestHarness.h"
#include "Utils/CommandLineMidiFileSupport.h"
#include "Utils/WindowsFileAssociation.h"

namespace miditest {

int runShellTests() {
  int failures = 0;
  const auto expect = [&](bool condition, const char *message) {
    miditest::expect(failures, condition, message);
  };

  auto commandLineTempDir =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getNonexistentChildFile("midi-player-command-line-tests", "");
  expect(commandLineTempDir.createDirectory(),
         "command-line temporary directory should exist");
  const auto commandLineMidi =
      commandLineTempDir.getChildFile("song with spaces.mid");
  expect(commandLineMidi.replaceWithText("MThd"),
         "command-line test MIDI file should be writable");
  const auto commandLineText = commandLineTempDir.getChildFile("notes.txt");
  expect(commandLineText.replaceWithText("not midi"),
         "command-line non-MIDI file should be writable");

  expect(parseMidiFileFromCommandLine(commandLineMidi.getFullPathName()) ==
             commandLineMidi,
         "a single unquoted MIDI path with spaces should still be accepted");
  expect(parseMidiFileFromCommandLine("--from-shell " +
                                      commandLineMidi.getFullPathName().quoted()) ==
             commandLineMidi,
         "a quoted MIDI path after an option should be detected");
  expect(parseMidiFileFromCommandLine(commandLineText.getFullPathName().quoted() +
                                      " " +
                                      commandLineMidi.getFullPathName().quoted()) ==
             commandLineMidi,
         "command-line parsing should skip non-MIDI file arguments");
  commandLineTempDir.deleteRecursively();
  expect(makeMidiFileAssociationCommand("C:\\Program Files\\Midi Player\\Player.exe") ==
             "\"C:\\Program Files\\Midi Player\\Player.exe\" \"%1\"",
         "file association command should quote the executable and shell path");
  expect(getMidiFileProgId() == "ModernMidiPlayer.MIDIFile",
         "file association ProgID should remain stable");

  return failures;
}

} // namespace miditest
