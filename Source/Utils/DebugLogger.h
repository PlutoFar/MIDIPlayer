#pragma once

// Contract: 启动时根据 `portable_debug.dat` 配置进程全局日志，工作线程结束后再关闭日志。
// Concurrency: 日志开关在工作线程启动前确定；`init`/`shutdown` 不得与日志调用并发修改配置。
// Side effect: 诊断模式写入程序目录日志；`ScopedTimer` 的阈值单位为毫秒。

#include <juce_core/juce_core.h>

class DebugLogger {
public:
  static bool isDebugMode() { return debugEnabled; }

  static void log(const juce::String &message) {
    if (!debugEnabled)
      return;

    juce::String timestamp =
        juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::String fullMessage = "[" + timestamp + "] " + message;

    juce::Logger::writeToLog(fullMessage);

    DBG(fullMessage);
  }

  static void init() {
    auto exeFile =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto exeDir = exeFile.getParentDirectory();

    auto debugMarker = exeDir.getChildFile("portable_debug.dat");
    debugEnabled = debugMarker.existsAsFile();

    if (!debugEnabled)
      return;

    auto logFile = exeDir.getChildFile("debug_log.txt");

    static std::unique_ptr<juce::FileLogger> fileLogger;
    fileLogger = std::make_unique<juce::FileLogger>(
        logFile, "Modern MIDI Player Debug Log", 0);

    juce::Logger::setCurrentLogger(fileLogger.get());

    log("--- Debug Logger Initialized ---");
    log("Log file: " + logFile.getFullPathName());
  }

  static void shutdown() {
    if (!debugEnabled)
      return;
    log("--- Debug Logger Shutdown ---");
    juce::Logger::setCurrentLogger(nullptr);
  }

private:
  static inline bool debugEnabled = false;
};

#define LOG_DEBUG(msg) DebugLogger::log(msg)

class ScopedTimer {
public:
  ScopedTimer(const juce::String &actionName, int msThreshold = 50)
      : name(actionName), threshold(msThreshold),
        startTime(juce::Time::getMillisecondCounter()) {}

  ~ScopedTimer() {
    auto duration = juce::Time::getMillisecondCounter() - startTime;
    if (duration >= (uint32_t)threshold) {
      LOG_DEBUG("PERF WARNING: [" + name + "] took " + juce::String(duration) +
                " ms (threshold: " + juce::String(threshold) + ")");
    } else if (threshold < 0) {
      LOG_DEBUG("PERF: [" + name + "] took " + juce::String(duration) + " ms");
    }
  }

private:
  juce::String name;
  int threshold;
  uint32_t startTime;
};

#define SCOPED_TIMER(name) ScopedTimer __timer_##__LINE__(name)
#define SCOPED_TIMER_SLOW(name, ms) ScopedTimer __timer_##__LINE__(name, ms)
#define SCOPED_TIMER_ALWAYS(name) ScopedTimer __timer_##__LINE__(name, -1)
