#pragma once

#include "../AudioEngine/ExportFileSupport.h"
#include "../AudioEngine/ExportSettings.h"
#include "../Core/Core.h"
#include "../Core/PluginLoadNotification.h"
#include "../Utils/UserSettings.h"
#include "../Utils/Win11Helpers.h"
#include "../Utils/WindowsFileAssociation.h"
#include "BackgroundComponent.h"
#include "CustomControls.h"
#include "CustomLookAndFeel.h"
#include "FluentSettingsStyle.h"
#include "IconAssets.h"
#include "TransportWidgets.h"
#include "NavigationSidebar.h"
#include "PlaylistPanel.h"
#include "PluginWindowLifecycle.h"

#include <cmath>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

// Responsibilities: 播放主界面、对话框所有权及用户操作编排；业务状态由 `Core` 持有。
// Ownership: 借用 `Core` 和应用外观，两者必须覆盖本组件及其对话框寿命。
// Concurrency: 公开接口、JUCE 回调和监听器入口在消息线程执行；异步结果使用 `SafePointer`。
// Ordering: 析构停止界面回调、取消插件操作并关闭对话框，之后宿主才销毁 `Core`。
class MainContentComponent : public juce::Component,
                             public juce::Button::Listener,
                             public juce::ComboBox::Listener,
                             public juce::Slider::Listener,
                             public juce::FileDragAndDropTarget,
                             public juce::DragAndDropContainer,
                             public juce::Timer,
                             public PlaylistPanel::Listener,
                             public NavigationSidebar::Listener,
                             public BackgroundSettingsDialog::Listener,
                             public juce::AsyncUpdater {
public:
  MainContentComponent(midi::Core &c,
                       FluentLookAndFeel &applicationLookAndFeel);

  ~MainContentComponent() override;

  // Preconditions: 宿主在消息线程、窗口销毁前调用；失败诊断由退出流程处理。
  // Postconditions: 保存当前音量、播放模式及已修改的用户配置。
  [[nodiscard]] juce::Result saveSettings();

  void timerCallback() override;

  void paint(juce::Graphics &g) override;

  void paintOverChildren(juce::Graphics &g) override;

  void resized() override;

  void handleAsyncUpdate() override;

  void layoutTransportBar(juce::Rectangle<int> area);

  void navigationItemSelected(const juce::String &itemId) override;

  void navigationPinToggled(bool isPinned) override;

  void navigationBackgroundClicked() override;

  void buttonClicked(juce::Button *b) override;

  void toggleLoopMode();

  void toggleMute();

  void updateLoopButtonTooltip();

  void sliderValueChanged(juce::Slider *s) override;

  void sliderDragStarted(juce::Slider *s) override;

  void sliderDragEnded(juce::Slider *s) override;

  void triggerSeekUpdate(double normalizedPos);

  void comboBoxChanged(juce::ComboBox *c) override;

  void playlistTrackSelected(int index) override;

  void playlistLoaded(const juce::File &playlistFile) override;

  bool playlistSaveRequested() override;

  bool playlistClearRequested() override;

  bool playlistLoadRequested(const juce::File &playlistFile) override;

  bool playlistTrackMoveRequested(int fromIndex, int toIndex,
                                  int newCurrentIndex) override;

  bool playlistTrackRemoveRequested(int index, int newCurrentIndex) override;

  void playlistTrackRevealRequested(int index) override;

  void playlistTrackReordered(int newCurrentIndex) override;

  void playlistTrackDoubleClicked(int index);

  void playlistFilesDropped(const juce::StringArray &files) override;

  bool isInterestedInFileDrag(const juce::StringArray &files) override;

  void filesDropped(const juce::StringArray &files, int, int) override;

  void backgroundSettingsChanged(bool reapplyEffects) override;

  void dialogMaterialChanged(bool backdropChanged) override;

  void backgroundSettingsClosed() override;

  void onAccentColorChanged(juce::Colour newColor);

  bool keyPressed(const juce::KeyPress &key) override;

  void fileDragEnter(const juce::StringArray &, int, int) override;

  void fileDragExit(const juce::StringArray &) override;

  void mouseMove(const juce::MouseEvent &e) override;

  void mouseDrag(const juce::MouseEvent &e) override;

  void mouseExit(const juce::MouseEvent &e) override;

  void mouseUp(const juce::MouseEvent &e) override;

  void mouseDown(const juce::MouseEvent &e) override;

private:
  bool getProgressHoverInfo(const juce::MouseEvent &e, juce::String &text,
                            int &anchorX);

  void updateProgressTimeTooltip(const juce::MouseEvent &e);

  void updateVolumeTooltip();

  void showExportDialog();

  void chooseExportTarget(int selectedTrackIndex,
                          const ExportSettings &settings);

  void performExport(int selectedTrackIndex, const ExportSettings &settings,
                     const juce::File &targetFile);

  void runLater(int delayMs, std::function<void(MainContentComponent &)> fn);

  void setupIconButton(juce::Button &btn, const juce::String &,
                       const juce::String &tooltip);

  void drawIconButton(juce::Graphics &g, juce::Button &btn,
                      const juce::String &icon,
                      float iconSize = DesignTokens::Icon::toolbar);

  void drawIconButtonCombined(juce::Graphics &g, juce::Button &btn,
                              const juce::String &mainIcon,
                              const juce::String &subIcon);

  void drawSequentialIcon(juce::Graphics &g, juce::Button &btn);

  void drawPlayButton(juce::Graphics &g, juce::Button &btn, bool isPlaying);

  void showPage(const juce::String &pageId, const juce::String &title);

  void startPluginScan();

  void confirmUnloadPlugin();

public:
  // Postconditions: 从核心读取当前列表的修改摘要；该检查不保存文件。
  bool hasUnsavedChanges() const;

  juce::String getPlaylistChangeSummary() const;

  // Side effect: 保存已有列表或同步选择目标；取消/文件错误返回 `false`，用于决定能否继续关闭窗口。
  bool savePlaylist();

  bool savePlaylistAs();

  // Preconditions: 文件已通过命令行入口筛选；核心仍验证 MIDI 内容，界面负责缺少插件时的交互。
  void openMidiFileFromShell(const juce::File &file);
  void setPendingShellOpen(bool pending);

private:
  void showOperationError(const juce::String &title,
                          const std::wstring &message);

  void unloadPlugin();

  int beginPluginSwitch();

  void finishPluginLoadUi(const midi::PluginInfo &plugin);

  void showPluginLoadSuccessToast(const juce::String &pluginName);

  void loadPluginInfo(const midi::PluginInfo &plugin, bool openEditorAfterLoad);

  void loadSelectedPlugin();

  void openPluginWindow();

  void closePluginWindow();

  void handlePluginWorkerCrash();

  void showPluginMessage(const juce::String &title,
                         const juce::String &message);

  void togglePlayPause();

  void stopPlayback();

  void playNextTrack();

  void playPreviousTrack();

  void showOpenFileDialog();

  void updateFileAssociation();
  void showFileAssociationPrompt();

  // Ordering: 自动加载完成后只按插件策略打开编辑器；首次音色选择后的播放由用户发起。
  void tryLoadLastPluginWithDialog();

  void showAudioSettings();

  void showBackgroundSettings();

  void showFontSettings();

  void applyConfiguredFonts();

  void closeSettingsWindows();

  void refreshDialogMaterials(bool backdropChanged);

  void updatePluginList();

  void tryLoadLastPlugin();

  juce::String formatTime(int seconds);

  void loadSettings();

  midi::Core &core;
  juce::String lastMidiErrorShown;
  FluentLookAndFeel &fluentLookAndFeel;

  BackgroundComponent background;
  NavigationSidebar navigation;

  juce::Label pageTitle;
  juce::ComboBox pluginSelector;
  TransparentButton loopModeBtn;
  TransparentButton exportBtn;
  VolumeSlider volumeSlider;
  TransparentButton scanBtn, unloadBtn, openPluginBtn;

  juce::Label contentLabel;
  PlaylistPanel playlistPanel;

  juce::Component transportBar;
  ScrollingLabel trackLabel;
  juce::Label timeLabel;
  juce::Slider progressSlider;
  ProgressTimeTooltip progressTimeTooltip;
  TransparentButton prevBtn, playBtn, nextBtn, stopBtn, volumeBtn;
  std::unique_ptr<juce::FileChooser> fileChooser;
  std::unique_ptr<juce::Drawable> sequentialIconDrawable;
  juce::Colour lastSequentialIconColor = juce::Colours::black;
  juce::String currentPage = "library";

  juce::Component::SafePointer<juce::DialogWindow> audioSettingsWindow;
  juce::Component::SafePointer<juce::DialogWindow> backgroundSettingsWindow;
  juce::Component::SafePointer<juce::DialogWindow> fontSettingsWindow;
  juce::Component::SafePointer<juce::DialogWindow> pluginMessageWindow;
  bool isUserDraggingProgress = false;
  bool isScanningPlugins = false;
  juce::Component::SafePointer<juce::DialogWindow> pluginLoadingWindow;
  bool pluginLoadInProgress = false;
  bool lastPlayingState = false;
  bool isDragOver = false;
  bool isMuted = false;
  double volumeBeforeMute = 1.0;
  float playbackModeAnimationScale = 1.0f;
  ToastComponent modeToast;
  EmbeddedTooltip tooltipOverlay;

  std::atomic<uint32_t> lastSeekRequestTime{0};
  bool playbackPausedByPluginSwitch = false;
  bool pluginWorkerCrashAlertShown = false;
  bool pluginWorkerRecoveryHandled = false;
  bool pendingShellOpen = false;
  PluginWindowLifecycle pluginLifecycle;

  juce::File currentPlaylistFile;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainContentComponent)
};
