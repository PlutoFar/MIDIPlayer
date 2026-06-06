#include "AudioEngine/AudioEngine.h"
#include "UI/MainContentComponent.h"
#include "Utils/DebugLogger.h"
#include <juce_gui_extra/juce_gui_extra.h>

class ModernMidiPlayerApplication : public juce::JUCEApplication {
public:
  ModernMidiPlayerApplication() = default;

  const juce::String getApplicationName() override { return L"MIDI 播放器"; }

  const juce::String getApplicationVersion() override { return "1.0.0"; }

  bool moreThanOneInstanceAllowed() override { return false; }

  void initialise(const juce::String &commandLine) override {
    DebugLogger::init();

    engine = std::make_unique<AudioEngine>();
    mainWindow = std::make_unique<MainWindow>(getApplicationName(), *engine);

    auto midiFile = parseMidiFileFromCommandLine(commandLine);
    if (midiFile.existsAsFile()) {
      juce::Component::SafePointer<MainContentComponent> safeContent;
      if (mainWindow != nullptr) {
        auto *content = dynamic_cast<MainContentComponent *>(
            mainWindow->getContentComponent());
        if (content) {
          content->setPendingShellOpen(true);
          safeContent = content;
        }
      }

      juce::Timer::callAfterDelay(400, [safeContent, midiFile]() {
        if (safeContent != nullptr)
          safeContent->openMidiFileFromShell(midiFile);
      });
    }
  }

  void shutdown() override {
    mainWindow = nullptr;
    engine = nullptr;
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
    MainWindow(const juce::String &name, AudioEngine &audioEngine)
        : DocumentWindow(
              name,
              juce::Desktop::getInstance().getDefaultLookAndFeel().findColour(
                  juce::ResizableWindow::backgroundColourId),
              DocumentWindow::allButtons) {
      setUsingNativeTitleBar(true);
      setContentOwned(new MainContentComponent(audioEngine), true);

#if JUCE_IOS || JUCE_ANDROID
      setFullScreen(true);
#else
      setResizable(true, true);

      setResizeLimits(800, 600, 1920, 1200);

      auto &settings = getAppSettings();
      if (settings.getRememberWindowBounds()) {
        auto bounds = settings.getWindowBounds();
        if (!bounds.isEmpty()) {
          auto displayArea =
              juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
          if (displayArea.intersects(bounds)) {
            setBounds(bounds);
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

      if (settings.getAlwaysOnTop())
        setAlwaysOnTop(true);
    }

    void closeButtonPressed() override {
      if (isClosing)
        return;

      if (getAppSettings().getRememberWindowBounds()) {
        getAppSettings().setWindowBounds(getBounds());
        getAppSettings().save();
      }

      auto *content =
          dynamic_cast<MainContentComponent *>(getContentComponent());
      if (content && content->hasUnsavedChanges()) {
        isClosing = true;

        auto message = L"播放列表有以下未保存的更改：\n\n" +
                       content->getPlaylistChangeSummary() +
                       L"\n\n是否保存更改？";
        auto safeWindow = juce::Component::SafePointer<MainWindow>(this);
        juce::AlertWindow::showYesNoCancelBox(
            juce::AlertWindow::QuestionIcon, L"未保存的更改", message,
            L"保存并退出", L"不保存", L"取消", this,
            juce::ModalCallbackFunction::create(
                [safeWindow](int result) {
                  if (safeWindow == nullptr)
                    return;

                  if (result == 1) {
                    auto *mainContent =
                        dynamic_cast<MainContentComponent *>(
                            safeWindow->getContentComponent());
                    if (mainContent == nullptr) {
                      safeWindow->isClosing = false;
                      return;
                    }

                    mainContent->savePlaylist([safeWindow](bool saved) {
                      if (safeWindow == nullptr)
                        return;

                      if (saved)
                        JUCEApplication::getInstance()->systemRequestedQuit();
                      else
                        safeWindow->isClosing = false;
                    });
                  } else if (result == 2) {
                    JUCEApplication::getInstance()->systemRequestedQuit();
                  } else {
                    safeWindow->isClosing = false;
                  }
                }));
      } else {
        JUCEApplication::getInstance()->systemRequestedQuit();
      }
    }

  private:
    bool isClosing = false;
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
  };

private:
  juce::File parseMidiFileFromCommandLine(const juce::String &commandLine) {
    auto trimmed = commandLine.trim();
    if (trimmed.isEmpty())
      return {};

    if (trimmed.startsWithChar('"') && trimmed.endsWithChar('"'))
      trimmed = trimmed.substring(1, trimmed.length() - 1).trim();

    if (trimmed.isEmpty())
      return {};

    juce::File file(trimmed);
    if (!file.existsAsFile())
      return {};

    auto ext = file.getFileExtension().toLowerCase();
    if (ext == ".mid" || ext == ".midi")
      return file;

    return {};
  }

  std::unique_ptr<AudioEngine> engine;
  std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(ModernMidiPlayerApplication)
