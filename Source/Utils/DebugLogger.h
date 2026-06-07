#pragma once

#include <juce_core/juce_core.h>

/**
    DebugLogger: 一个简单的静态日志类，将日志输出到程序目录下的 debug_log.txt。
*/
class DebugLogger {
public:
  // 检查是否启用了调试模式（程序目录下存在 portable_debug.dat）
  static bool isDebugMode() { return debugEnabled; }

  static void log(const juce::String &message) {
    // 仅在调试模式下记录日志
    if (!debugEnabled)
      return;

    juce::String timestamp =
        juce::Time::getCurrentTime().toString(true, true, true, true);
    juce::String fullMessage = "[" + timestamp + "] " + message;

    juce::Logger::writeToLog(fullMessage);

    // 同时输出到 DBG() 以便在 IDE 中查看
    DBG(fullMessage);
  }

  static void init() {
    auto exeFile =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile);
    auto exeDir = exeFile.getParentDirectory();

    // 仅当 portable_debug.dat 存在时才启用调试日志
    // 用户需要将 portable.dat 重命名为 portable_debug.dat 来开启调试模式
    auto debugMarker = exeDir.getChildFile("portable_debug.dat");
    debugEnabled = debugMarker.existsAsFile();

    if (!debugEnabled)
      return;

    auto logFile = exeDir.getChildFile("debug_log.txt");

    // 创建文件记录器，参数：文件、标题、单条消息最大长度
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
  // 调试模式标志，由 init() 根据 portable_debug.dat 是否存在来设置
  static inline bool debugEnabled = false;
};

// 宏定义方便使用
#define LOG_DEBUG(msg) DebugLogger::log(msg)

/**
    ScopedTimer: 用于测量代码块执行时间的辅助类。
    如果执行时间超过阈值，会自动记录日志。
*/
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
    } else if (threshold < 0) { // 如果阈值为负，则总是记录耗时
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
