#include "TestHarness.h"
#include "PluginBridge/PluginBridgeProtocol.h"

#include <juce_gui_basics/juce_gui_basics.h>
#include <future>
#include <string_view>

#if JUCE_WINDOWS
#include <windows.h>
#endif

namespace miditest {

void setTestEnvironmentVariable(const wchar_t *name, const wchar_t *value) {
#if JUCE_WINDOWS
  if (!SetEnvironmentVariableW(name, value))
    throw std::runtime_error("Could not set the test worker environment");
#else
  juce::ignoreUnused(name, value);
#endif
}

void clearTestEnvironmentVariable(const wchar_t *name) {
  setTestEnvironmentVariable(name, nullptr);
}

} // namespace miditest

namespace {

int runBridgeOnCommandThread() {
  // JUCE 在消息线程交付 IPC 心跳失效。阻塞命令与生产环境一样在工作线程运行。
  auto result = std::async(std::launch::async, miditest::runPluginBridgeTests);
  while (result.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready)
    juce::MessageManager::getInstance()->runDispatchLoopUntil(5);
  return result.get();
}

} // namespace

int main(int argc, char *argv[]) {
  juce::ScopedJuceInitialiser_GUI initialise;

  juce::StringArray arguments;
  for (int index = 1; index < argc; ++index) {
    const auto argument = juce::String::fromUTF8(argv[index]);
    arguments.add(argument.containsChar(' ') ? argument.quoted() : argument);
  }
  const auto commandLine = arguments.joinIntoString(" ");
  if (PluginBridge::isPluginWorkerCommandLine(commandLine))
    return miditest::runTestWorker(commandLine);

  const struct {
    std::string_view name;
    int (*run)();
  } suites[] = {
      {"midi-playback", miditest::runMidiPlaybackTests},
      {"plugin-bridge", runBridgeOnCommandThread},
      {"export-encoding", miditest::runExportEncodingTests},
      {"export-task", miditest::runExportTaskTests},
      {"playlist", miditest::runCoreTests},
      {"persistence", miditest::runPersistenceTests},
      {"shell", miditest::runShellTests},
      {"background", miditest::runBackgroundTests}};

  int failures = 0;
  bool matched = false;
  for (const auto &suite : suites) {
    if (argc != 1 && (argc != 3 || std::string_view(argv[1]) != "--suite" ||
                     argv[2] != suite.name))
      continue;
    matched = true;
    std::cout << "Running " << suite.name << '\n';
    try {
      failures += suite.run();
    } catch (const std::exception &error) {
      std::cerr << suite.name << ": " << error.what() << '\n';
      ++failures;
    }
  }
  if (!matched) {
    std::cerr << "Usage: MidiTests [--suite <name>]\n";
    return 2;
  }
  return failures == 0 ? 0 : 1;
}
