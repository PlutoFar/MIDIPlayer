#include "AudioThreadPriority.h"
#include "../Utils/DebugLogger.h"

#if JUCE_WINDOWS
#include <windows.h>
#include <avrt.h>
#endif

juce::Result AudioThreadPriority::enableRealtime() {
  jassert(taskHandle == nullptr);
#if JUCE_WINDOWS
  DWORD taskIndex = 0;
  taskHandle = AvSetMmThreadCharacteristicsW(L"Pro Audio", &taskIndex);
  if (taskHandle == nullptr)
    return juce::Result::fail(
        L"音频线程注册失败 (AvSetMmThreadCharacteristicsW): Windows " +
        juce::String(static_cast<int>(GetLastError())));

  if (!AvSetMmThreadPriority(taskHandle, AVRT_PRIORITY_NORMAL))
    return juce::Result::fail(
        L"音频线程优先级设置失败 (AvSetMmThreadPriority): Windows " +
        juce::String(static_cast<int>(GetLastError())));

  return juce::Result::ok();
#else
  return juce::Result::fail("Windows multimedia scheduling is required");
#endif
}

AudioThreadPriority::~AudioThreadPriority() {
#if JUCE_WINDOWS
  if (taskHandle != nullptr && !AvRevertMmThreadCharacteristics(taskHandle))
    LOG_DEBUG("AvRevertMmThreadCharacteristics failed: Windows " +
              juce::String(static_cast<int>(GetLastError())));
#endif
}
