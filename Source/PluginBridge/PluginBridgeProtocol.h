#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include <cmath>
#include <cstdint>

namespace PluginBridge {

inline constexpr const char *workerCommandLineUid =
    "modern-midi-player-plugin-worker-v2";
inline constexpr int workerConnectionTimeoutMs = 15000;
inline constexpr int workerCommandTimeoutMs = 30000;
inline constexpr int workerShutdownTimeoutMs = 2000;
inline constexpr int workerRenderHangTimeoutMs = 2000;

inline int getWorkerRenderTimeoutMs(int blockSize, double sampleRate) {
  const double validSampleRate = sampleRate > 0.0 ? sampleRate : 44100.0;
  const double blockPeriodMs =
      1000.0 * static_cast<double>(juce::jmax(1, blockSize)) / validSampleRate;
  return juce::jmax(10, static_cast<int>(std::ceil(blockPeriodMs * 2.0)));
}

enum class Command : int {
  none = 0,
  loadPlugin = 1,
  unloadPlugin = 2,
  prepare = 3,
  openEditor = 5,
  closeEditor = 6,
  shutdown = 7
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
};

inline juce::MemoryBlock valueTreeToBlock(const juce::ValueTree &tree) {
  juce::MemoryOutputStream out;
  tree.writeToStream(out);
  return out.getMemoryBlock();
}

inline juce::ValueTree blockToValueTree(const juce::MemoryBlock &block) {
  juce::MemoryInputStream in(block, false);
  return juce::ValueTree::readFromStream(in);
}

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

inline juce::ValueTree makeCommandTree(Command command) {
  juce::ValueTree tree("PluginBridgeCommand");
  tree.setProperty("command", static_cast<int>(command), nullptr);
  return tree;
}

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

inline juce::MemoryBlock makePrepareCommand(const PrepareRequest &request) {
  auto tree = makeCommandTree(Command::prepare);
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

inline juce::MemoryBlock makeStatusReply(StatusCode code,
                                         const juce::String &message,
                                         Command command = Command::none) {
  juce::ValueTree tree("PluginBridgeStatus");
  tree.setProperty("code", static_cast<int>(code), nullptr);
  tree.setProperty("command", static_cast<int>(command), nullptr);
  tree.setProperty("message", message, nullptr);
  return valueTreeToBlock(tree);
}

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
  return reply;
}

inline bool isPluginWorkerCommandLine(const juce::String &commandLine) {
  return commandLine.contains(workerCommandLineUid);
}

inline int writeMidiBuffer(const juce::MidiBuffer &source,
                           unsigned char *dest, int maxBytes) {
  juce::MemoryOutputStream out(dest, static_cast<size_t>(maxBytes));
  for (const auto metadata : source) {
    const auto message = metadata.getMessage();
    const int messageSize = message.getRawDataSize();
    const auto requiredBytes =
        static_cast<int64_t>(sizeof(int) * 2 + messageSize);

    if (out.getPosition() + requiredBytes > maxBytes)
      return -1;

    out.writeInt(metadata.samplePosition);
    out.writeInt(messageSize);
    out.write(message.getRawData(), static_cast<size_t>(messageSize));
  }

  return static_cast<int>(out.getPosition());
}

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

    const auto message = metadata.getMessage();
    const int messageSize = message.getRawDataSize();
    const auto requiredBytes =
        static_cast<int64_t>(sizeof(int) * 2 + messageSize);
    if (out.getPosition() + requiredBytes > maxBytes)
      return -1;

    out.writeInt(metadata.samplePosition - startSample);
    out.writeInt(messageSize);
    out.write(message.getRawData(), static_cast<size_t>(messageSize));
  }

  return static_cast<int>(out.getPosition());
}

inline void readMidiBuffer(const unsigned char *source, int numBytes,
                           juce::MidiBuffer &dest) {
  juce::MemoryInputStream in(source, static_cast<size_t>(numBytes), false);
  while (in.getNumBytesRemaining() >= static_cast<int64_t>(sizeof(int) * 2)) {
    const int samplePosition = in.readInt();
    const int messageSize = in.readInt();
    if (messageSize <= 0 || in.getNumBytesRemaining() < messageSize)
      return;

    juce::HeapBlock<unsigned char> messageData(messageSize);
    in.read(messageData.getData(), static_cast<size_t>(messageSize));
    dest.addEvent(messageData.getData(), messageSize, samplePosition);
  }
}

} // namespace PluginBridge
