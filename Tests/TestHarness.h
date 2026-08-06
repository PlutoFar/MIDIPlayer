#pragma once

#include <iostream>

// Shared, minimal test harness for suites split out of ExportAndHintTests.cpp.
// Each suite owns its own failure counter, prints "FAIL: ..." lines, and
// returns the count; the main runner in ExportAndHintTests.cpp sums them.
// See docs/winui3-refactor-plan.md (Test Plan).
namespace miditest {

inline void expect(int &failures, bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

// Per-area suites. Implemented in their own .cpp files.
int runWorkerPathTests();
int runCoreTests();
int runExportTaskTests();
int runPersistenceTests();
int runWinBridgeTests();

} // namespace miditest
