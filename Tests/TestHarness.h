#pragma once

#include <juce_core/juce_core.h>
#include <iostream>
#include <stdexcept>

// Contract: 每个测试组持有独立失败计数，失败输出诊断并返回数量。
namespace miditest {

inline void expect(int &failures, bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

class TestDirectory {
public:
  TestDirectory()
      : path(juce::File::getSpecialLocation(juce::File::tempDirectory)
                 .getChildFile("midi-player-test-" + juce::Uuid().toString())) {
    if (const auto result = path.createDirectory(); result.failed())
      throw std::runtime_error(result.getErrorMessage().toStdString());
  }
  ~TestDirectory() { path.deleteRecursively(); }
  TestDirectory(const TestDirectory &) = delete;
  TestDirectory &operator=(const TestDirectory &) = delete;

  const juce::File path;
};

int runMidiPlaybackTests();
int runPluginBridgeTests();
int runTestWorker(const juce::String &commandLine);
void setTestEnvironmentVariable(const wchar_t *name, const wchar_t *value);
void clearTestEnvironmentVariable(const wchar_t *name);
int runExportEncodingTests();
int runCoreTests();
int runExportTaskTests();
int runPersistenceTests();
int runShellTests();
int runBackgroundTests();
int runAudioSessionTests();

} // namespace miditest
