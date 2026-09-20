#pragma once

// Responsibilities: 本机构建间的控制消息、插件描述和 MIDI 字节编解码；不持有进程或共享内存。
// Invariant: 两端必须使用一致的命令编号、字段含义和 `workerCommandLineUid`。
// Trust Boundary: `ValueTree` 解码只恢复字段，不等同于完整请求校验；执行端仍负责业务有效性检查。

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdint>

namespace PluginBridge {

inline constexpr const char *workerCommandLineUid =
    "modern-midi-player-plugin-worker-v3";
inline constexpr int workerConnectionTimeoutMs = 15000;
inline constexpr int workerCommandTimeoutMs = 30000;
inline constexpr int workerShutdownTimeoutMs = 2000;
inline constexpr int workerRenderHangTimeoutMs = 2000;

enum class Command : int {
  none = 0,
  loadPlugin = 1,
  unloadPlugin = 2,
  prepare = 3,
  openEditor = 5,
  closeEditor = 6,
  shutdown = 7,
  beginExport = 8,
  endExport = 9
};

enum class StatusCode : int {
  ok = 0,
  pluginLoadFailed = 1,
  pluginRouteFailed = 2,
  pluginCrashed = 3,
  invalidCommand = 4,
  sharedBlockFailed = 5,
  renderFailed = 6
};

struct PluginLoadRequest {
  juce::String name;
  juce::String format;
  juce::String fileOrIdentifier;
  int uniqueId = 0;
  int deprecatedUid = 0;
  bool isInstrument = false;
  int numInputChannels = 0;
  int numOutputChannels = 0;
  juce::String pluginXml;
  juce::String sharedBlockName;
};

struct PrepareRequest {
  double sampleRate = 44100.0;
  int blockSize = 512;
  bool nonRealtime = false;
};

struct StatusReply {
  StatusCode code = StatusCode::ok;
  Command command = Command::none;
  juce::String message;
  int latencySamples = 0;
};

// Ownership: 编码返回自有字节副本；解码只在调用期间借用输入，失败由无效树表示。
inline juce::MemoryBlock valueTreeToBlock(const juce::ValueTree &tree) {
  juce::MemoryOutputStream out;
  tree.writeToStream(out);
  return out.getMemoryBlock();
}

inline juce::ValueTree blockToValueTree(const juce::MemoryBlock &block) {
  juce::MemoryInputStream in(block, false);
  return juce::ValueTree::readFromStream(in);
}

// Preconditions: 请求字段由插件目录生成；下面的值树/字节转换不验证插件路径或通道能力。
inline juce::ValueTree toValueTree(const PluginLoadRequest &request) {
  juce::ValueTree tree("PluginLoadRequest");
  tree.setProperty("name", request.name, nullptr);
  tree.setProperty("format", request.format, nullptr);
  tree.setProperty("fileOrIdentifier", request.fileOrIdentifier, nullptr);
  tree.setProperty("uniqueId", request.uniqueId, nullptr);
  tree.setProperty("deprecatedUid", request.deprecatedUid, nullptr);
  tree.setProperty("isInstrument", request.isInstrument, nullptr);
  tree.setProperty("numInputChannels", request.numInputChannels, nullptr);
  tree.setProperty("numOutputChannels", request.numOutputChannels, nullptr);
  tree.setProperty("pluginXml", request.pluginXml, nullptr);
  tree.setProperty("sharedBlockName", request.sharedBlockName, nullptr);
  return tree;
}

inline PluginLoadRequest loadRequestFromValueTree(juce::ValueTree tree) {
  if (tree.hasType("PluginBridgeCommand"))
    tree = tree.getChildWithName("PluginLoadRequest");

  PluginLoadRequest request;
  request.name = tree["name"].toString();
  request.format = tree["format"].toString();
  request.fileOrIdentifier = tree["fileOrIdentifier"].toString();
  request.uniqueId = static_cast<int>(tree["uniqueId"]);
  request.deprecatedUid = static_cast<int>(tree["deprecatedUid"]);
  request.isInstrument = static_cast<bool>(tree["isInstrument"]);
  request.numInputChannels = static_cast<int>(tree["numInputChannels"]);
  request.numOutputChannels = static_cast<int>(tree["numOutputChannels"]);
  request.pluginXml = tree["pluginXml"].toString();
  request.sharedBlockName = tree["sharedBlockName"].toString();
  return request;
}

inline juce::MemoryBlock toMemoryBlock(const PluginLoadRequest &request) {
  return valueTreeToBlock(toValueTree(request));
}

inline PluginLoadRequest loadRequestFromMemoryBlock(
    const juce::MemoryBlock &block) {
  return loadRequestFromValueTree(blockToValueTree(block));
}

// Postconditions: 同时携带 JUCE 插件 XML 与显式字段，保留描述中的插件身份信息。
inline PluginLoadRequest makeLoadRequest(const juce::PluginDescription &desc,
                                         const juce::String &sharedBlockName) {
  PluginLoadRequest request;
  request.name = desc.name;
  request.format = desc.pluginFormatName;
  request.fileOrIdentifier = desc.fileOrIdentifier;
  request.uniqueId = desc.uniqueId;
  request.deprecatedUid = desc.deprecatedUid;
  request.isInstrument = desc.isInstrument;
  request.numInputChannels = desc.numInputChannels;
  request.numOutputChannels = desc.numOutputChannels;
  request.sharedBlockName = sharedBlockName;

  if (auto xml = desc.createXml())
    request.pluginXml = xml->toString();

  return request;
}

// Ordering: 可解析的插件 XML 优先；否则使用请求的显式描述字段，实例能力由工作进程检查。
inline juce::PluginDescription toPluginDescription(
    const PluginLoadRequest &request) {
  juce::PluginDescription desc;

  if (request.pluginXml.isNotEmpty()) {
    if (auto xml = juce::XmlDocument::parse(request.pluginXml))
      if (desc.loadFromXml(*xml))
        return desc;
  }

  desc.name = request.name;
  desc.pluginFormatName = request.format;
  desc.fileOrIdentifier = request.fileOrIdentifier;
  desc.uniqueId = request.uniqueId;
  desc.deprecatedUid = request.deprecatedUid;
  desc.isInstrument = request.isInstrument;
  desc.numInputChannels = request.numInputChannels;
  desc.numOutputChannels = request.numOutputChannels;
  return desc;
}

// Contract: 命令封装、准备参数和简单命令函数只构造本协议字段，不发送消息。
inline juce::ValueTree makeCommandTree(Command command) {
  juce::ValueTree tree("PluginBridgeCommand");
  tree.setProperty("command", static_cast<int>(command), nullptr);
  return tree;
}

// Failures: 根类型不匹配返回 `Command::none`；数值命令的支持情况由分发端判断。
inline Command commandFromValueTree(const juce::ValueTree &tree) {
  if (!tree.hasType("PluginBridgeCommand"))
    return Command::none;

  return static_cast<Command>(static_cast<int>(tree["command"]));
}

inline juce::MemoryBlock makeLoadPluginCommand(
    const PluginLoadRequest &request) {
  auto tree = makeCommandTree(Command::loadPlugin);
  tree.addChild(toValueTree(request), -1, nullptr);
  return valueTreeToBlock(tree);
}

inline juce::MemoryBlock makePrepareCommand(const PrepareRequest &request,
                                           Command command = Command::prepare) {
  auto tree = makeCommandTree(command);
  tree.setProperty("sampleRate", request.sampleRate, nullptr);
  tree.setProperty("blockSize", request.blockSize, nullptr);
  tree.setProperty("nonRealtime", request.nonRealtime, nullptr);
  return valueTreeToBlock(tree);
}

inline PrepareRequest prepareRequestFromValueTree(
    const juce::ValueTree &tree) {
  PrepareRequest request;
  request.sampleRate = static_cast<double>(tree["sampleRate"]);
  request.blockSize = static_cast<int>(tree["blockSize"]);
  request.nonRealtime = static_cast<bool>(tree["nonRealtime"]);
  return request;
}

inline juce::MemoryBlock makeSimpleCommand(Command command) {
  return valueTreeToBlock(makeCommandTree(command));
}

// Invariant: 回复携带对应 `Command`；控制端依赖命令串行化匹配回复，协议没有独立请求 ID。
inline juce::MemoryBlock makeStatusReply(StatusCode code,
                                         const juce::String &message,
                                         Command command = Command::none,
                                         int latencySamples = 0) {
  juce::ValueTree tree("PluginBridgeStatus");
  tree.setProperty("code", static_cast<int>(code), nullptr);
  tree.setProperty("command", static_cast<int>(command), nullptr);
  tree.setProperty("message", message, nullptr);
  tree.setProperty("latencySamples", latencySamples, nullptr);
  return valueTreeToBlock(tree);
}

// Failures: 回复根类型错误产生 `invalidCommand`；字段枚举仍按协议约定解释。
inline StatusReply statusReplyFromMemoryBlock(const juce::MemoryBlock &block) {
  const auto tree = blockToValueTree(block);
  StatusReply reply;

  if (!tree.hasType("PluginBridgeStatus")) {
    reply.code = StatusCode::invalidCommand;
    reply.message = "invalid bridge status";
    return reply;
  }

  reply.code = static_cast<StatusCode>(static_cast<int>(tree["code"]));
  reply.command = static_cast<Command>(static_cast<int>(tree["command"]));
  reply.message = tree["message"].toString();
  reply.latencySamples = static_cast<int>(tree["latencySamples"]);
  return reply;
}

// Postconditions: 仅识别命令行是否包含 worker UID；连接参数由 JUCE 初始化入口解析。
inline bool isPluginWorkerCommandLine(const juce::String &commandLine) {
  return commandLine.contains(workerCommandLineUid);
}

// Preconditions: `dest` 至少有 `maxBytes` 可写字节，容量非负；事件时间戳为采样偏移。
// Postconditions: 返回已编码字节数；容量不足返回 -1，此时缓冲区可能含前缀，调用方必须丢弃整包。
inline int writeMidiBuffer(const juce::MidiBuffer &source,
                           unsigned char *dest, int maxBytes) {
  juce::MemoryOutputStream out(dest, static_cast<size_t>(maxBytes));
  for (const auto metadata : source) {
    const int messageSize = metadata.numBytes;
    const auto requiredBytes =
        static_cast<int64_t>(sizeof(int) * 2 + messageSize);

    if (out.getPosition() + requiredBytes > maxBytes)
      return -1;

    out.writeInt(metadata.samplePosition);
    out.writeInt(messageSize);
    out.write(metadata.data, static_cast<size_t>(messageSize));
  }

  return static_cast<int>(out.getPosition());
}

// Preconditions: 遵循完整编码的缓冲区契约，区间加法不得溢出。
// Postconditions: 只编码 [startSample, startSample + numSamples) 内事件，并将偏移归零到分块起点。
inline int writeMidiBufferRange(const juce::MidiBuffer &source,
                                unsigned char *dest, int maxBytes,
                                int startSample, int numSamples) {
  juce::MemoryOutputStream out(dest, static_cast<size_t>(maxBytes));
  const int endSample = startSample + juce::jmax(0, numSamples);

  for (const auto metadata : source) {
    if (metadata.samplePosition < startSample)
      continue;
    if (metadata.samplePosition >= endSample)
      break;

    const int messageSize = metadata.numBytes;
    const auto requiredBytes =
        static_cast<int64_t>(sizeof(int) * 2 + messageSize);
    if (out.getPosition() + requiredBytes > maxBytes)
      return -1;

    out.writeInt(metadata.samplePosition - startSample);
    out.writeInt(messageSize);
    out.write(metadata.data, static_cast<size_t>(messageSize));
  }

  return static_cast<int>(out.getPosition());
}

// Preconditions: 接收端已验证 `numBytes` 在源缓冲区容量内，`numSamples` 为有效块长度。
// Postconditions: 完整解码并验证块内偏移后替换 `dest`。
// Failures: 截断、无效消息长度或越界采样偏移返回 `false`，保留原 `dest`，调用方拒绝整块。
[[nodiscard]] inline bool readMidiBuffer(const unsigned char *source, int numBytes,
                                       juce::MidiBuffer &dest, int numSamples) {
  juce::MemoryInputStream in(source, static_cast<size_t>(numBytes), false);
  while (in.getNumBytesRemaining() > 0) {
    if (in.getNumBytesRemaining() < static_cast<int64_t>(sizeof(int) * 2))
      return false;
    const int samplePosition = in.readInt();
    const int messageSize = in.readInt();
    if (samplePosition < 0 || samplePosition >= numSamples ||
        messageSize <= 0 || in.getNumBytesRemaining() < messageSize)
      return false;

    const auto *messageData = source + in.getPosition();
    in.skipNextBytes(messageSize);
    // Reason: JUCE 对固定长度消息按状态字节读取，声明长度不足时必须在调用前拒绝。
    const auto status = messageData[0];
    if (status < 0x80 ||
        ((status != 0xf0 && status != 0xf7) &&
         messageSize != juce::MidiMessage::getMessageLengthFromFirstByte(status)))
      return false;
  }
  // Ordering: 先验证整包，再复用预分配存储；渲染时不创建逐消息堆块。
  dest.clear();
  in.setPosition(0);
  while (in.getNumBytesRemaining() > 0) {
    const int samplePosition = in.readInt();
    const int messageSize = in.readInt();
    if (!dest.addEvent(source + in.getPosition(), messageSize, samplePosition))
      return false;
    in.skipNextBytes(messageSize);
  }
  return true;
}

} // namespace PluginBridge
