#include "PluginProcessEntry.h"
#include "PluginScanProcess.h"
#include "PluginWorkerProcess.h"

namespace PluginBridge {

bool isPluginProcessCommandLine(const juce::String &commandLine) {
  return isPluginScanCommandLine(commandLine) ||
         isPluginWorkerCommandLine(commandLine);
}

bool runPluginProcess(const juce::String &commandLine) {
  return runPluginScanIfRequested(commandLine) ||
         runWorkerIfRequested(commandLine);
}

void shutDownPluginProcess() { workerInstance().reset(); }

} // namespace PluginBridge
