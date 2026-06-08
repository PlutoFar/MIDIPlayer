#include "AudioEngine/AudioEngine.h"
#include "UI/MainContentComponent.h"
#include "Utils/DebugLogger.h"
#include <juce_gui_extra/juce_gui_extra.h>

/**
    MIDI 播放器应用程序入口。

    使用 JUCE 框架构建的 Win11 风格 VST3i MIDI 播放器。
*/
class ModernMidiPlayerApplication : public juce::JUCEApplication {
public:
  ModernMidiPlayerApplication() = default;

  const juce::String getApplicationName() override { return L"MIDI 播放器"; }

  const juce::String getApplicationVersion() override { return "1.0.0"; }

  bool moreThanOneInstanceAllowed() override {
    return false; // 防止多实例运行
  }

  void initialise(const juce::String &commandLine) override {
    DebugLogger::init();
    LOG_DEBUG("Application Initialising...");

    // 创建引擎和主窗口
    engine = std::make_unique<AudioEngine>();
    mainWindow = std::make_unique<MainWindow>(getApplicationName(), *engine);

    // 解析命令行参数：检查是否传入了 MIDI 文件路径
    auto midiFile = parseMidiFileFromCommandLine(commandLine);
    if (midiFile.existsAsFile()) {
      // 提前标记：有文件关联打开挂起，抑制通用音频设备警告弹窗
      if (mainWindow != nullptr) {
        auto *content = dynamic_cast<MainContentComponent *>(
            mainWindow->getContentComponent());
        if (content)
          content->setPendingShellOpen(true);
      }

      // 延迟加载，等待窗口和插件初始化完成
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
    // 正确的关闭顺序：先关窗口，再释放引擎
    mainWindow = nullptr;
    engine = nullptr;
    DebugLogger::shutdown();
  }

  void systemRequestedQuit() override { quit(); }

  void anotherInstanceStarted(const juce::String &commandLine) override {
    // 解析命令行参数，检查是否传入了 MIDI 文件
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

  /**
      主窗口类 - Win11 风格的文档窗口
  */
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

      // 设置合理的窗口大小限制
      setResizeLimits(800, 600, 1920, 1200);

      // 窗口持久化逻辑：如果启用了该选项，则根据上次保存的坐标恢复窗口。
      auto &settings = getAppSettings();
      if (settings.getRememberWindowBounds()) {
        auto bounds = settings.getWindowBounds();
        if (!bounds.isEmpty()) {
          auto displayArea =
              juce::Desktop::getInstance().getDisplays().getTotalBounds(true);
          if (displayArea.intersects(bounds)) {
            bounds.setSize(juce::jlimit(800, 1920, bounds.getWidth()),
                           juce::jlimit(600, 1200, bounds.getHeight()));
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

      // 恢复窗口置顶状态
      if (settings.getAlwaysOnTop())
        setAlwaysOnTop(true);
    }

    void closeButtonPressed() override {
      if (isClosing)
        return;

      // 如果启用了记忆功能，在关闭前保存窗口位置。
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
            // 用户取消另存为或保存失败时留在窗口内。
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
  /**
      解析命令行参数，提取 MIDI 文件路径。
      处理带引号和不带引号的路径，支持含空格的路径。
  */
  juce::File parseMidiFileFromCommandLine(const juce::String &commandLine) {
    auto trimmed = commandLine.trim();
    if (trimmed.isEmpty())
      return {};

    // 去除两端引号（Shell 传入的路径通常带引号）
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
