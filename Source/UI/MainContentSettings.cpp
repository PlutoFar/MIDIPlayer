#include "AudioSettingsContent.h"
#include "FontSettingsContent.h"
#include "MainContentComponent.h"

void MainContentComponent::showAudioSettings() {
  audioSettingsWindow.deleteAndZero();
  auto *content = new AudioSettingsContent(core, fluentLookAndFeel);

  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(content);
  options.dialogTitle = L"音频设置";
  options.dialogBackgroundColour = fluentLookAndFeel.getColors().cardBackground;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = false;
  options.resizable = false;
  options.componentToCentreAround = this;
  audioSettingsWindow = FluentSettingsStyle::launchDialogAsync(options);
}

void MainContentComponent::showBackgroundSettings() {
  backgroundSettingsWindow.deleteAndZero();
  auto *content =
      new BackgroundSettingsDialog(background, this, fluentLookAndFeel);
  content->setLookAndFeel(&fluentLookAndFeel);

  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(content);
  options.dialogTitle = L"背景设置";
  options.dialogBackgroundColour = fluentLookAndFeel.getColors().cardBackground;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = false;
  options.resizable = false;
  options.componentToCentreAround = this;
  backgroundSettingsWindow = FluentSettingsStyle::launchDialogAsync(options);
}

void MainContentComponent::showFontSettings() {
  fontSettingsWindow.deleteAndZero();
  auto *content = new FontSettingsContent(fluentLookAndFeel);
  content->setLookAndFeel(&fluentLookAndFeel);
  content->onSettingsChanged =
      [safeThis = juce::Component::SafePointer<MainContentComponent>(this)]() {
        if (safeThis == nullptr)
          return;

        safeThis->applyConfiguredFonts();
        safeThis->playlistPanel.refresh();
        safeThis->playlistPanel.repaint();
        safeThis->navigation.resized();
        safeThis->navigation.repaint();
        safeThis->resized();
        safeThis->repaint();
      };

  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(content);
  options.dialogTitle = L"字体设置";
  options.dialogBackgroundColour = fluentLookAndFeel.getColors().cardBackground;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = false;
  options.resizable = false;
  options.componentToCentreAround = this;
  fontSettingsWindow = FluentSettingsStyle::launchDialogAsync(options);
}

void MainContentComponent::applyConfiguredFonts() {
  pageTitle.setFont(fluentLookAndFeel.getTitleFont());
  contentLabel.setFont(fluentLookAndFeel.getBodyLargeFont());
  trackLabel.setFont(fluentLookAndFeel.getBodyFont(true));
  timeLabel.setFont(fluentLookAndFeel.getBodyFont());
}

void MainContentComponent::closeSettingsWindows() {
  audioSettingsWindow.deleteAndZero();
  backgroundSettingsWindow.deleteAndZero();
  fontSettingsWindow.deleteAndZero();
}

void MainContentComponent::refreshDialogMaterials(bool backdropChanged) {
  auto apply = [backdropChanged](
                   juce::Component::SafePointer<juce::DialogWindow> window) {
    if (window == nullptr)
      return;
    if (backdropChanged)
      FluentSettingsStyle::refreshDialogMaterial(window.getComponent());
    else
      FluentSettingsStyle::refreshDialogSurface(window.getComponent());
  };
  apply(audioSettingsWindow);
  apply(backgroundSettingsWindow);
  apply(fontSettingsWindow);
}

void MainContentComponent::loadSettings() {
  auto &settings = getAppSettings();
  volumeSlider.setValue(settings.getMasterVolume(), juce::dontSendNotification);
  core.volume(volumeLevelToGain((float)volumeSlider.getValue()));

  int savedMode = settings.getPlayMode();
  core.setPlayMode(savedMode);

  fluentLookAndFeel.setUIFont(settings.getUIFontName());
  fluentLookAndFeel.setUIFontSize(settings.getLegacyUIFontSize());
  fluentLookAndFeel.setPlaylistFont(settings.getPlaylistFontName());

  playlistPanel.refresh();
}

juce::Result MainContentComponent::saveSettings() {
  getAppSettings().setMasterVolume((float)volumeSlider.getValue());
  getAppSettings().setPlayMode(core.state().playlist.playMode);
  return getAppSettings().saveDetailed();
}
