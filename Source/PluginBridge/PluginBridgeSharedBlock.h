#pragma once

// Responsibilities: Windows 命名文件映射、请求/响应事件及固定容量音频块布局。
// Ownership: 每个进程独立持有句柄；映射引用不得超过 `SharedBlockOwner` 的寿命。
// Invariant: 两端使用相同布局；同时最多一项渲染请求，事件通知前写完对应数据和序列号。

#include <juce_core/juce_core.h>
#include "../Midi/MusicalTimeline.h"

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
__declspec(dllimport) BOOL __stdcall ResetEvent(HANDLE hEvent);
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

// Preconditions: 宿主块长度可用 int 安全执行向上取整；非正长度返回 0。
inline int getSharedBlockChunkCount(int totalSamples) {
  return totalSamples > 0
             ? (totalSamples + SharedBlockLayout::maxSamples - 1) /
                   SharedBlockLayout::maxSamples
             : 0;
}

// Postconditions: 返回从 `startSample` 开始的受容量限制长度；区间无效时返回 0。
inline int getSharedBlockChunkSize(int totalSamples, int startSample) {
  if (totalSamples <= 0 || startSample < 0 || startSample >= totalSamples)
    return 0;
  return juce::jmin(SharedBlockLayout::maxSamples,
                    totalSamples - startSample);
}

// Ordering: 主进程写请求序列号/参数/MIDI 后通知；工作进程写音频/结果/响应序列号后通知。
struct SharedBlockHeader {
  double sampleRate = 44100.0;
  std::uint64_t requestSequence = 0;
  std::uint64_t responseSequence = 0;
  int blockSize = 512;
  int midiBytes = 0;
  int resultCode = 0;
  MusicalPosition position;
  int latencySamples = 0;
};

struct SharedBlock {
  SharedBlockHeader header;
  float audio[SharedBlockLayout::maxChannels][SharedBlockLayout::maxSamples]{};
  unsigned char midi[SharedBlockLayout::maxMidiBytes]{};
};

// Contract: 工作进程为插件保留完整输出通道；共享块仅回传前两个通道。
inline int getWorkerProcessChannelCount(int pluginOutputChannels) {
  return juce::jmax(SharedBlockLayout::maxChannels, pluginOutputChannels);
}

// Reason: 只有最多双输出的插件使用显式立体声配置，多输出插件保留原总线布局。
inline bool shouldUseExplicitStereoHostConfig(int pluginOutputChannels) {
  return pluginOutputChannels <= SharedBlockLayout::maxChannels;
}

enum class WaitResult { signalled, timedOut, failed };

class SharedBlockOwner {
public:
  // Preconditions: 两端使用相同的非空会话名，且不与其他插件会话共享名称。
  // Failures: 构造后必须检查 `isOpen`；系统调用失败保存在 `getLastErrorMessage`，析构回收部分句柄。
  explicit SharedBlockOwner(const juce::String &name) : mappingName(name) {
#if JUCE_WINDOWS
    openWindowsHandles();
#else
    lastError = "plugin bridge shared blocks are Windows-only";
#endif
  }

  // Preconditions: 本进程的渲染与等待已结束，不再持有 `block()` 的借用引用。
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

  // Preconditions: `isOpen()` 为真；访问时遵循请求/响应的写入所有权，不提供额外数据锁。
  // Failures: 未映射时触发调试断言；发布构建仍要求调用方满足前置条件。
  SharedBlock &block() {
    jassert(mappedBlock != nullptr);
    return *mappedBlock;
  }

  // Contract: const 访问具有与可修改访问相同的映射寿命和跨进程同步要求。
  const SharedBlock &block() const {
    jassert(mappedBlock != nullptr);
    return *mappedBlock;
  }

  juce::String getName() const { return mappingName; }
  juce::String getLastErrorMessage() const { return lastError; }

  // Ordering: 先写完整共享块，再发送对应事件；失败返回 `false` 并记录系统诊断。
  bool signalRequest() { return signal(requestEvent, "request"); }
  bool signalResponse() { return signal(responseEvent, "response"); }

  // Preconditions: 宿主没有渲染请求，工作线程已停止；仅在重新准备插件时调用。
  // Ordering: 清除停止线程的唤醒事件，防止新线程处理上一会话的共享块。
  bool resetEvents() {
#if JUCE_WINDOWS
    if (!ResetEvent(requestEvent) || !ResetEvent(responseEvent)) {
      setLastWindowsError("ResetEvent failed");
      return false;
    }
    return true;
#else
    lastError = "plugin bridge shared blocks are Windows-only";
    return false;
#endif
  }

  // Contract: `timeoutMs` 为毫秒，负值表示无限等待；调用方必须安排终止唤醒。
  // Failures: 超时和句柄/系统错误通过不同的 `WaitResult` 值返回，不自动重试。
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
