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
#include "LegacyIconAssets.h"
#include "LegacyTransportWidgets.h"
#include "NavigationSidebar.h"
#include "PlaylistPanel.h"
#include "PluginWindowLifecycle.h"

#include <cmath>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

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
                      float iconSize = LegacyDesignTokens::Icon::toolbar);

  void drawIconButtonCombined(juce::Graphics &g, juce::Button &btn,
                              const juce::String &mainIcon,
                              const juce::String &subIcon);

  void drawSequentialIcon(juce::Graphics &g, juce::Button &btn);

  void drawPlayButton(juce::Graphics &g, juce::Button &btn, bool isPlaying);

  void showPage(const juce::String &pageId, const juce::String &title);

  void startPluginScan();

  void confirmUnloadPlugin();

public:
  bool hasUnsavedChanges() const;

  juce::String getPlaylistChangeSummary() const;

  bool savePlaylist();

  bool savePlaylistAs();

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

#if JUCE_WINDOWS
  bool isFileAssociatedToSelf();

  bool registerFileAssociation();

  void removeFileAssociation();
#endif // JUCE_WINDOWS

  std::unique_ptr<juce::ToggleButton> createDontShowAgainToggle();

  void showFileAssociationPrompt();

  // 自动插件加载只打开插件窗口，不自动播放；部分乐器插件需要先加载音色。
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

  void saveSettings();
  midi::Core &core;
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
