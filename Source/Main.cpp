#include "Core/Core.h"
#include "PluginBridge/PluginScanProcess.h"
#include "PluginBridge/PluginWorkerProcess.h"
#include "UI/MainContentComponent.h"
#include "Utils/CommandLineMidiFileSupport.h"
#include "Utils/DebugLogger.h"
#include <juce_gui_extra/juce_gui_extra.h>

class ModernMidiPlayerApplication : public juce::JUCEApplication {
public:
  ModernMidiPlayerApplication() = default;

  const juce::String getApplicationName() override { return L"MIDI 播放器"; }

  const juce::String getApplicationVersion() override { return "1.0.0"; }

  bool moreThanOneInstanceAllowed() override {
    const auto commandLine =
        juce::JUCEApplicationBase::getCommandLineParameters();
    return PluginBridge::isPluginWorkerCommandLine(commandLine) ||
           PluginBridge::isPluginScanCommandLine(commandLine);
  }

  void initialise(const juce::String &commandLine) override {
    if (PluginBridge::runPluginScanIfRequested(commandLine))
      return;

    if (PluginBridge::runWorkerIfRequested(commandLine))
      return;

    DebugLogger::init();
    LOG_DEBUG("Application Initialising...");

    applicationLookAndFeel = std::make_unique<FluentLookAndFeel>(true);
    juce::LookAndFeel::setDefaultLookAndFeel(applicationLookAndFeel.get());

    core = std::make_unique<midi::Core>();
    core->init();
    mainWindow = std::make_unique<MainWindow>(getApplicationName(), *core,
                                              *applicationLookAndFeel);

    auto midiFile = parseMidiFileFromCommandLine(commandLine);
    if (midiFile.existsAsFile()) {
      if (mainWindow != nullptr) {
        auto *content = dynamic_cast<MainContentComponent *>(
            mainWindow->getContentComponent());
        if (content)
          content->setPendingShellOpen(true);
      }

      juce::Timer::callAfterDelay(400, [this, midiFile]() {
        if (mainWindow != nullptr) {
          auto *content = dynamic_cast<MainContentComponent *>(
              mainWindow->getContentComponent());
          if (content)
            content->openMidiFileFromShell(midiFile);
        }
      });
    }
  }

  void shutdown() override {
    LOG_DEBUG("Application Shutting Down...");
    mainWindow = nullptr;
    core = nullptr;
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
    applicationLookAndFeel = nullptr;
    DebugLogger::shutdown();
  }

  void systemRequestedQuit() override { quit(); }

  void anotherInstanceStarted(const juce::String &commandLine) override {
    auto midiFile = parseMidiFileFromCommandLine(commandLine);

    if (mainWindow != nullptr) {
      mainWindow->toFront(true);

      if (midiFile.existsAsFile()) {
        auto *content = dynamic_cast<MainContentComponent *>(
            mainWindow->getContentComponent());
        if (content)
          content->openMidiFileFromShell(midiFile);
      }
    }
  }

  class MainWindow : public juce::DocumentWindow {
  public:
    MainWindow(const juce::String &name, midi::Core &core,
               FluentLookAndFeel &lookAndFeel)
        : DocumentWindow(
              name,
              juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                  juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      setContentOwned(new MainContentComponent(core, lookAndFeel), true);

#if JUCE_IOS || JUCE_ANDROID
      setFullScreen(true);
#else
      setResizable(true, true);

      setResizeLimits(800, 600, 32767, 32767);

      auto &settings = getAppSettings();
      if (settings.getRememberWindowBounds()) {
        auto bounds = settings.getWindowBounds();
        if (!bounds.isEmpty()) {
          auto &displays = juce::Desktop::getInstance().getDisplays();
          auto *display = displays.getDisplayForRect(bounds);
          auto displayArea = display != nullptr
                                 ? display->userArea
                                 : displays.getTotalBounds(true);
          if (displayArea.intersects(bounds)) {
            bounds.setSize(juce::jlimit(800, displayArea.getWidth(),
                                        bounds.getWidth()),
                           juce::jlimit(600, displayArea.getHeight(),
                                        bounds.getHeight()));
            setBounds(bounds.constrainedWithin(displayArea));
          } else {
            centreWithSize(1100, 750);
          }
        } else {
          centreWithSize(1100, 750);
        }
      } else {
        centreWithSize(1100, 750);
      }
#endif

      setVisible(true);

      if (settings.getRememberWindowBounds() && settings.getWindowMaximized())
        setFullScreen(true);

      if (settings.getAlwaysOnTop())
        setAlwaysOnTop(true);
    }

    void closeButtonPressed() override {
      if (isClosing)
        return;

      if (getAppSettings().getRememberWindowBounds()) {
        getAppSettings().setWindowMaximized(isFullScreen());
        if (!isFullScreen())
          getAppSettings().setWindowBounds(getBounds());
        getAppSettings().save();
      }

      auto *content =
          dynamic_cast<MainContentComponent *>(getContentComponent());
      if (content && content->hasUnsavedChanges()) {
        isClosing = true;

        juce::String summary = content->getPlaylistChangeSummary();
        juce::String message = L"播放列表有以下未保存的更改：\n\n" + summary +
                               L"\n\n是否保存更改？";

        int result = juce::AlertWindow::showYesNoCancelBox(
            juce::AlertWindow::QuestionIcon, L"未保存的更改", message,
            L"保存并退出", L"不保存", L"取消", this);

        if (result == 1) {
          if (content->savePlaylist()) {
            JUCEApplication::getInstance()->systemRequestedQuit();
          } else {
            isClosing = false;
          }
        } else if (result == 2) {
          JUCEApplication::getInstance()->systemRequestedQuit();
        } else {
          isClosing = false;
        }
      } else {
        JUCEApplication::getInstance()->systemRequestedQuit();
      }
    }

  private:
    bool isClosing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
  };

private:
  std::unique_ptr<FluentLookAndFeel> applicationLookAndFeel;
  std::unique_ptr<midi::Core> core;
  std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(ModernMidiPlayerApplication)
