#pragma once

#include "../AudioEngine/PluginListSupport.h"
#include "../Core/WorkerPath.h"

#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

namespace PluginBridge {

inline constexpr const char *pluginScanCommandLineFlag =
    "--midi-plugin-scan";

inline juce::FileSearchPath getVst3SearchPaths() {
  juce::FileSearchPath searchPath;

  const auto exe =
      juce::File::getSpecialLocation(juce::File::currentExecutableFile);
  const auto exeDir = exe.getParentDirectory();
  const bool portable = exeDir.getChildFile("portable.dat").existsAsFile() ||
                        exeDir.getChildFile("portable_debug.dat").existsAsFile();
  if (portable) {
    const auto localVst3 = exeDir.getChildFile("VST3");
    if (localVst3.isDirectory())
      searchPath.add(localVst3);
  }

  const juce::File commonFiles("C:\\Program Files\\Common Files");
  if (commonFiles.isDirectory())
    searchPath.add(commonFiles.getChildFile("VST3"));

  searchPath.add(
      juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory)
          .getChildFile("VST3"));

  const juce::StringArray additionalPaths = {
      "C:\\Program Files\\VSTPlugins",
      "C:\\Program Files\\Steinberg\\VSTPlugins",
      "C:\\Program Files\\Native Instruments",
      "C:\\Program Files\\Common Files\\Native Instruments\\VST3",
      "C:\\VST3", "C:\\VSTPlugins"};
  for (const auto &path : additionalPaths) {
    const juce::File directory(path);
    if (directory.isDirectory())
      searchPath.add(directory);
  }

  return searchPath;
}

inline bool scanPluginListInCurrentProcess(const juce::File &inputFile,
                                           const juce::File &outputFile) {
  juce::KnownPluginList list;
  if (auto input = juce::XmlDocument::parse(inputFile)) {
    if (!input->hasTagName("KNOWNPLUGINS"))
      return false;
    list.recreateFromXml(*input);
  }

  juce::AudioPluginFormatManager formats;
  formats.addFormat(std::make_unique<juce::VST3PluginFormat>());
  removeMissingPluginTypes(list, formats);

  const auto searchPath = getVst3SearchPaths();
  for (int i = 0; i < formats.getNumFormats(); ++i) {
    auto *format = formats.getFormat(i);
    if (format == nullptr || format->getName() != "VST3")
      continue;

    juce::PluginDirectoryScanner scanner(list, *format, searchPath, true,
                                         juce::File(), false);
    juce::String name;
    while (scanner.scanNextFile(true, name)) {
    }
  }

  auto output = list.createXml();
  return output != nullptr && output->writeTo(outputFile);
}

inline bool isPluginScanCommandLine(const juce::String &commandLine) {
  juce::StringArray args;
  args.addTokens(commandLine, true);
  return args.contains(pluginScanCommandLineFlag);
}

inline bool runPluginScanIfRequested(const juce::String &commandLine) {
  juce::StringArray args;
  args.addTokens(commandLine, true);
  const int flagIndex = args.indexOf(pluginScanCommandLineFlag);
  if (flagIndex < 0)
    return false;

  if (flagIndex + 2 >= args.size()) {
    if (auto *application = juce::JUCEApplicationBase::getInstance())
      application->setApplicationReturnValue(1);
    juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
    return true;
  }

  const juce::File input(args[flagIndex + 1]);
  const juce::File output(args[flagIndex + 2]);
  const bool succeeded = scanPluginListInCurrentProcess(input, output);
  if (auto *application = juce::JUCEApplicationBase::getInstance())
    application->setApplicationReturnValue(succeeded ? 0 : 1);
  juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
  return true;
}

inline bool scanPluginListInChildProcess(
    const juce::KnownPluginList &source, juce::KnownPluginList &destination,
    const std::function<bool()> &shouldCancel, juce::String &error,
    const juce::File &executableOverride = {}) {
  juce::TemporaryFile input(".xml");
  juce::TemporaryFile output(".xml");
  const auto sourceXml = source.createXml();
  if (sourceXml == nullptr || !sourceXml->writeTo(input.getFile())) {
    error = L"无法创建插件扫描输入文件。";
    return false;
  }

  const auto executable = executableOverride == juce::File{}
                              ? midi::WorkerPath::resolve()
                              : executableOverride;
  if (!executable.existsAsFile()) {
    error = L"未找到插件扫描子进程。";
    return false;
  }

  juce::StringArray args{executable.getFullPathName(),
                         pluginScanCommandLineFlag,
                         input.getFile().getFullPathName(),
                         output.getFile().getFullPathName()};
  juce::ChildProcess process;
  if (!process.start(args, juce::ChildProcess::wantStdOut |
                               juce::ChildProcess::wantStdErr)) {
    error = L"无法启动插件扫描子进程。";
    return false;
  }

  while (!process.waitForProcessToFinish(50)) {
    if (shouldCancel && shouldCancel()) {
      process.kill();
      error = L"插件扫描已取消。";
      return false;
    }
  }

  if (process.getExitCode() != 0) {
    error = L"插件扫描子进程异常退出。";
    const auto diagnostics = process.readAllProcessOutput().trim();
    if (diagnostics.isNotEmpty())
      error += "\n" + diagnostics;
    return false;
  }

  auto result = juce::XmlDocument::parse(output.getFile());
  if (result == nullptr || !result->hasTagName("KNOWNPLUGINS")) {
    error = L"插件扫描结果无效。";
    return false;
  }

  destination.recreateFromXml(*result);
  error.clear();
  return true;
}

} // namespace PluginBridge
