#include "AudioEngine/ExportAudioProcessing.h"
#include "AudioEngine/ExportFileSupport.h"
#include "AudioEngine/ExportFormatSupport.h"
#include "AudioEngine/PluginListSupport.h"
#include "Midi/MidiPlayer.h"
#include "Playlist/PlaylistManager.h"
#include "PluginBridge/PluginBridgeClient.h"
#include "PluginBridge/PluginBridgeSharedBlock.h"
#include "PluginBridge/PluginWorkerProcess.h"
#include "UI/AudioSettingsSupport.h"
#include "UI/AudioStatusAnimation.h"
#include "UI/BackgroundEffectSupport.h"
#include "UI/BackgroundHistorySupport.h"
#include "UI/BackgroundLoadSupport.h"
#include "UI/BackgroundThumbnailSupport.h"
#include "UI/BackgroundScrollMotion.h"
#include "UI/ButtonAnimationSupport.h"
#include "UI/ComboBoxAnimationSupport.h"
#include "UI/ExportHintState.h"
#include "UI/MarqueeLabelSupport.h"
#include "UI/ExportPresetSupport.h"
#include "UI/FluentDialogSupport.h"
#include "UI/FluentSettingsStyle.h"
#include "UI/LegacyTransportWidgets.h"
#include "UI/CustomControls.h"
#include "UI/LegacyIconAssets.h"
#include "UI/PlaylistAnimationSupport.h"
#include "UI/PluginWindowLifecycle.h"
#include "UI/SidebarAnimationSupport.h"
#include "UI/TooltipPlacement.h"
#include "Utils/CommandLineMidiFileSupport.h"
#include "Utils/PaletteSelectionSupport.h"
#include "Utils/UserSettings.h"
#include "Utils/WindowsFileAssociation.h"

#include "TestHarness.h"

#include <array>
#include <atomic>
#include <cmath>
#include <cstring>
#include <iostream>
#include <thread>

namespace {
int failures = 0;

#if JUCE_WINDOWS
extern "C" __declspec(dllimport) int __stdcall
SetEnvironmentVariableW(const wchar_t *name, const wchar_t *value);
#endif

void expect(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAIL: " << message << '\n';
    ++failures;
  }
}

juce::Rectangle<int> getVisibleAlphaBounds(const juce::Image &image) {
  int minX = image.getWidth();
  int minY = image.getHeight();
  int maxX = -1;
  int maxY = -1;

  for (int y = 0; y < image.getHeight(); ++y) {
    for (int x = 0; x < image.getWidth(); ++x) {
      if (image.getPixelAt(x, y).getAlpha() == 0)
        continue;
      minX = juce::jmin(minX, x);
      minY = juce::jmin(minY, y);
      maxX = juce::jmax(maxX, x);
      maxY = juce::jmax(maxY, y);
    }
  }

  if (maxX < minX || maxY < minY)
    return {};
  return {minX, minY, maxX - minX + 1, maxY - minY + 1};
}

juce::Rectangle<int>
renderSvgIconBounds(FluentLookAndFeel &lookAndFeel, const char *svg,
                    float visualSize) {
  juce::Image image(juce::Image::ARGB, 128, 128, true);
  auto xml = juce::XmlDocument::parse(svg);
  auto drawable = xml != nullptr ? juce::Drawable::createFromSVG(*xml)
                                 : nullptr;
  if (drawable == nullptr)
    return {};

  drawable->replaceColour(juce::Colours::black, juce::Colours::white);
  {
    juce::Graphics graphics(image);
    lookAndFeel.drawDrawableIcon(graphics, *drawable,
                                 image.getBounds().toFloat(), visualSize);
  }
  return getVisibleAlphaBounds(image);
}

juce::Rectangle<int> renderGlyphIconBounds(FluentLookAndFeel &lookAndFeel,
                                           const juce::String &glyph,
                                           float visualSize) {
  juce::Image image(juce::Image::ARGB, 128, 128, true);
  {
    juce::Graphics graphics(image);
    graphics.setColour(juce::Colours::white);
    lookAndFeel.drawIconGlyph(graphics, glyph, image.getBounds().toFloat(),
                              visualSize);
  }
  return getVisibleAlphaBounds(image);
}

juce::Rectangle<int>
renderSystemGlyphBounds(FluentLookAndFeel &lookAndFeel,
                        const juce::String &glyph, float fontSize,
                        float buttonSize) {
  juce::Image image(juce::Image::ARGB, 128, 128, true);
  {
    juce::Graphics graphics(image);
    graphics.setColour(juce::Colours::white);
    lookAndFeel.drawSystemIconGlyph(
        graphics, glyph,
        image.getBounds().toFloat().withSizeKeepingCentre(buttonSize,
                                                          buttonSize),
        fontSize);
  }
  return getVisibleAlphaBounds(image);
}

void testProductionIconVisualBounds() {
  FluentLookAndFeel lookAndFeel(true);
  const std::array<const wchar_t *, 24> productionGlyphs = {
      L"\uE73E", L"\uE71A", L"\uE74D", L"\uE768", L"\uE769",
      L"\uE892", L"\uE893", L"\uE896", L"\uE8A7", L"\uE8B1",
      L"\uE8ED", L"\uE8EE", L"\uE992", L"\uE994", L"\uE995",
      L"\uE9A1", L"\uEA42", L"\uE713", L"\uE718", L"\uE840",
      L"\uE8D2", L"\uE8D6", L"\uE90B", L"\uE91B"};

  for (const float visualSize : {16.0f, 24.0f, 36.0f, 48.0f}) {
    for (const auto *glyph : productionGlyphs) {
      const auto bounds =
          renderGlyphIconBounds(lookAndFeel, glyph, visualSize);
      expect(!bounds.isEmpty() && bounds.getWidth() <= visualSize + 1.0f &&
                 bounds.getHeight() <= visualSize + 1.0f,
             "Fluent glyphs should retain their official font metrics");
    }
  }

  constexpr float productionIconSize = LegacyDesignTokens::Icon::transport;
  const auto reference =
      renderGlyphIconBounds(lookAndFeel, L"\uE995", productionIconSize);
  const auto sequential = renderSvgIconBounds(
      lookAndFeel, LegacyIconAssets::sequentialPlaybackSvg,
      productionIconSize * LegacyIconAssets::sequentialPlaybackOpticalScale);
  std::cout << "Production icon bounds: sequential=" << sequential.toString()
            << ", reference=" << reference.toString() << '\n';
  expect(!sequential.isEmpty() && !reference.isEmpty(),
         "transport icons must render a visible alpha boundary");
  expect(std::abs(sequential.getWidth() - reference.getWidth()) <= 1 &&
             std::abs(sequential.getHeight() - reference.getHeight()) <= 1,
         "custom playback SVG should match the reference Fluent icon size");
  expect(std::abs(sequential.getCentreX() - reference.getCentreX()) <= 1 &&
             std::abs(sequential.getCentreY() - reference.getCentreY()) <= 1,
         "custom playback SVG should match the reference Fluent icon centre");

  const auto paneToggle = renderSystemGlyphBounds(
      lookAndFeel, L"\uE700", LegacyDesignTokens::Icon::paneToggle,
      (float)LegacyDesignTokens::Layout::navigationPaneToggleButtonSize);
  expect(!paneToggle.isEmpty(),
         "NavigationView pane toggle glyph must render visibly");
  expect(paneToggle.getWidth() <= LegacyDesignTokens::Icon::paneToggle + 1.0f &&
             paneToggle.getHeight() <=
                 LegacyDesignTokens::Icon::paneToggle + 1.0f,
         "NavigationView pane toggle must preserve its official 20px metrics");
  expect(std::abs(paneToggle.getCentreX() - 64) <= 1 &&
             std::abs(paneToggle.getCentreY() - 64) <= 1,
         "NavigationView pane toggle must remain centred in its button");

  for (const auto *captionGlyph : {L"\uE8BB", L"\uE921", L"\uE922"}) {
    const auto caption =
        renderSystemGlyphBounds(lookAndFeel, captionGlyph, 12.0f, 40.0f);
    expect(!caption.isEmpty() && caption.getWidth() <= 13 &&
               caption.getHeight() <= 13,
           "window caption glyphs should preserve Fluent 12px metrics");
    expect(std::abs(caption.getCentreX() - 64) <= 1 &&
               std::abs(caption.getCentreY() - 64) <= 1,
           "window caption glyphs should remain centred in their buttons");
  }
}

void setTestEnvironmentVariable(const wchar_t *name, const wchar_t *value) {
#if JUCE_WINDOWS
  SetEnvironmentVariableW(name, value);
#else
  juce::ignoreUnused(name, value);
#endif
}

void clearTestEnvironmentVariable(const wchar_t *name) {
#if JUCE_WINDOWS
  SetEnvironmentVariableW(name, nullptr);
#else
  juce::ignoreUnused(name);
#endif
}

bool waitForBridgeStatus(PluginBridge::PluginBridgeClient &bridge,
                         PluginBridge::BridgeStatus status, int timeoutMs) {
  const auto start = juce::Time::getMillisecondCounter();
  const auto timeout = static_cast<uint32_t>(juce::jmax(1, timeoutMs));

  do {
    if (bridge.getStatus() == status)
      return true;

    juce::Thread::sleep(10);
  } while (juce::Time::getMillisecondCounter() - start < timeout);

  return bridge.getStatus() == status;
}

bool canCreateAndWrite(const juce::String &formatName,
                       const juce::AudioFormatWriterOptions &options,
                       const char *expectedSignature) {
  juce::AudioFormatManager manager;
  manager.registerBasicFormats();
  auto *format = findExportAudioFormat(manager, formatName);
  if (format == nullptr)
    return false;

  juce::MemoryBlock encodedData;
  std::unique_ptr<juce::OutputStream> stream =
      std::make_unique<juce::MemoryOutputStream>(encodedData, false);
  auto writer = format->createWriterFor(stream, options);
  if (writer == nullptr)
    return false;

  juce::AudioBuffer<float> buffer(2, 512);
  buffer.clear();
  if (!writer->writeFromAudioSampleBuffer(buffer, 0, buffer.getNumSamples()))
    return false;

  writer.reset();
  const auto signatureLength = std::strlen(expectedSignature);
  return encodedData.getSize() >= signatureLength &&
         std::memcmp(encodedData.getData(), expectedSignature,
                     signatureLength) == 0;
}

void addEvent(juce::MidiMessageSequence &sequence, juce::MidiMessage message,
              double timeSeconds) {
  message.setTimeStamp(timeSeconds);
  sequence.addEvent(message);
}

bool containsNoteOff(const juce::MidiBuffer &buffer, int channel, int note) {
  for (const auto metadata : buffer) {
    const auto message = metadata.getMessage();
    if (message.isNoteOff() && message.getChannel() == channel &&
        message.getNoteNumber() == note)
      return true;
  }
  return false;
}

int findNoteEventIndex(const juce::MidiBuffer &buffer, bool noteOn,
                       int channel, int note) {
  int index = 0;
  for (const auto metadata : buffer) {
    const auto message = metadata.getMessage();
    const bool matchesType = noteOn ? message.isNoteOn() : message.isNoteOff();
    if (matchesType && message.getChannel() == channel &&
        message.getNoteNumber() == note)
      return index;
    ++index;
  }
  return -1;
}

bool containsController(const juce::MidiBuffer &buffer, int channel,
                        int controller, int value) {
  for (const auto metadata : buffer) {
    const auto message = metadata.getMessage();
    if (message.isController() && message.getChannel() == channel &&
        message.getControllerNumber() == controller &&
        message.getControllerValue() == value)
      return true;
  }
  return false;
}

int findControllerIndex(const juce::MidiBuffer &buffer, int channel,
                        int controller, int value) {
  int index = 0;
  for (const auto metadata : buffer) {
    const auto message = metadata.getMessage();
    if (message.isController() && message.getChannel() == channel &&
        message.getControllerNumber() == controller &&
        message.getControllerValue() == value)
      return index;
    ++index;
  }
  return -1;
}

int findControllerSamplePosition(const juce::MidiBuffer &buffer, int channel,
                                 int controller, int value) {
  for (const auto metadata : buffer) {
    const auto message = metadata.getMessage();
    if (message.isController() && message.getChannel() == channel &&
        message.getControllerNumber() == controller &&
        message.getControllerValue() == value)
      return metadata.samplePosition;
  }
  return -1;
}

int findNoteSamplePosition(const juce::MidiBuffer &buffer, bool noteOn,
                           int channel, int note) {
  for (const auto metadata : buffer) {
    const auto message = metadata.getMessage();
    const bool matchesType = noteOn ? message.isNoteOn() : message.isNoteOff();
    if (matchesType && message.getChannel() == channel &&
        message.getNoteNumber() == note)
      return metadata.samplePosition;
  }
  return -1;
}

class TestPluginBridgeWorker final : private juce::ChildProcessWorker {
public:
  TestPluginBridgeWorker() : renderThread(*this) {}

  ~TestPluginBridgeWorker() override { stopRenderThread(); }

  bool run(const juce::String &commandLine) {
    if (!initialiseFromCommandLine(commandLine,
                                   PluginBridge::workerCommandLineUid,
                                   PluginBridge::workerConnectionTimeoutMs))
      return false;

    stopped.wait(PluginBridge::workerConnectionTimeoutMs);
    return true;
  }

private:
  class RenderThread final : public juce::Thread {
  public:
    explicit RenderThread(TestPluginBridgeWorker &owner)
        : juce::Thread("Test plugin bridge renderer"), owner(owner) {}

    void run() override { owner.runRenderLoop(); }

  private:
    TestPluginBridgeWorker &owner;
  };

  void handleConnectionMade() override {
    sendMessageToCoordinator(PluginBridge::makeStatusReply(
        PluginBridge::StatusCode::ok, "test worker ready",
        PluginBridge::Command::none));
    if (juce::SystemStats::getEnvironmentVariable(
            "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_AFTER_READY", "") == "1")
      stopped.signal();
  }

  void handleConnectionLost() override { stopped.signal(); }

  void handleMessageFromCoordinator(const juce::MemoryBlock &message) override {
    const auto tree = PluginBridge::blockToValueTree(message);
    const auto command = PluginBridge::commandFromValueTree(tree);
    if (command == PluginBridge::Command::loadPlugin &&
        juce::SystemStats::getEnvironmentVariable(
            "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_IGNORE_LOAD", "") == "1")
      return;
    if (command == PluginBridge::Command::loadPlugin &&
        juce::SystemStats::getEnvironmentVariable(
            "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_FAIL", "") == "1") {
      sendMessageToCoordinator(PluginBridge::makeStatusReply(
          PluginBridge::StatusCode::pluginLoadFailed, "test load failed",
          PluginBridge::Command::loadPlugin));
      return;
    }

    if (command == PluginBridge::Command::loadPlugin &&
        juce::SystemStats::getEnvironmentVariable(
            "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_ON_LOAD", "") == "1") {
      stopped.signal();
      return;
    }

    if (command == PluginBridge::Command::loadPlugin &&
        juce::SystemStats::getEnvironmentVariable(
            "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_OK", "") == "1") {
      const auto request = PluginBridge::loadRequestFromValueTree(tree);
      const int renderDelayMs =
          juce::SystemStats::getEnvironmentVariable(
              "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_RENDER_DELAY_MS", "0")
              .getIntValue();
      if (renderDelayMs > 0) {
        sharedBlock =
            std::make_unique<PluginBridge::SharedBlockOwner>(request.sharedBlockName);
        firstRenderDelayMs = renderDelayMs;
        firstRenderPending = true;
        renderThread.startThread(juce::Thread::Priority::highest);
      }
      sendMessageToCoordinator(PluginBridge::makeStatusReply(
          PluginBridge::StatusCode::ok, "test plugin loaded",
          PluginBridge::Command::loadPlugin));
      return;
    }

    if (command == PluginBridge::Command::prepare && sharedBlock != nullptr) {
      sendMessageToCoordinator(PluginBridge::makeStatusReply(
          PluginBridge::StatusCode::ok, "test plugin prepared",
          PluginBridge::Command::prepare));
      return;
    }

    if (command == PluginBridge::Command::unloadPlugin) {
      stopRenderThread();
      const int delayMs = juce::SystemStats::getEnvironmentVariable(
                              "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_UNLOAD_DELAY_MS",
                              "0")
                              .getIntValue();
      if (delayMs > 0)
        juce::Thread::sleep(delayMs);
      sendMessageToCoordinator(PluginBridge::makeStatusReply(
          PluginBridge::StatusCode::ok, "test plugin unloaded",
          PluginBridge::Command::unloadPlugin));
      return;
    }

    if (command == PluginBridge::Command::shutdown) {
      if (juce::SystemStats::getEnvironmentVariable(
              "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_IGNORE_SHUTDOWN", "") == "1")
        return;
      sendMessageToCoordinator(PluginBridge::makeStatusReply(
          PluginBridge::StatusCode::ok, "test worker shutdown",
          PluginBridge::Command::shutdown));
      stopped.signal();
    }
  }

  void runRenderLoop() {
    while (!renderThread.threadShouldExit() && sharedBlock != nullptr) {
      if (sharedBlock->waitForRequest(-1) != PluginBridge::WaitResult::signalled)
        return;
      if (renderThread.threadShouldExit())
        return;

      auto &block = sharedBlock->block();
      if (firstRenderPending) {
        firstRenderPending = false;
        juce::Thread::sleep(firstRenderDelayMs);
      }

      for (auto &channel : block.audio)
        juce::FloatVectorOperations::clear(channel, block.header.blockSize);
      block.header.resultCode =
          static_cast<int>(PluginBridge::StatusCode::ok);
      block.header.responseSequence = block.header.requestSequence;
      sharedBlock->signalResponse();
    }
  }

  void stopRenderThread() {
    if (!renderThread.isThreadRunning()) {
      sharedBlock = nullptr;
      return;
    }
    renderThread.signalThreadShouldExit();
    if (sharedBlock != nullptr)
      sharedBlock->signalRequest();
    renderThread.stopThread(PluginBridge::workerShutdownTimeoutMs);
    sharedBlock = nullptr;
  }

  juce::WaitableEvent stopped;
  std::unique_ptr<PluginBridge::SharedBlockOwner> sharedBlock;
  RenderThread renderThread;
  int firstRenderDelayMs = 0;
  bool firstRenderPending = false;
};

juce::String makeCommandLine(int argc, char *argv[]) {
  juce::String commandLine;
  for (int i = 1; i < argc; ++i) {
    if (i > 1)
      commandLine += " ";

    const juce::String arg(argv[i]);
    commandLine += arg.containsChar(' ') ? arg.quoted('"') : arg;
  }
  return commandLine;
}

bool runBridgeWorkerChildIfRequested(int argc, char *argv[]) {
  const auto commandLine = makeCommandLine(argc, argv);
  if (!PluginBridge::isPluginWorkerCommandLine(commandLine))
    return false;

  if (juce::SystemStats::getEnvironmentVariable(
          "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_AFTER_READY", "") == "1") {
    TestPluginBridgeWorker worker;
    worker.run(commandLine);
    return true;
  }

  if (juce::SystemStats::getEnvironmentVariable(
          "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_FAIL", "") == "1") {
    TestPluginBridgeWorker worker;
    worker.run(commandLine);
    return true;
  }

  if (juce::SystemStats::getEnvironmentVariable(
          "MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_OK", "") == "1") {
    TestPluginBridgeWorker worker;
    worker.run(commandLine);
    return true;
  }

  if (juce::SystemStats::getEnvironmentVariable(
          "MIDI_PLAYER_PLUGIN_BRIDGE_SMOKE_XML", "")
          .isNotEmpty()) {
    if (PluginBridge::runWorkerIfRequested(commandLine))
      juce::MessageManager::getInstance()->runDispatchLoop();
    PluginBridge::workerInstance() = nullptr;
    return true;
  }

  TestPluginBridgeWorker worker;
  worker.run(commandLine);
  return true;
}

std::unique_ptr<juce::PluginDescription> findPluginDescription(
    const juce::File &xmlFile, const juce::String &pluginName) {
  auto xml = juce::XmlDocument::parse(xmlFile);
  if (xml == nullptr || !xml->hasTagName("KNOWNPLUGINS"))
    return nullptr;

  for (auto *child : xml->getChildIterator()) {
    juce::PluginDescription desc;
    if (desc.loadFromXml(*child) && desc.name == pluginName)
      return std::make_unique<juce::PluginDescription>(desc);
  }

  return nullptr;
}

void runRealPluginBridgeSmokeIfRequested() {
  const auto xmlPath = juce::SystemStats::getEnvironmentVariable(
      "MIDI_PLAYER_PLUGIN_BRIDGE_SMOKE_XML", "");
  const auto pluginName = juce::SystemStats::getEnvironmentVariable(
      "MIDI_PLAYER_PLUGIN_BRIDGE_SMOKE_NAME", "");
  const auto stopAfter = juce::SystemStats::getEnvironmentVariable(
      "MIDI_PLAYER_PLUGIN_BRIDGE_SMOKE_STOP_AFTER", "");
  const bool openEditorBeforeRender =
      juce::SystemStats::getEnvironmentVariable(
          "MIDI_PLAYER_PLUGIN_BRIDGE_OPEN_EDITOR_BEFORE_RENDER", "") == "1";
  const int waitMs = juce::SystemStats::getEnvironmentVariable(
                         "MIDI_PLAYER_PLUGIN_BRIDGE_SMOKE_WAIT_MS", "0")
                         .getIntValue();
  if (xmlPath.isEmpty() || pluginName.isEmpty())
    return;

  auto maybeStopAfter = [&](const char *stage,
                            PluginBridge::PluginBridgeClient &bridge) {
    std::cout << "SMOKE_STAGE " << stage << '\n';
    if (waitMs > 0)
      juce::Thread::sleep(waitMs);
    if (stopAfter == stage) {
      bridge.stop();
      return true;
    }
    return false;
  };

  auto desc = findPluginDescription(juce::File(xmlPath), pluginName);
  expect(desc != nullptr,
         "plugin bridge smoke should find the requested plugin description");
  if (desc == nullptr)
    return;

  PluginBridge::PluginBridgeClient bridge;
  expect(bridge.loadPlugin(*desc),
         "plugin bridge smoke should load the requested plugin in a worker");
  if (!bridge.isPluginLoaded()) {
    std::cerr << "Plugin bridge smoke load error: "
              << bridge.getLastError() << '\n';
    return;
  }
  if (maybeStopAfter("load", bridge))
    return;

  const bool prepared = bridge.prepare(44100.0, 512);
  expect(prepared, "plugin bridge smoke should prepare the worker plugin");
  if (!prepared) {
    std::cerr << "Plugin bridge smoke prepare error: "
              << bridge.getLastError() << '\n';
    bridge.stop();
    return;
  }
  if (maybeStopAfter("prepare", bridge))
    return;

  if (openEditorBeforeRender) {
    const bool editorOpened = bridge.openEditor();
    expect(editorOpened,
           "plugin bridge smoke should open the plugin editor in the worker");
    if (!editorOpened)
      std::cerr << "Plugin bridge smoke editor error: "
                << bridge.getLastError() << '\n';
    bridge.closeEditor();
    if (maybeStopAfter("editor", bridge))
      return;
  }

  juce::AudioBuffer<float> buffer(2, 512);
  buffer.clear();
  juce::MidiBuffer midi;
  midi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 0);
  midi.addEvent(juce::MidiMessage::noteOff(1, 60), 240);
  const bool rendered = bridge.processBlock(midi, buffer, 44100.0);
  expect(rendered,
         "plugin bridge smoke should render one MIDI block in the worker");
  if (!rendered)
    std::cerr << "Plugin bridge smoke render error: "
              << bridge.getLastError() << '\n';
  if (maybeStopAfter("render", bridge))
    return;

  if (!openEditorBeforeRender &&
      juce::SystemStats::getEnvironmentVariable(
          "MIDI_PLAYER_PLUGIN_BRIDGE_OPEN_EDITOR", "") == "1") {
    const bool editorOpened = bridge.openEditor();
    expect(editorOpened,
           "plugin bridge smoke should open the plugin editor in the worker");
    if (!editorOpened)
      std::cerr << "Plugin bridge smoke editor error: "
                << bridge.getLastError() << '\n';
    bridge.closeEditor();
    if (maybeStopAfter("editor", bridge))
      return;
  }

  bridge.unloadPlugin();
  if (maybeStopAfter("unload", bridge))
    return;
  bridge.stop();
  std::cout << "SMOKE_STAGE stop\n";
}
} // namespace

int main(int argc, char *argv[]) {
  juce::ScopedJuceInitialiser_GUI juceInitialiser;

  if (runBridgeWorkerChildIfRequested(argc, argv))
    return 0;

  testProductionIconVisualBounds();

  const auto customDialogPolicy = getFluentDialogWindowPolicy(false);
  expect(!customDialogPolicy.useSystemDropShadow,
         "transparent dialogs must avoid JUCE's external rectangular shadow "
         "windows");
  expect(customDialogPolicy.configureBeforeEnteringModal,
         "dialog peer styles must be configured before modal ownership starts");
  expect(!customDialogPolicy.useDwmRoundedCorners,
         "material dialogs should avoid competing DWM corner rendering");
  expect(customDialogPolicy.useWindowRegion,
         "non-draggable dialogs should clip the native window to one rounded "
         "region");
  expect(customDialogPolicy.centreOnOwner &&
             customDialogPolicy.useNativeOwner &&
             !customDialogPolicy.allowAlwaysOnTop &&
             !customDialogPolicy.allowDragging,
         "settings dialogs should remain centred, modal, and non-draggable");
  using WindowControlKind = juce::Component::WindowControlKind;
  expect(getNonDraggableDialogControlKind(WindowControlKind::caption) ==
                 WindowControlKind::client &&
             getNonDraggableDialogControlKind(WindowControlKind::sizeTop) ==
                 WindowControlKind::client &&
             getNonDraggableDialogControlKind(WindowControlKind::close) ==
                 WindowControlKind::close,
         "modal dialog hit testing should block native caption movement");
  expect(getDialogEnterAlpha(0.0f) == dialogTransitionAlphaFloor &&
             getDialogEnterAlpha(1.0f) == 1.0f &&
             getDialogEnterAlpha(0.5f) > 0.5f,
         "dialog enter animation should ease quickly toward full opacity");
  expect(getDialogExitAlpha(0.0f) == 1.0f &&
             getDialogExitAlpha(1.0f) == dialogTransitionAlphaFloor &&
             getDialogExitAlpha(0.5f) > 0.5f,
         "dialog exit animation should retain surface coverage before hiding");
  expect(getDialogExitAlpha(0.6f, 0.0f) == 0.6f &&
             getDialogExitAlpha(0.6f, 1.0f) ==
                 dialogTransitionAlphaFloor,
         "dialog exit animation should support closing during fade-in");
  const auto nativeDialogPolicy = getFluentDialogWindowPolicy(true);
  expect(!nativeDialogPolicy.useSystemDropShadow,
         "all material dialogs should use one clipped native window");
  expect(!nativeDialogPolicy.useDwmRoundedCorners,
         "all custom dialogs should use the same corner implementation");
  expect(nativeDialogPolicy.useWindowRegion,
         "all custom dialogs should clip opaque corner pixels");

  FluentLookAndFeel interactionLookAndFeel(true);
  juce::ToggleButton interactionToggle(L"更换原生图标样式");
  interactionToggle.setBounds(0, 0, 420, 40);
  const auto toggleLayout =
      interactionLookAndFeel.getToggleButtonLayout(interactionToggle);
  expect(interactionLookAndFeel.getToggleButtonPreferredWidth(
             interactionToggle) > 100,
         "toggle preferred width should include its visible label");
  expect(toggleLayout.textBounds.getWidth() > 0,
         "toggle layout should reserve visible label width");
  expect(toggleLayout.interactionBounds.contains(
             juce::Point<float>(12.0f, 20.0f)) &&
             !toggleLayout.interactionBounds.contains(
                 juce::Point<float>(320.0f, 20.0f)),
         "toggle hover feedback should stop after its visible label");

  const auto normalisedMaterial = WindowMaterial::normalise(
      {WindowMaterial::Type::Acrylic, 0.1f, 80});
  expect(normalisedMaterial.opacity == 0.25f &&
             normalisedMaterial.strength == 50,
         "dialog material settings should remain inside supported ranges");
  expect(!WindowMaterial::supportsStrength(
             WindowMaterial::Type::Transparent) &&
             WindowMaterial::supportsStrength(
                 WindowMaterial::Type::GaussianBlur),
         "pure transparency should disable the effect-strength control");
  const float lowAcrylicOpacity = FluentSettingsStyle::dialogSurfaceAlpha(
      {WindowMaterial::Type::Acrylic, 0.25f, 24});
  const float highAcrylicOpacity = FluentSettingsStyle::dialogSurfaceAlpha(
      {WindowMaterial::Type::Acrylic, 0.98f, 24});
  expect(highAcrylicOpacity - lowAcrylicOpacity > 0.35f,
         "dialog opacity changes should produce a clearly visible range");
  expect(getVolumeIconGlyph(0.0f) == L"\uE992" &&
             getVolumeIconGlyph(0.2f) == L"\uE993" &&
             getVolumeIconGlyph(0.5f) == L"\uE994" &&
             getVolumeIconGlyph(0.9f) == L"\uE995",
         "volume icons should cover mute, low, medium, and high stages");

  const juce::Rectangle<int> tooltipParent(0, 0, 200, 120);
  const juce::Rectangle<int> topAnchor(80, 2, 40, 20);
  const auto topPlacement =
      TooltipPlacement::place({80, 40}, topAnchor, tooltipParent);
  expect(topPlacement.getY() >= topAnchor.getBottom() &&
             !topPlacement.intersects(topAnchor),
         "tooltips near the top edge should automatically move below");

  juce::Array<juce::Rectangle<int>> tooltipAvoidAreas;
  tooltipAvoidAreas.add({60, 0, 100, 46});
  const juce::Rectangle<int> centreAnchor(80, 54, 40, 20);
  const auto avoidingPlacement = TooltipPlacement::place(
      {80, 40}, centreAnchor, tooltipParent, tooltipAvoidAreas);
  expect(!avoidingPlacement.intersects(centreAnchor) &&
             !avoidingPlacement.intersects(tooltipAvoidAreas[0]),
         "tooltips should avoid the anchor and registered overlay regions");

  ComboBoxAnimationState comboMotion;
  comboMotion.setTargets(true, true);
  comboMotion.tick();
  expect(comboMotion.getOpenProgress() > 0.0f &&
             comboMotion.getHoverProgress() > 0.0f,
         "an opening ComboBox should animate its arrow and focus state");
  while (comboMotion.tick())
    ;
  expect(comboMotion.getOpenProgress() == 1.0f &&
             comboMotion.getHoverProgress() == 1.0f,
         "ComboBox opening animation should settle exactly");
  comboMotion.setTargets(false, false);
  while (comboMotion.tick())
    ;
  expect(comboMotion.getOpenProgress() == 0.0f &&
             comboMotion.getHoverProgress() == 0.0f,
         "ComboBox closing animation should settle exactly");

  float navigationIconScale = 1.0f;
  while (advanceNavigationIconScale(navigationIconScale, true, false))
    ;
  expect(navigationIconScale ==
             LegacyDesignTokens::Motion::navigationHoverScale,
         "all sidebar icons should share the same hover-scale animation");
  while (advanceNavigationIconScale(navigationIconScale, true, true))
    ;
  expect(navigationIconScale ==
             LegacyDesignTokens::Motion::navigationPressedScale,
         "all sidebar icons should share the same pressed-scale animation");

  PlaylistAnimationState playlistMotion;
  expect(getCurrentTrackIndexAfterRemoval(2, 2) == -1,
         "removing the current track should clear the playback index");
  expect(getCurrentTrackIndexAfterRemoval(2, 1) == 1,
         "removing an earlier track should decrement the playback index once");
  expect(getCurrentTrackIndexAfterRemoval(2, 3) == 2,
         "removing a later track should preserve the playback index");
  playlistMotion.startInsert(3, 2);
  expect(playlistMotion.getRowAlpha(3) == 0.0f,
         "new playlist rows should begin transparent");
  expect(playlistMotion.getRowScale(4) < 1.0f,
         "new playlist rows should begin slightly compressed");
  playlistMotion.startReorder(1, 3, 48.0f);
  expect(playlistMotion.getRowOffset(3) == -96.0f,
         "moved playlist row should begin at its previous visual position");
  expect(playlistMotion.getRowOffset(1) == 48.0f,
         "rows crossed by a downward reorder should make room smoothly");
  while (playlistMotion.tick())
    ;
  expect(playlistMotion.getRowOffset(3) == 0.0f &&
             playlistMotion.getRowAlpha(3) == 1.0f,
         "playlist row animations should settle exactly");
  playlistMotion.startShiftAfterRemoval(2, 48.0f);
  expect(playlistMotion.getRowOffset(2) == 48.0f,
         "rows after a removal should begin at their previous positions");
  while (playlistMotion.tick())
    ;
  expect(playlistMotion.getRowOffset(2) == 0.0f,
         "rows after a removal should settle into the closed gap");
  playlistMotion.startCurrentTrackTransition(1, 4);
  expect(playlistMotion.getCurrentHighlight(1) == 1.0f &&
             playlistMotion.getCurrentHighlight(4) == 0.0f,
         "current-track highlight should begin on the previous row");
  playlistMotion.tick();
  expect(playlistMotion.getCurrentHighlight(1) < 1.0f &&
             playlistMotion.getCurrentHighlight(4) > 0.0f,
         "current-track highlight should crossfade between rows");

  BackgroundScrollMotion backgroundScroll;
  backgroundScroll.setBounds(0.0f, 100.0f);
  backgroundScroll.setPosition(92.0f);
  backgroundScroll.addImpulse(18.0f);
  bool reachedOverscroll = false;
  for (int i = 0; i < 180 && backgroundScroll.tick(); ++i)
    reachedOverscroll =
        reachedOverscroll || backgroundScroll.getOverscroll() > 0.0f;
  expect(!reachedOverscroll,
         "background history should stop at the edge without overscroll");
  expect(std::abs(backgroundScroll.getPosition() - 100.0f) < 0.01f &&
             std::abs(backgroundScroll.getOverscroll()) < 0.01f,
         "background history should settle directly on the final edge");

  AudioStatusAnimation audioStatus;
  audioStatus.beginBusy();
  expect(audioStatus.getPhase() == AudioStatusAnimation::Phase::busy,
         "audio device changes should expose a busy state");
  audioStatus.complete(true);
  expect(audioStatus.getSuccessGlow() > 0.0f,
         "successful audio changes should flash the status indicator");
  audioStatus.complete(false);
  bool hadFailureShake = false;
  for (int i = 0; i < 60 && audioStatus.tick(); ++i)
    hadFailureShake =
        hadFailureShake || std::abs(audioStatus.getShakeOffset()) > 0.1f;
  expect(hadFailureShake,
         "failed audio changes should produce one short shake");

  expect(formatAudioSampleRate(48000.0) == "48.0 kHz",
         "audio sample rates should use a compact DAW label");
  expect(formatAudioBufferSize(480, 48000.0) ==
             "480 samples  |  10.0 ms",
         "audio buffer labels should include round-trip timing context");

  const auto stereoMask = makeStereoOutputMask(6, 2);
  expect(stereoMask.countNumberOfSetBits() == 2 &&
             stereoMask[2] && stereoMask[3],
         "stereo output mapping should enable the selected adjacent pair");
  expect(makeStereoOutputMask(1, 0).countNumberOfSetBits() == 1,
         "mono devices should map their only output channel");

  expect(!hasDrawableBackgroundImage({false, false, false, false}),
         "default background should bypass image-only effects");
  expect(hasDrawableBackgroundImage({false, false, true, false}),
         "a loaded original image should enable background effects");
  expect(hasDrawableBackgroundImage({false, true, false, true}),
         "the previous image should remain drawable during a transition");
  expect(
      !shouldDrawVisibleBackgroundImage({false, false, true, false}, true),
      "startup background effects should wait for the processed image");
  expect(shouldDrawVisibleBackgroundImage({false, false, true, false}, false),
         "plain startup backgrounds should draw the original image");
  expect(!shouldDrawOriginalBackgroundImage({false, false, true, false}, true),
         "effect startup should not draw the unprocessed original image");
  expect(shouldDrawOriginalBackgroundImage({false, false, true, false}, false),
         "plain startup should keep the original image fallback");
  expect(shouldUseAsyncBackgroundLoad(true, true),
         "saved startup backgrounds should keep asynchronous fallback loading");
  expect(shouldUseAsyncBackgroundLoad(false, true),
         "non-startup background changes should load asynchronously");
  expect(!shouldUseAsyncBackgroundLoad(true, false),
         "empty startup background paths should not start image loading");
  expect(shouldPrepareStartupBackgroundSynchronously(true, true, 2),
         "saved startup backgrounds with effects should be ready before paint");
  expect(!shouldPrepareStartupBackgroundSynchronously(true, true, 1),
         "plain startup backgrounds should keep asynchronous image loading");
  expect(!shouldPrepareStartupBackgroundSynchronously(false, true, 2),
         "runtime background changes should keep asynchronous effect loading");
  expect(getBackgroundWorkerStopTimeoutMs() > 0 &&
             getBackgroundWorkerStopTimeoutMs() <= 2000,
         "background worker shutdown should use a finite UI-safe timeout");
  expect(getBackgroundThumbnailWorkerThreadCount() == 2,
         "recent background thumbnails should use a small shared worker pool");
  expect(getBackgroundThumbnailMaxJobCount() >= 10,
         "thumbnail worker queue should cover the visible recent image list");

  const std::vector<juce::Colour> palette{
      juce::Colour(0xFF102030), juce::Colour(0xFF405060)};
  expect(midi::selectPaletteAccent(palette, palette[1]) == palette[1],
         "Monet should preserve a saved accent that remains in the palette");
  expect(midi::selectPaletteAccent(palette, juce::Colour(0xFFABCDEF)) ==
             palette[0],
         "Monet should select the leading swatch when the saved accent is absent");
  expect(midi::selectPaletteAccent({}, juce::Colour(0xFFABCDEF)) ==
             juce::Colour(0xFFABCDEF),
         "an empty Monet palette should preserve the saved accent");

  expect(!shouldRunMarqueeTimer(120, 180),
         "short export track names should not keep the marquee timer running");
  expect(shouldRunMarqueeTimer(240, 180),
         "overflowing export track names should run the marquee timer");
  expect(!shouldRunButtonAnimationTimer(false, 1.0f, 1.0f, 0.8f, 0.8f),
         "idle buttons at rest should not keep their animation timer running");
  expect(shouldRunButtonAnimationTimer(true, 1.0f, 1.05f, 0.8f, 1.0f),
         "hovered buttons should run their animation timer");
  expect(shouldRunButtonAnimationTimer(false, 1.02f, 1.0f, 0.82f, 0.8f),
         "buttons should keep ticking until scale and alpha settle");

  SidebarAnimationController sidebar;
  sidebar.reset(false);
  sidebar.setCollapsed(true);
  const float expandedWidth = sidebar.getWidth();
  sidebar.tick();
  expect(sidebar.getWidth() == expandedWidth,
         "collapse should hide labels before changing sidebar width");
  expect(sidebar.getLabelAlpha() < 1.0f,
         "collapse phase one should fade labels");
  while (sidebar.getPhase() ==
         SidebarAnimationController::Phase::hidingLabels)
    sidebar.tick();
  sidebar.tick();
  expect(sidebar.getWidth() < expandedWidth,
         "collapse phase two should shrink the sidebar");
  while (sidebar.isAnimating())
    sidebar.tick();
  expect(sidebar.getWidth() == SidebarAnimationController::collapsedWidth &&
             sidebar.getLabelAlpha() == 0.0f,
         "collapsed sidebar should finish at compact width with hidden labels");

  sidebar.setCollapsed(false);
  sidebar.tick();
  expect(sidebar.getWidth() >
             SidebarAnimationController::collapsedWidth &&
             sidebar.getLabelAlpha() == 0.0f,
         "expand should start with width growth and hidden labels");
  while (sidebar.getWidth() <
             SidebarAnimationController::expandedWidth * 0.72f &&
         sidebar.isAnimating())
    sidebar.tick();
  expect(sidebar.getWidth() < SidebarAnimationController::expandedWidth &&
             sidebar.getLabelAlpha() > 0.0f,
         "labels should follow the latter half of width expansion");
  bool overshotExpandedEdge = false;
  while (sidebar.isAnimating()) {
    sidebar.tick();
    overshotExpandedEdge =
        overshotExpandedEdge ||
        sidebar.getWidth() > SidebarAnimationController::expandedWidth;
  }
  expect(overshotExpandedEdge,
         "sidebar expansion should use a subtle damped edge rebound");
  expect(sidebar.getWidth() == SidebarAnimationController::expandedWidth &&
             sidebar.getLabelAlpha() == 1.0f,
         "expanded sidebar should finish with fully visible labels");

  auto historyTestDir =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getNonexistentChildFile("midi-player-history-tests", "");
  expect(historyTestDir.createDirectory(),
         "history test directory should be created");
  auto historyA = historyTestDir.getChildFile("a.png");
  auto historyADuplicate = historyTestDir.getChildFile("a-copy.png");
  auto historyB = historyTestDir.getChildFile("b.png");
  auto historyC = historyTestDir.getChildFile("c.png");
  historyA.replaceWithText("same image");
  historyADuplicate.replaceWithText("same image");
  historyB.replaceWithText("second image");
  historyC.replaceWithText("third image");

  juce::Array<juce::File> historyFiles{
      historyA, historyADuplicate, historyB, historyC};
  const auto uniqueHistory =
      getUniqueRecentBackgroundFiles(historyFiles, 10);
  expect(uniqueHistory.size() == 3,
         "duplicate background files should appear only once");

  const auto historyLayout = makeBackgroundHistoryLayout(3, 260, 8);
  expect(historyLayout.tileWidth == 126,
         "history viewport should show two equal thumbnails");
  expect(historyLayout.canScrollRight(0),
         "right arrow should be enabled at the first page");
  expect(!historyLayout.canScrollLeft(0),
         "left arrow should be disabled at the first page");
  const int finalHistoryPage =
      historyLayout.getNextPagePosition(0);
  expect(finalHistoryPage == historyLayout.maxScrollX,
         "next page should clamp to the final non-looping position");
  expect(!historyLayout.canScrollRight(finalHistoryPage),
         "right arrow should be disabled at the final page");
  expect(historyLayout.canScrollLeft(finalHistoryPage),
         "left arrow should be enabled at the final page");
  expect(historyLayout.getNextPagePosition(finalHistoryPage) ==
             finalHistoryPage,
         "history navigation should not wrap at the end");

  const auto cacheRoot = historyTestDir.getChildFile("Backgrounds");
  const auto currentBackground = cacheRoot.getChildFile("bg_current.png");
  const auto removableBackground = cacheRoot.getChildFile("bg_old.png");
  const auto externalBackground =
      historyTestDir.getChildFile("bg_external.png");
  expect(!canRemoveRecentBackgroundCache(cacheRoot, currentBackground,
                                         currentBackground),
         "the active background cache must remain protected");
  expect(canRemoveRecentBackgroundCache(cacheRoot, currentBackground,
                                        removableBackground),
         "inactive background cache entries should be removable");
  expect(!canRemoveRecentBackgroundCache(cacheRoot, currentBackground,
                                         externalBackground),
         "background removal must stay inside the managed cache directory");
  historyTestDir.deleteRecursively();

  expect(exportOfflineBlockSize <= 4096,
         "offline export blocks should keep cancellation responsive");
  expect(exportOfflineBlockSize <= PluginBridge::SharedBlockLayout::maxSamples,
         "offline export blocks should fit the plugin bridge shared block");

  const auto ogg = getExportFormatCapabilities("Ogg Vorbis");
  expect(ogg.available, "Ogg Vorbis should be available");
  expect(ogg.bitDepths.contains(32), "Ogg Vorbis should use 32-bit input");
  expect(!ogg.bitDepths.contains(16),
         "Ogg Vorbis should not advertise 16-bit input");
  expect(ogg.sampleRates.contains(96000),
         "Ogg Vorbis should allow 96 kHz");
  expect(ogg.qualityOptions.size() == 11,
         "Ogg Vorbis should expose all encoder bitrates");

  const auto validOgg =
      validateExportFormatSettings("Ogg Vorbis", 96000.0, 32, true, 10);
  expect(validOgg.wasOk(), "valid Ogg settings should pass validation");

  const auto invalidOgg =
      validateExportFormatSettings("Ogg Vorbis", 48000.0, 16, false, 5);
  expect(invalidOgg.failed(), "16-bit Ogg settings should be rejected");

  const auto wavFloat =
      createExportWriterOptions(96000.0, 32, true, 0, "Float Export");
  expect(wavFloat.getSampleFormat() ==
             juce::AudioFormatWriterOptions::SampleFormat::floatingPoint,
         "32-bit WAV float should request IEEE floating-point samples");
  expect(canCreateAndWrite("WAV", wavFloat, "RIFF"),
         "32-bit WAV float writer should encode a RIFF file");

  const auto flacInteger =
      createExportWriterOptions(96000.0, 24, false, 5, "FLAC Export");
  expect(canCreateAndWrite("FLAC", flacInteger, "fLaC"),
         "24-bit FLAC writer should encode a FLAC file");

  const auto oggFloat =
      createExportWriterOptions(96000.0, 32, true, 6, "Ogg Export");
  expect(canCreateAndWrite("Ogg Vorbis", oggFloat, "OggS"),
         "Ogg Vorbis writer should encode an Ogg stream");

  const auto highResolutionPreset = getExportPreset(2);
  expect(highResolutionPreset.qualityIndex == 5,
         "high-resolution FLAC preset should explicitly use level 5");
  expect(exportQualityComboIdFromIndex(highResolutionPreset.qualityIndex) == 6,
         "FLAC level 5 should select ComboBox item ID 6");
  expect(exportQualityIndexFromComboId(6) == 5,
         "ComboBox item ID 6 should map back to FLAC level 5");
  expect(findMatchingExportPreset("FLAC", 96000.0, 24, false, 5, true) == 2,
         "FLAC level 5 settings should match the high-resolution preset");
  expect(findMatchingExportPreset("FLAC", 96000.0, 24, false, 4, true) == 4,
         "a different FLAC level should be treated as custom");

  auto tempDir = juce::File::getSpecialLocation(
                     juce::File::tempDirectory)
                     .getNonexistentChildFile("midi-player-export-tests", "");
  expect(tempDir.createDirectory(), "temporary test directory should exist");
  const auto chosenFile = tempDir.getChildFile("mix.wav");
  const auto normalisedFile = normaliseExportTargetFile(chosenFile, "FLAC");
  expect(normalisedFile.hasFileExtension(".flac"),
         "export target should use the selected format extension");
  normalisedFile.replaceWithText("existing");
  expect(exportTargetNeedsOverwriteConfirmation(normalisedFile),
         "overwrite confirmation should inspect the normalised target");

  juce::AudioBuffer<float> floatBuffer(2, 1);
  floatBuffer.setSample(0, 0, 2.0f);
  floatBuffer.setSample(1, 0, -2.0f);
  applyMasterOutputStage(
      floatBuffer, 0.8f,
      shouldPreserveExportHeadroom("WAV", 32, true));
  expect(floatBuffer.getSample(0, 0) > 1.5f,
         "32-bit float WAV should preserve samples above 0 dBFS");

  juce::AudioBuffer<float> integerBuffer(2, 1);
  integerBuffer.setSample(0, 0, 2.0f);
  integerBuffer.setSample(1, 0, -2.0f);
  applyMasterOutputStage(
      integerBuffer, 0.8f,
      shouldPreserveExportHeadroom("WAV", 16, false));
  expect(integerBuffer.getMagnitude(0, 1) < 1.0f,
         "integer export should use the playback soft limiter");

  expect(shouldDitherExport("WAV", 16, false),
         "16-bit WAV should use TPDF dither");
  expect(shouldDitherExport("FLAC", 24, false),
         "24-bit FLAC should use TPDF dither");
  expect(!shouldDitherExport("WAV", 32, true),
         "32-bit float WAV should not use dither");
  expect(!shouldDitherExport("Ogg Vorbis", 32, true),
         "Ogg Vorbis input should not use PCM dither");

  juce::AudioBuffer<float> ditherBuffer(2, 256);
  ditherBuffer.clear();
  expect(measureExportTailLevel(ditherBuffer) == 0.0f,
         "tail detection should see digital silence before dither");
  juce::Random ditherRandom(12345);
  applyTpdfDither(ditherBuffer, 16, ditherRandom);
  expect(ditherBuffer.getMagnitude(0, ditherBuffer.getNumSamples()) > 0.0f,
         "TPDF dither should decorrelate quantisation from digital silence");
  expect(measureExportTailLevel(ditherBuffer) > 0.00001f,
         "16-bit dither must not be included in automatic tail detection");

  tempDir.deleteRecursively();

  auto pluginTempDir =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getNonexistentChildFile("midi-player-plugin-list-tests", "");
  expect(pluginTempDir.createDirectory(),
         "plugin-list temporary directory should exist");
  const auto existingVst3 = pluginTempDir.getChildFile("Existing.vst3");
  expect(existingVst3.createDirectory(),
         "existing VST3 test bundle should exist");
  const auto missingVst3 = pluginTempDir.getChildFile("Missing.vst3");

  juce::PluginDescription existingPlugin;
  existingPlugin.name = "Existing";
  existingPlugin.pluginFormatName = "VST3";
  existingPlugin.fileOrIdentifier = existingVst3.getFullPathName();
  existingPlugin.uniqueId = 1;

  juce::PluginDescription missingPlugin;
  missingPlugin.name = "Missing";
  missingPlugin.pluginFormatName = "VST3";
  missingPlugin.fileOrIdentifier = missingVst3.getFullPathName();
  missingPlugin.uniqueId = 2;

  juce::KnownPluginList knownPlugins;
  expect(knownPlugins.addType(existingPlugin),
         "existing plugin should be added to the test list");
  expect(knownPlugins.addType(missingPlugin),
         "missing plugin should be added to the test list");

  juce::AudioPluginFormatManager pluginFormatManager;
  pluginFormatManager.addFormat(std::make_unique<juce::VST3PluginFormat>());
  expect(removeMissingPluginTypes(knownPlugins, pluginFormatManager) == 1,
         "full scan cleanup should remove exactly one missing plugin");
  expect(knownPlugins.getNumTypes() == 1,
         "full scan cleanup should retain existing plugins");
  expect(knownPlugins.getTypes()[0].fileOrIdentifier ==
             existingVst3.getFullPathName(),
         "full scan cleanup should retain the existing plugin path");
  pluginTempDir.deleteRecursively();

  {
    PluginBridge::PluginLoadRequest request;
    request.name = "Ivory VST";
    request.format = "VST3";
    request.fileOrIdentifier =
        "C:\\Program Files\\Common Files\\VST3\\Synthogy\\Ivory.vst3\\Contents\\x86_64-win\\Ivory.vst3";
    request.uniqueId = 0xe6abd61b;
    request.sharedBlockName = "Local\\ModernMidiPlayerPluginBridge-test";

    const auto block = PluginBridge::toMemoryBlock(request);
    const auto decoded = PluginBridge::loadRequestFromMemoryBlock(block);

    expect(decoded.name == request.name,
           "plugin bridge load request should preserve plugin name");
    expect(decoded.fileOrIdentifier == request.fileOrIdentifier,
           "plugin bridge load request should preserve plugin path");
    expect(decoded.uniqueId == request.uniqueId,
           "plugin bridge load request should preserve unique ID");
    expect(decoded.sharedBlockName == request.sharedBlockName,
           "plugin bridge load request should preserve shared block name");
  }

  {
    const auto reply =
        PluginBridge::makeStatusReply(PluginBridge::StatusCode::pluginCrashed,
                                      "worker process exited",
                                      PluginBridge::Command::loadPlugin);
    const auto decoded = PluginBridge::statusReplyFromMemoryBlock(reply);
    expect(decoded.code == PluginBridge::StatusCode::pluginCrashed,
           "plugin bridge status reply should preserve crash status");
    expect(decoded.command == PluginBridge::Command::loadPlugin,
           "plugin bridge status reply should preserve command context");
    expect(decoded.message == "worker process exited",
           "plugin bridge status reply should preserve diagnostic text");
  }

  expect(PluginBridge::isPluginWorkerCommandLine(
             juce::String("--worker ") + PluginBridge::workerCommandLineUid),
         "plugin worker command line should be recognised");
  expect(!PluginBridge::isPluginWorkerCommandLine(
             "--worker modern-midi-player-plugin-worker-v1"),
         "stale v1 workers should be rejected by the v2 host protocol");
  expect(!PluginBridge::isPluginWorkerCommandLine("C:\\song.mid"),
         "regular MIDI file command line should not start worker mode");

  auto commandLineTempDir =
      juce::File::getSpecialLocation(juce::File::tempDirectory)
          .getNonexistentChildFile("midi-player-command-line-tests", "");
  expect(commandLineTempDir.createDirectory(),
         "command-line temporary directory should exist");
  const auto commandLineMidi =
      commandLineTempDir.getChildFile("song with spaces.mid");
  expect(commandLineMidi.replaceWithText("MThd"),
         "command-line test MIDI file should be writable");
  const auto commandLineText = commandLineTempDir.getChildFile("notes.txt");
  expect(commandLineText.replaceWithText("not midi"),
         "command-line non-MIDI file should be writable");

  expect(parseMidiFileFromCommandLine(commandLineMidi.getFullPathName()) ==
             commandLineMidi,
         "a single unquoted MIDI path with spaces should still be accepted");
  expect(parseMidiFileFromCommandLine("--from-shell " +
                                      commandLineMidi.getFullPathName().quoted()) ==
             commandLineMidi,
         "a quoted MIDI path after an option should be detected");
  expect(parseMidiFileFromCommandLine(commandLineText.getFullPathName().quoted() +
                                      " " +
                                      commandLineMidi.getFullPathName().quoted()) ==
             commandLineMidi,
         "command-line parsing should skip non-MIDI file arguments");

  PlaylistManager persistencePlaylist;
  const auto playlistSaveResult = persistencePlaylist.saveDetailed(commandLineTempDir);
  expect(playlistSaveResult.failed() &&
             playlistSaveResult.getErrorMessage().isNotEmpty(),
         "playlist save failures should return a diagnostic message");
  juce::PropertySet testSettings;
  testSettings.setValue("answer", 42);
  const auto settingsSaveResult =
      UserSettings::writeSettingsAtomically(testSettings, commandLineTempDir);
  expect(settingsSaveResult.failed() &&
             settingsSaveResult.getErrorMessage().isNotEmpty(),
         "settings save failures should return a diagnostic message");
  commandLineTempDir.deleteRecursively();

  expect(makeMidiFileAssociationCommand("C:\\Program Files\\Midi Player\\Player.exe") ==
             "\"C:\\Program Files\\Midi Player\\Player.exe\" \"%1\"",
         "file association command should quote the executable and shell path");
  expect(getMidiFileProgId() == "ModernMidiPlayer.MIDIFile",
         "file association ProgID should remain stable");

  PluginBridge::BridgeState bridgeState;
  expect(bridgeState.status == PluginBridge::BridgeStatus::stopped,
         "plugin bridge should start stopped");
  bridgeState.markStarting();
  expect(bridgeState.status == PluginBridge::BridgeStatus::starting,
         "plugin bridge should expose startup state");
  bridgeState.markReady();
  expect(bridgeState.status == PluginBridge::BridgeStatus::ready,
         "plugin bridge should expose ready state");
  bridgeState.markCrashed("worker process exited");
  expect(bridgeState.status == PluginBridge::BridgeStatus::crashed,
         "plugin bridge should expose crashed state");
  expect(bridgeState.message == "worker process exited",
         "plugin bridge should keep explicit crash diagnostics");

  {
    PluginBridge::PluginBridgeClient bridgeClient;
    expect(bridgeClient.start(),
           "plugin bridge client should launch and connect to a worker process");
    expect(bridgeClient.getState().status == PluginBridge::BridgeStatus::ready,
           "plugin bridge client should expose connected worker state");
    bridgeClient.stop();
  }

  {
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_OK", L"1");
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_RENDER_DELAY_MS", L"75");
    PluginBridge::PluginBridgeClient bridgeClient;
    juce::PluginDescription desc;
    desc.name = "Late render recovery test plugin";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "C:\\test\\LateRenderPlugin.vst3";
    expect(bridgeClient.loadPlugin(desc),
           "plugin bridge should load the delayed-render test worker");
    expect(bridgeClient.prepare(48000.0, 64),
           "plugin bridge should prepare the delayed-render test worker");

    juce::AudioBuffer<float> buffer(2, 64);
    juce::MidiBuffer midi;
    expect(!bridgeClient.processBlock(midi, buffer, 48000.0),
           "one late realtime render should produce silence for that block");
    expect(bridgeClient.getStatus() == PluginBridge::BridgeStatus::ready,
           "one late realtime render should keep the worker recoverable");
    juce::Thread::sleep(100);
    expect(bridgeClient.processBlock(midi, buffer, 48000.0),
           "the next block should drain the late response and resume rendering");
    expect(bridgeClient.getStatus() == PluginBridge::BridgeStatus::ready,
           "late render recovery should keep the bridge ready");
    bridgeClient.unloadPlugin();
    bridgeClient.stop();
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_OK");
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_RENDER_DELAY_MS");
  }

  {
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_IGNORE_SHUTDOWN", L"1");
    PluginBridge::PluginBridgeClient bridgeClient;
    expect(bridgeClient.start(),
           "plugin bridge should connect to a shutdown-hang test worker");
    const auto stopStarted = juce::Time::getMillisecondCounter();
    bridgeClient.stop();
    const auto stopElapsed =
        juce::Time::getMillisecondCounter() - stopStarted;
    expect(stopElapsed < 5000,
           "hung worker shutdown should be forcibly bounded");
    expect(bridgeClient.getStatus() == PluginBridge::BridgeStatus::stopped,
           "forced worker shutdown should leave the bridge stopped");
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_IGNORE_SHUTDOWN");
  }

  {
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_AFTER_READY", L"1");
    PluginBridge::PluginBridgeClient bridgeClient;
    const bool started = bridgeClient.start();
    const bool crashed = waitForBridgeStatus(
        bridgeClient, PluginBridge::BridgeStatus::crashed, 20000);
    expect(started || crashed,
           "plugin bridge client should either start or report a worker crash");
    expect(crashed,
           "plugin bridge client should mark crashed when the worker exits");
    expect(bridgeClient.getLastError().contains("connection lost"),
           "plugin bridge client should preserve the worker disconnect error");
    bridgeClient.terminateCrashedWorker();
    expect(bridgeClient.getStatus() == PluginBridge::BridgeStatus::stopped,
           "terminating a crashed worker should return the bridge to reloadable state");
    expect(bridgeClient.getLastError().contains("connection lost"),
           "terminating a crashed worker should keep the crash diagnostic");
    bridgeClient.stop();
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_AFTER_READY");
  }

  {
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_ON_LOAD", L"1");
    PluginBridge::PluginBridgeClient bridgeClient;
    juce::PluginDescription desc;
    desc.name = "Ivory VST";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "C:\\test\\Ivory.vst3";
    const bool loaded = bridgeClient.loadPlugin(desc);
    expect(!loaded,
           "plugin bridge client should report worker exits during plugin load");
    expect(bridgeClient.getLastError().contains("loadPlugin") &&
               bridgeClient.getLastError().contains("Ivory VST"),
           "worker disconnect diagnostics should include the active load command and plugin name");
    bridgeClient.terminateCrashedWorker();
    bridgeClient.stop();
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_EXIT_ON_LOAD");
  }

  {
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_IGNORE_LOAD", L"1");
    PluginBridge::PluginBridgeClient bridgeClient;
    juce::PluginDescription desc;
    desc.name = "Cancelled test plugin";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "C:\\test\\CancelledPlugin.vst3";
    std::atomic<bool> loadResult{true};
    std::thread loadThread(
        [&] { loadResult = bridgeClient.loadPlugin(desc); });
    const auto deadline = juce::Time::getMillisecondCounter() + 5000u;
    while (bridgeClient.getStatus() != PluginBridge::BridgeStatus::ready &&
           juce::Time::getMillisecondCounter() < deadline)
      juce::Thread::sleep(5);
    const auto cancelStarted = juce::Time::getMillisecondCounter();
    bridgeClient.cancelPendingOperation();
    loadThread.join();
    expect(!loadResult.load(),
           "cancelling a pending plugin load should return failure");
    expect(juce::Time::getMillisecondCounter() - cancelStarted < 5000,
           "pending plugin load cancellation should be bounded");
    bridgeClient.terminateCrashedWorker();
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_IGNORE_LOAD");
  }

  {
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_FAIL", L"1");
    PluginBridge::PluginBridgeClient bridgeClient;
    juce::PluginDescription desc;
    desc.name = "Failing test plugin";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "C:\\missing\\FailingTestPlugin.vst3";
    const bool loaded = bridgeClient.loadPlugin(desc);
    expect(!loaded,
           "plugin bridge client should report worker plugin load failures");
    expect(bridgeClient.getStatus() == PluginBridge::BridgeStatus::ready,
           "plugin load failure replies should keep the worker ready");
    expect(bridgeClient.getLastError().contains("test load failed"),
           "plugin load failure replies should preserve diagnostics");
    bridgeClient.stop();
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_FAIL");
  }

  {
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_OK", L"1");
    setTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_UNLOAD_DELAY_MS", L"200");
    PluginBridge::PluginBridgeClient bridgeClient;
    juce::PluginDescription desc;
    desc.name = "Unload wait test plugin";
    desc.pluginFormatName = "VST3";
    desc.fileOrIdentifier = "C:\\test\\UnloadWaitPlugin.vst3";
    expect(bridgeClient.loadPlugin(desc),
           "plugin bridge client should load the test worker plugin");
    const auto unloadStart = juce::Time::getMillisecondCounter();
    bridgeClient.unloadPlugin();
    const auto unloadElapsed =
        juce::Time::getMillisecondCounter() - unloadStart;
    expect(unloadElapsed >= 150,
           "plugin bridge unload should wait for the worker acknowledgement");
    expect(bridgeClient.getStatus() == PluginBridge::BridgeStatus::stopped,
           "plugin bridge unload should stop the worker after a clean unload");
    bridgeClient.stop();
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_LOAD_OK");
    clearTestEnvironmentVariable(
        L"MIDI_PLAYER_PLUGIN_BRIDGE_TEST_UNLOAD_DELAY_MS");
  }

  expect(PluginBridge::SharedBlockLayout::maxChannels == 2,
         "bridge audio block should match current stereo app output");
  expect(PluginBridge::SharedBlockLayout::maxSamples == 1024,
         "bridge audio block should retain the fixed IPC payload size");
  expect(PluginBridge::getSharedBlockChunkCount(1024) == 1,
         "a 1024-sample host block should use one bridge chunk");
  expect(PluginBridge::getSharedBlockChunkCount(2048) == 2,
         "a 2048-sample host block should use two bridge chunks");
  expect(PluginBridge::getSharedBlockChunkCount(2560) == 3,
         "a DirectSound-sized host block should use three bridge chunks");
  expect(PluginBridge::getSharedBlockChunkCount(4096) == 4,
         "a 4096-sample host block should use four bridge chunks");
  expect(PluginBridge::getSharedBlockChunkSize(2560, 2048) == 512,
         "the final bridge chunk should contain the remaining samples");
  expect(PluginBridge::SharedBlockLayout::maxMidiBytes >= 8192,
         "bridge MIDI payload should fit dense file playback blocks");
  expect(PluginBridge::getWorkerProcessChannelCount(22) == 22,
         "multi-output instruments should process with their full output count");
  expect(PluginBridge::getWorkerProcessChannelCount(1) ==
             PluginBridge::SharedBlockLayout::maxChannels,
         "mono instruments should still render into the stereo bridge output");
  expect(PluginBridge::shouldUseExplicitStereoHostConfig(2),
         "stereo instruments should use the explicit stereo host layout");
  expect(!PluginBridge::shouldUseExplicitStereoHostConfig(22),
         "multi-output instruments should keep their default bus layout");

  juce::MidiBuffer bridgeMidi;
  bridgeMidi.addEvent(juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 7);
  bridgeMidi.addEvent(juce::MidiMessage::controllerEvent(1, 64, 127), 11);
  unsigned char midiBytes[PluginBridge::SharedBlockLayout::maxMidiBytes]{};
  const int written = PluginBridge::writeMidiBuffer(
      bridgeMidi, midiBytes, PluginBridge::SharedBlockLayout::maxMidiBytes);
  expect(written > 0, "bridge MIDI serialization should write bytes");
  juce::MidiBuffer decodedBridgeMidi;
  PluginBridge::readMidiBuffer(midiBytes, written, decodedBridgeMidi);
  expect(findNoteSamplePosition(decodedBridgeMidi, true, 1, 60) == 7,
         "bridge MIDI serialization should preserve note sample position");
  expect(findControllerSamplePosition(decodedBridgeMidi, 1, 64, 127) == 11,
         "bridge MIDI serialization should preserve controller sample position");

  juce::MidiBuffer rangedBridgeMidi;
  rangedBridgeMidi.addEvent(
      juce::MidiMessage::noteOn(1, 60, (juce::uint8)100), 100);
  rangedBridgeMidi.addEvent(
      juce::MidiMessage::noteOn(1, 61, (juce::uint8)100), 1100);
  rangedBridgeMidi.addEvent(
      juce::MidiMessage::noteOn(1, 62, (juce::uint8)100), 2500);
  const int rangedBytes = PluginBridge::writeMidiBufferRange(
      rangedBridgeMidi, midiBytes, PluginBridge::SharedBlockLayout::maxMidiBytes,
      1024, 1024);
  juce::MidiBuffer decodedRange;
  PluginBridge::readMidiBuffer(midiBytes, rangedBytes, decodedRange);
  expect(findNoteSamplePosition(decodedRange, true, 1, 61) == 76,
         "bridge chunk serialization should rebase MIDI sample positions");
  expect(findNoteSamplePosition(decodedRange, true, 1, 60) < 0 &&
             findNoteSamplePosition(decodedRange, true, 1, 62) < 0,
         "bridge chunk serialization should exclude adjacent chunk events");

  PluginBridge::PrepareRequest prepareRequest;
  prepareRequest.sampleRate = 96000.0;
  prepareRequest.blockSize = 4096;
  prepareRequest.nonRealtime = true;
  const auto parsedPrepare = PluginBridge::prepareRequestFromValueTree(
      PluginBridge::blockToValueTree(
          PluginBridge::makePrepareCommand(prepareRequest)));
  expect(parsedPrepare.sampleRate == prepareRequest.sampleRate &&
             parsedPrepare.blockSize == prepareRequest.blockSize &&
             parsedPrepare.nonRealtime,
         "bridge prepare protocol should preserve non-realtime mode");
  expect(PluginBridge::getWorkerRenderTimeoutMs(64, 48000.0) == 10,
         "small real-time blocks should retain a scheduler-safe timeout floor");
  expect(PluginBridge::getWorkerRenderTimeoutMs(1024, 48000.0) == 43,
         "render timeout should track two host buffer periods");
  expect(PluginBridge::workerRenderHangTimeoutMs >
             PluginBridge::getWorkerRenderTimeoutMs(1024, 48000.0),
         "a missed realtime deadline should remain distinct from a worker hang");
  PluginBridge::SharedBlockHeader sequencedHeader;
  sequencedHeader.requestSequence = 41;
  sequencedHeader.responseSequence = 41;
  expect(sequencedHeader.requestSequence == sequencedHeader.responseSequence,
         "bridge render replies should identify the request they completed");

  PluginWindowLifecycle pluginLifecycle;
  int pluginA = 1;
  int pluginB = 2;

  const int firstPluginGeneration = pluginLifecycle.beginSwitch();
  const auto pluginARequest = pluginLifecycle.makeRequest(&pluginA);
  expect(pluginARequest.generation == firstPluginGeneration,
         "plugin editor requests should capture the active switch generation");
  expect(pluginLifecycle.isCurrent(pluginARequest, &pluginA),
         "a request should remain valid for the same processor generation");

  pluginLifecycle.markWindowOpened(pluginARequest);
  expect(pluginLifecycle.windowMatchesCurrent(&pluginA),
         "an opened plugin window should be tied to its processor identity");

  pluginLifecycle.beginSwitch();
  expect(!pluginLifecycle.isCurrent(pluginARequest, &pluginA),
         "old editor requests should be rejected after a plugin switch");
  expect(!pluginLifecycle.windowMatchesCurrent(&pluginA),
         "plugin switching should invalidate the previous editor window");

  const auto pluginBRequest = pluginLifecycle.makeRequest(&pluginB);
  expect(pluginLifecycle.isCurrent(pluginBRequest, &pluginB),
         "the latest plugin request should match the latest processor");
  expect(!pluginLifecycle.isCurrent(pluginBRequest, &pluginA),
         "a request must not apply to a different processor pointer");
  expect(getPluginEditorOpenDelayMs("Ivory VST") ==
             getPluginEditorOpenDelayMs("Vienna Synchron Pianos"),
         "plugin editor scheduling should use one lifecycle path for all plugins");
  expect(shouldShowPluginSwitchPausedNotice(true, false),
         "switching plugins during playback should tell the user playback paused");
  expect(shouldShowPluginSwitchPausedNotice(false, true),
         "switching plugins with a pending resume should keep playback paused");
  expect(!shouldShowPluginSwitchPausedNotice(false, false),
         "switching plugins while already stopped should not show a pause notice");
  expect(makeLoadedPluginLabel("Ivory VST", true)
             .contains(juce::String(L"请手动恢复")),
         "plugin load label should tell users to resume manually after a plugin switch");
  expect(makeLoadedPluginLabel("Ivory VST", false) ==
             juce::String(L"已加载: Ivory VST"),
         "plugin load label should stay unchanged when playback was already stopped");
  expect(!shouldAutoOpenPluginEditorAfterLoad("Ivory VST"),
         "Ivory should not auto-open its editor immediately after loading");
  expect(shouldAutoOpenPluginEditorAfterLoad("Vienna Synchron Pianos"),
         "non-Ivory plugins should keep the existing auto-editor behaviour");

  ExportHintState hints;
  hints.setHoveredTarget(ExportHintTarget::sampleRate);
  hints.setPopupTarget(ExportHintTarget::sampleRate, true);
  hints.setHoveredTarget(ExportHintTarget::bitDepth);
  expect(hints.getActiveTarget() == ExportHintTarget::sampleRate,
         "open popup should freeze the ComboBox hint");
  hints.setPopupTarget(ExportHintTarget::sampleRate, false);
  hints.setHoveredTarget(ExportHintTarget::bitDepth);
  expect(hints.getActiveTarget() == ExportHintTarget::bitDepth,
         "hint should update after the popup closes");

  MidiPlayer midiPlayer;
  auto playingSequence = std::make_unique<juce::MidiMessageSequence>();
  addEvent(*playingSequence, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100),
           0.0);
  addEvent(*playingSequence, juce::MidiMessage::controllerEvent(1, 64, 127),
           0.0);
  addEvent(*playingSequence, juce::MidiMessage::noteOff(1, 60), 1.0);
  midiPlayer.setSequence(std::move(playingSequence), 1000.0);
  midiPlayer.setPlaying(true);

  juce::MidiBuffer playingEvents;
  midiPlayer.processBlock(playingEvents, 128);

  midiPlayer.setSequence(std::make_unique<juce::MidiMessageSequence>(),
                         1000.0);
  juce::MidiBuffer trackSwitchEvents;
  midiPlayer.processBlock(trackSwitchEvents, 128);
  expect(containsNoteOff(trackSwitchEvents, 1, 60),
         "switching tracks should directly release every active note");
  expect(containsController(trackSwitchEvents, 1, 64, 0),
         "switching tracks should explicitly release the sustain pedal");
  expect(containsController(trackSwitchEvents, 1, 66, 0),
         "switching tracks should explicitly release the sostenuto pedal");
  expect(containsController(trackSwitchEvents, 1, 67, 0),
         "switching tracks should explicitly release the soft pedal");

  MidiPlayer silentSeekPlayer;
  auto silentSeekSequence = std::make_unique<juce::MidiMessageSequence>();
  addEvent(*silentSeekSequence,
           juce::MidiMessage::noteOn(1, 61, (juce::uint8)100), 0.0);
  addEvent(*silentSeekSequence,
           juce::MidiMessage::controllerEvent(1, 64, 127), 0.0);
  addEvent(*silentSeekSequence, juce::MidiMessage::noteOff(1, 61), 0.2);
  addEvent(*silentSeekSequence,
           juce::MidiMessage::controllerEvent(1, 64, 0), 0.2);
  addEvent(*silentSeekSequence,
           juce::MidiMessage::controllerEvent(1, 1, 0), 1.0);
  silentSeekPlayer.setSequence(std::move(silentSeekSequence), 1000.0);
  silentSeekPlayer.setPlaying(true);
  juce::MidiBuffer silentSeekStart;
  silentSeekPlayer.processBlock(silentSeekStart, 128);
  silentSeekPlayer.seekTo(500.0);
  juce::MidiBuffer silentSeekEvents;
  silentSeekPlayer.processBlock(silentSeekEvents, 128);
  expect(containsNoteOff(silentSeekEvents, 1, 61),
         "seeking past a note should directly release its previous key state");

  MidiPlayer activeSeekPlayer;
  auto activeSeekSequence = std::make_unique<juce::MidiMessageSequence>();
  addEvent(*activeSeekSequence,
           juce::MidiMessage::noteOn(1, 62, (juce::uint8)100), 0.0);
  addEvent(*activeSeekSequence,
           juce::MidiMessage::controllerEvent(1, 64, 127), 0.0);
  addEvent(*activeSeekSequence, juce::MidiMessage::noteOff(1, 62), 1.0);
  addEvent(*activeSeekSequence,
           juce::MidiMessage::controllerEvent(1, 64, 0), 1.0);
  activeSeekPlayer.setSequence(std::move(activeSeekSequence), 1000.0);
  activeSeekPlayer.setPlaying(true);
  juce::MidiBuffer activeSeekStart;
  activeSeekPlayer.processBlock(activeSeekStart, 128);
  activeSeekPlayer.seekTo(500.0);
  juce::MidiBuffer activeSeekEvents;
  activeSeekPlayer.processBlock(activeSeekEvents, 128);
  const int oldNoteOffIndex = findNoteEventIndex(activeSeekEvents, false, 1, 62);
  const int chasedNoteOnIndex = findNoteEventIndex(activeSeekEvents, true, 1, 62);
  expect(oldNoteOffIndex >= 0 && chasedNoteOnIndex > oldNoteOffIndex,
         "seek should release the old key before chasing the target key state");
  const int pedalOffIndex = findControllerIndex(activeSeekEvents, 1, 64, 0);
  const int chasedPedalOnIndex = findControllerIndex(activeSeekEvents, 1, 64, 127);
  expect(pedalOffIndex >= 0 && chasedPedalOnIndex > pedalOffIndex,
         "seek should release the old pedal before chasing the target pedal state");

  expect(findControllerSamplePosition(activeSeekEvents, 1, 64, 0) == 0,
         "seek should reset sustain at sample 0");
  expect(findControllerSamplePosition(activeSeekEvents, 1, 64, 127) == 1,
         "seek should restore sustain at sample 1");
  expect(findNoteSamplePosition(activeSeekEvents, true, 1, 62) == 1,
         "seek should restore active notes at sample 1");
  expect(activeSeekPlayer.getPositionInSamples() == 500.0,
         "the silent seek reconstruction block must not advance playback");

  juce::MidiBuffer resumedPlayback;
  activeSeekPlayer.processBlock(resumedPlayback, 128);
  expect(activeSeekPlayer.getPositionInSamples() == 628.0,
         "playback should advance from the seek target on the next block");

  activeSeekPlayer.setPlaying(false);
  activeSeekPlayer.triggerStopCleanup();
  juce::MidiBuffer chasedNoteCleanup;
  activeSeekPlayer.processBlock(chasedNoteCleanup, 128);
  expect(containsNoteOff(chasedNoteCleanup, 1, 62),
         "stop cleanup should release notes restored by seek chase");

  MidiPlayer pauseResumePlayer;
  auto pauseResumeSequence = std::make_unique<juce::MidiMessageSequence>();
  addEvent(*pauseResumeSequence,
           juce::MidiMessage::noteOn(1, 65, (juce::uint8)100), 0.0);
  addEvent(*pauseResumeSequence,
           juce::MidiMessage::controllerEvent(1, 64, 127), 0.0);
  addEvent(*pauseResumeSequence, juce::MidiMessage::noteOff(1, 65), 1.0);
  addEvent(*pauseResumeSequence,
           juce::MidiMessage::controllerEvent(1, 64, 0), 1.0);
  pauseResumePlayer.setSequence(std::move(pauseResumeSequence), 1000.0);
  pauseResumePlayer.setPlaying(true);
  juce::MidiBuffer pauseResumeStart;
  pauseResumePlayer.processBlock(pauseResumeStart, 128);

  pauseResumePlayer.setPlaying(false);
  pauseResumePlayer.triggerStopCleanup();
  juce::MidiBuffer pauseCleanup;
  pauseResumePlayer.processBlock(pauseCleanup, 128);
  expect(containsController(pauseCleanup, 1, 64, 0),
         "pause cleanup should release sustain while output is silent");

  const double draggedPausedPosition = 500.0;
  pauseResumePlayer.seekTo(draggedPausedPosition);
  juce::MidiBuffer pausedSeekEvents;
  pauseResumePlayer.processBlock(pausedSeekEvents, 128);
  expect(pausedSeekEvents.isEmpty(),
         "seeking while paused should only update position and emit no MIDI");
  expect(pauseResumePlayer.getPositionInSamples() == draggedPausedPosition,
         "paused seek should update the stored playback position");

  pauseResumePlayer.seekTo(draggedPausedPosition, true);
  pauseResumePlayer.setPlaying(true);
  juce::MidiBuffer pauseResumeChase;
  pauseResumePlayer.processBlock(pauseResumeChase, 128);
  expect(findControllerSamplePosition(pauseResumeChase, 1, 64, 127) == 1,
         "resume should restore the sustained CC64 state at sample 1");
  expect(findNoteSamplePosition(pauseResumeChase, true, 1, 65) == 1,
         "resume should restore the sustained note at sample 1");
  expect(pauseResumePlayer.getPositionInSamples() == draggedPausedPosition,
         "resume reconstruction should preserve the seek target");

  MidiPlayer releasedNotePlayer;
  auto releasedNoteSequence = std::make_unique<juce::MidiMessageSequence>();
  addEvent(*releasedNoteSequence,
           juce::MidiMessage::noteOn(1, 63, (juce::uint8)100), 0.0);
  addEvent(*releasedNoteSequence, juce::MidiMessage::noteOff(1, 63), 0.05);
  addEvent(*releasedNoteSequence,
           juce::MidiMessage::controllerEvent(1, 1, 0), 1.0);
  releasedNotePlayer.setSequence(std::move(releasedNoteSequence), 1000.0);
  releasedNotePlayer.setPlaying(true);
  juce::MidiBuffer releasedNotePlayback;
  releasedNotePlayer.processBlock(releasedNotePlayback, 128);
  releasedNotePlayer.setPlaying(false);
  releasedNotePlayer.triggerStopCleanup();
  juce::MidiBuffer releasedNoteCleanup;
  releasedNotePlayer.processBlock(releasedNoteCleanup, 128);
  expect(!containsNoteOff(releasedNoteCleanup, 1, 63),
         "cleanup should not repeat a note-off already sent during playback");

  MidiPlayer unfinishedNotePlayer;
  auto unfinishedNoteSequence = std::make_unique<juce::MidiMessageSequence>();
  addEvent(*unfinishedNoteSequence,
           juce::MidiMessage::noteOn(1, 64, (juce::uint8)100), 0.0);
  addEvent(*unfinishedNoteSequence,
           juce::MidiMessage::controllerEvent(1, 1, 0), 0.1);
  unfinishedNotePlayer.setSequence(std::move(unfinishedNoteSequence), 1000.0);
  unfinishedNotePlayer.setPlaying(true);
  juce::MidiBuffer unfinishedNotePlayback;
  unfinishedNotePlayer.processBlock(unfinishedNotePlayback, 128);
  expect(unfinishedNotePlayer.isWaitingForTail(),
         "a naturally ended sequence should keep rendering its plugin tail");
  expect(!unfinishedNotePlayer.hasFinished(),
         "track completion should wait for the measured plugin tail");
  unfinishedNotePlayer.finishTail();
  expect(unfinishedNotePlayer.hasFinished(),
         "tail completion should publish the natural track end");
  unfinishedNotePlayer.triggerStopCleanup();
  juce::MidiBuffer unfinishedNoteCleanup;
  unfinishedNotePlayer.processBlock(unfinishedNoteCleanup, 128);
  expect(containsNoteOff(unfinishedNoteCleanup, 1, 64),
         "track-end cleanup should release an unmatched active note");

  MidiPlayer sysExPlayer;
  auto sysExSequence = std::make_unique<juce::MidiMessageSequence>();
  const juce::uint8 sysExData[] = {0x41, 0x10, 0x42, 0x12, 0x00, 0x00, 0x7f};
  addEvent(*sysExSequence,
           juce::MidiMessage::createSysExMessage(sysExData,
                                                 sizeof(sysExData)),
           0.0);
  addEvent(*sysExSequence, juce::MidiMessage::noteOn(1, 60, (juce::uint8)100),
           0.1);
  sysExPlayer.setSequence(std::move(sysExSequence), 1000.0);
  sysExPlayer.setPlaying(true);
  juce::MidiBuffer sysExOutput;
  sysExPlayer.processBlock(sysExOutput, 128);
  bool foundSysEx = false;
  for (const auto metadata : sysExOutput)
    foundSysEx = foundSysEx || metadata.getMessage().isSysEx();
  expect(foundSysEx,
         "MIDI playback should preserve GS/XG system-exclusive messages");

  runRealPluginBridgeSmokeIfRequested();

  failures += miditest::runWorkerPathTests();
  failures += miditest::runCoreTests();
  failures += miditest::runExportTaskTests();
  failures += miditest::runPersistenceTests();
  failures += miditest::runWinBridgeTests();

  if (failures == 0)
    std::cout << "All tests passed\n";

  return failures == 0 ? 0 : 1;
}
