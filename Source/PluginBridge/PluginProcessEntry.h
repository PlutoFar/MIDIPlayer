#pragma once

#include <juce_core/juce_core.h>

namespace PluginBridge {

bool isPluginProcessCommandLine(const juce::String &commandLine);
bool runPluginProcess(const juce::String &commandLine);
void shutDownPluginProcess();

} // namespace PluginBridge
