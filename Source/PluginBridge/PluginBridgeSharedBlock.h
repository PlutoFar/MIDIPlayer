#pragma once

#include <juce_core/juce_core.h>

#include <cstdint>

#if JUCE_WINDOWS
extern "C" {
typedef void *HANDLE;
typedef void *LPSECURITY_ATTRIBUTES;
typedef unsigned long DWORD;
typedef int BOOL;
typedef const wchar_t *LPCWSTR;

#ifndef PAGE_READWRITE
#define PAGE_READWRITE 0x04
#endif
#ifndef FILE_MAP_ALL_ACCESS
#define FILE_MAP_ALL_ACCESS 0x000F001F
#endif
#ifndef WAIT_OBJECT_0
#define WAIT_OBJECT_0 0x00000000L
#endif
#ifndef WAIT_TIMEOUT
#define WAIT_TIMEOUT 0x00000102L
#endif
#ifndef WAIT_FAILED
#define WAIT_FAILED 0xFFFFFFFFL
#endif
#ifndef INFINITE
#define INFINITE 0xFFFFFFFFL
#endif
#ifndef INVALID_HANDLE_VALUE
#define INVALID_HANDLE_VALUE ((HANDLE)(intptr_t)-1)
#endif

__declspec(dllimport) HANDLE __stdcall
CreateFileMappingW(HANDLE hFile, LPSECURITY_ATTRIBUTES lpAttributes,
                   DWORD flProtect, DWORD dwMaximumSizeHigh,
                   DWORD dwMaximumSizeLow, LPCWSTR lpName);
__declspec(dllimport) void *__stdcall MapViewOfFile(
    HANDLE hFileMappingObject, DWORD dwDesiredAccess, DWORD dwFileOffsetHigh,
    DWORD dwFileOffsetLow, size_t dwNumberOfBytesToMap);
__declspec(dllimport) BOOL __stdcall UnmapViewOfFile(const void *lpBaseAddress);
__declspec(dllimport) HANDLE __stdcall
CreateEventW(LPSECURITY_ATTRIBUTES lpEventAttributes, BOOL bManualReset,
             BOOL bInitialState, LPCWSTR lpName);
__declspec(dllimport) BOOL __stdcall SetEvent(HANDLE hEvent);
__declspec(dllimport) DWORD __stdcall WaitForSingleObject(HANDLE hHandle,
                                                         DWORD dwMilliseconds);
__declspec(dllimport) BOOL __stdcall CloseHandle(HANDLE hObject);
__declspec(dllimport) DWORD __stdcall GetLastError(void);
}
#endif

namespace PluginBridge {

struct SharedBlockLayout {
  static constexpr int maxChannels = 2;
  static constexpr int maxSamples = 1024;
  static constexpr int maxMidiBytes = 8192;
};

inline int getSharedBlockChunkCount(int totalSamples) {
  return totalSamples > 0
             ? (totalSamples + SharedBlockLayout::maxSamples - 1) /
                   SharedBlockLayout::maxSamples
             : 0;
}

inline int getSharedBlockChunkSize(int totalSamples, int startSample) {
  if (totalSamples <= 0 || startSample < 0 || startSample >= totalSamples)
    return 0;
  return juce::jmin(SharedBlockLayout::maxSamples,
                    totalSamples - startSample);
}

struct SharedBlockHeader {
  double sampleRate = 44100.0;
  int blockSize = 512;
  int midiBytes = 0;
  int resultCode = 0;
};

struct SharedBlock {
  SharedBlockHeader header;
  float audio[SharedBlockLayout::maxChannels][SharedBlockLayout::maxSamples]{};
  unsigned char midi[SharedBlockLayout::maxMidiBytes]{};
};

inline int getWorkerProcessChannelCount(int pluginOutputChannels) {
  return juce::jmax(SharedBlockLayout::maxChannels, pluginOutputChannels);
}

inline bool shouldUseExplicitStereoHostConfig(int pluginOutputChannels) {
  return pluginOutputChannels <= SharedBlockLayout::maxChannels;
}

enum class WaitResult { signalled, timedOut, failed };

class SharedBlockOwner {
public:
  explicit SharedBlockOwner(const juce::String &name) : mappingName(name) {
#if JUCE_WINDOWS
    openWindowsHandles();
#else
    lastError = "plugin bridge shared blocks are Windows-only";
#endif
  }

  ~SharedBlockOwner() {
#if JUCE_WINDOWS
    if (mappedBlock != nullptr)
      UnmapViewOfFile(mappedBlock);
    if (responseEvent != nullptr)
      CloseHandle(responseEvent);
    if (requestEvent != nullptr)
      CloseHandle(requestEvent);
    if (mappingHandle != nullptr)
      CloseHandle(mappingHandle);
#endif
  }

  SharedBlockOwner(const SharedBlockOwner &) = delete;
  SharedBlockOwner &operator=(const SharedBlockOwner &) = delete;

  bool isOpen() const { return mappedBlock != nullptr && requestEvent != nullptr && responseEvent != nullptr; }

  SharedBlock &block() {
    jassert(mappedBlock != nullptr);
    return *mappedBlock;
  }

  const SharedBlock &block() const {
    jassert(mappedBlock != nullptr);
    return *mappedBlock;
  }

  juce::String getName() const { return mappingName; }
  juce::String getLastErrorMessage() const { return lastError; }

  bool signalRequest() { return signal(requestEvent, "request"); }
  bool signalResponse() { return signal(responseEvent, "response"); }

  WaitResult waitForRequest(int timeoutMs) {
    return wait(requestEvent, timeoutMs);
  }

  WaitResult waitForResponse(int timeoutMs) {
    return wait(responseEvent, timeoutMs);
  }

private:
#if JUCE_WINDOWS
  void openWindowsHandles() {
    if (mappingName.isEmpty()) {
      lastError = "empty shared block name";
      return;
    }

    const auto mappingWide = mappingName.toWideCharPointer();
    mappingHandle = CreateFileMappingW(
        INVALID_HANDLE_VALUE, nullptr, PAGE_READWRITE, 0,
        static_cast<DWORD>(sizeof(SharedBlock)), mappingWide);
    if (mappingHandle == nullptr) {
      setLastWindowsError("CreateFileMappingW failed");
      return;
    }

    mappedBlock = static_cast<SharedBlock *>(
        MapViewOfFile(mappingHandle, FILE_MAP_ALL_ACCESS, 0, 0,
                      sizeof(SharedBlock)));
    if (mappedBlock == nullptr) {
      setLastWindowsError("MapViewOfFile failed");
      return;
    }

    const auto requestEventName = mappingName + ".request";
    requestEvent =
        CreateEventW(nullptr, false, false, requestEventName.toWideCharPointer());
    if (requestEvent == nullptr) {
      setLastWindowsError("CreateEventW request failed");
      return;
    }

    const auto responseEventName = mappingName + ".response";
    responseEvent = CreateEventW(nullptr, false, false,
                                 responseEventName.toWideCharPointer());
    if (responseEvent == nullptr)
      setLastWindowsError("CreateEventW response failed");
  }

  void setLastWindowsError(const juce::String &context) {
    lastError = context + ": Windows error " + juce::String((int)GetLastError());
  }
#endif

  bool signal(HANDLE eventHandle, const juce::String &name) {
#if JUCE_WINDOWS
    if (eventHandle == nullptr) {
      lastError = "cannot signal missing " + name + " event";
      return false;
    }

    if (SetEvent(eventHandle) == 0) {
      setLastWindowsError("SetEvent " + name + " failed");
      return false;
    }

    return true;
#else
    juce::ignoreUnused(eventHandle, name);
    return false;
#endif
  }

  WaitResult wait(HANDLE eventHandle, int timeoutMs) {
#if JUCE_WINDOWS
    if (eventHandle == nullptr) {
      lastError = "cannot wait on missing event";
      return WaitResult::failed;
    }

    const DWORD timeout =
        timeoutMs < 0 ? INFINITE : static_cast<DWORD>(timeoutMs);
    const DWORD result = WaitForSingleObject(eventHandle, timeout);
    if (result == WAIT_OBJECT_0)
      return WaitResult::signalled;
    if (result == WAIT_TIMEOUT)
      return WaitResult::timedOut;

    setLastWindowsError("WaitForSingleObject failed");
    return WaitResult::failed;
#else
    juce::ignoreUnused(eventHandle, timeoutMs);
    return WaitResult::failed;
#endif
  }

  juce::String mappingName;
  juce::String lastError;
  SharedBlock *mappedBlock = nullptr;
  HANDLE mappingHandle = nullptr;
  HANDLE requestEvent = nullptr;
  HANDLE responseEvent = nullptr;
};

} // namespace PluginBridge
