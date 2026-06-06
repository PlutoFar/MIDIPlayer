#pragma once

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
    fileLogger.reset();
    debugEnabled = false;
  }

private:
  static inline bool debugEnabled = false;
  static inline std::unique_ptr<juce::FileLogger> fileLogger;
};
