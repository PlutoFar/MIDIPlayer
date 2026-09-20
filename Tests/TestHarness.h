#pragma once

#include <iostream>

// Contract: 每个测试组持有独立失败计数，失败输出诊断并返回数量。
// Ordering: `ExportAndHintTests.cpp` 汇总各组计数并设置进程退出码，失败不得被后续组覆盖。
namespace miditest {

inline void expect(int &failures, bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

// Preconditions: JUCE 消息管理器已初始化；各组自行管理其临时文件和进程。
int runWorkerPathTests();
int runCoreTests();
int runExportTaskTests();
int runPersistenceTests();

} // namespace miditest
