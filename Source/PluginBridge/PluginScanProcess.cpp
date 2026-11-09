#include "PluginScanProcess.h"
#include "PluginListSupport.h"
#include "WorkerPath.h"

#include <array>
#include <thread>

namespace PluginBridge {
namespace {

// Postconditions: 汇总系统、用户及便携目录；只枚举路径，不创建目录或加载插件。
juce::FileSearchPath getVst3SearchPaths() {
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

// Preconditions: 只在扫描子进程使用；插件发现可能执行第三方代码。
// Failures: 输入或输出无效时返回失败，由父进程保留原目录。
bool scanPluginListInCurrentProcess(const juce::File &inputFile,
                                           const juce::File &outputFile) {
  const auto input = juce::XmlDocument::parse(inputFile);
  if (input == nullptr || !input->hasTagName("KNOWNPLUGINS"))
    return false;
  juce::KnownPluginList list;
  list.recreateFromXml(*input);

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

juce::Result runScannerProcess(const juce::StringArray &args,
                               const juce::File &transcriptFile,
                               const std::function<bool()> &shouldCancel) {
  if (shouldCancel && shouldCancel())
    return juce::Result::fail(L"插件扫描已取消。");

  juce::FileOutputStream transcript(transcriptFile);
  if (!transcript.openedOk())
    return transcript.getStatus();

  juce::ChildProcess process;
  if (!process.start(args, juce::ChildProcess::wantStdOut |
                               juce::ChildProcess::wantStdErr))
    return juce::Result::fail(L"无法启动插件扫描子进程。");

  std::thread drain;
  try {
    // 持续读取管道，避免等待退出时与子进程的日志写入互相阻塞。
    drain = std::thread([&] {
      std::array<char, 4096> buffer;
      for (;;) {
        const int bytes = process.readProcessOutput(buffer.data(),
                                                    static_cast<int>(buffer.size()));
        if (bytes <= 0)
          break;
        if (transcript.getStatus().wasOk())
          transcript.write(buffer.data(), static_cast<size_t>(bytes));
      }
      transcript.flush();
    });
  } catch (const std::system_error &error) {
    process.kill();
    process.waitForProcessToFinish(-1);
    return juce::Result::fail(juce::String(L"无法启动扫描日志读取线程: ") + error.what());
  }

  bool cancelled = false;
  bool terminationFailed = false;
  while (!process.waitForProcessToFinish(50)) {
    if (!shouldCancel || !shouldCancel())
      continue;
    cancelled = process.kill();
    terminationFailed = !cancelled && process.isRunning();
    process.waitForProcessToFinish(-1);
    break;
  }
  drain.join();

  if (cancelled)
    return juce::Result::fail(L"插件扫描已取消。");
  if (terminationFailed)
    return juce::Result::fail(L"无法终止插件扫描子进程。");
  if (transcript.getStatus().failed())
    return transcript.getStatus();
  if (const auto exitCode = process.getExitCode(); exitCode != 0) {
    auto message = L"插件扫描子进程异常退出 (" + juce::String(exitCode) + L")。";
    const auto diagnostic = transcriptFile.loadFileAsString().trim();
    if (diagnostic.isNotEmpty())
      message += "\n" + diagnostic;
    return juce::Result::fail(message);
  }
  return juce::Result::ok();
}

} // namespace

// Contract: 识别扫描标志只决定入口分流，不证明后续路径参数有效。
bool isPluginScanCommandLine(const juce::String &commandLine) {
  juce::StringArray args;
  args.addTokens(commandLine, true);
  return args.contains(pluginScanCommandLineFlag);
}

// Postconditions: 命中扫描标志即返回 `true` 并安排退出；执行成败通过进程退出码报告。
bool runPluginScanIfRequested(const juce::String &commandLine) {
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

  const juce::File input(args[flagIndex + 1].unquoted());
  const juce::File output(args[flagIndex + 2].unquoted());
  const bool succeeded = scanPluginListInCurrentProcess(input, output);
  if (auto *application = juce::JUCEApplicationBase::getInstance())
    application->setApplicationReturnValue(succeeded ? 0 : 1);
  juce::MessageManager::callAsync([] { juce::JUCEApplicationBase::quit(); });
  return true;
}

juce::Result scanPluginListInChildProcess(
    const juce::KnownPluginList &source, juce::KnownPluginList &destination,
    const std::function<bool()> &shouldCancel, const juce::File &executableOverride) {
  juce::TemporaryFile input(".xml");
  juce::TemporaryFile output(".xml");
  const auto sourceXml = source.createXml();
  if (sourceXml == nullptr || !sourceXml->writeTo(input.getFile()))
    return juce::Result::fail(L"无法创建插件扫描输入文件。");

  const auto executable = executableOverride == juce::File{}
                              ? WorkerPath::resolve() : executableOverride;
  if (!executable.existsAsFile())
    return juce::Result::fail(L"未找到插件扫描子进程。");

  juce::TemporaryFile transcript(".log");
  const juce::StringArray args{executable.getFullPathName(), pluginScanCommandLineFlag,
                               input.getFile().getFullPathName(),
                               output.getFile().getFullPathName()};
  const auto result = runScannerProcess(args, transcript.getFile(), shouldCancel);
  if (result.failed())
    return result;

  const auto xml = juce::XmlDocument::parse(output.getFile());
  if (xml == nullptr || !xml->hasTagName("KNOWNPLUGINS"))
    return juce::Result::fail(L"插件扫描结果无效。");
  destination.recreateFromXml(*xml);
  return juce::Result::ok();
}

} // namespace PluginBridge
