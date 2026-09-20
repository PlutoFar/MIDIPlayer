#pragma once

#include "../Core/Core.h"
#include "AudioSettingsSupport.h"
#include "AudioStatusAnimation.h"
#include "CustomControls.h"
#include "FluentSettingsStyle.h"

struct AudioSettingsContent : public juce::Component,
                              private juce::ChangeListener,
                              private juce::Timer {
  AudioSettingsContent(midi::Core &c, FluentLookAndFeel &laf);

  ~AudioSettingsContent() override;

  void paint(juce::Graphics &g) override;

  void resized() override;

private:
  int getStatusTitleHeight() const;

  int getStatusDetailHeight() const;

  int getPreferredHeight() const;

  void configureField(juce::Label &label, const juce::String &text,
                      juce::ComboBox &box);

  static void layoutField(juce::Rectangle<int> area, juce::Label &label,
                          juce::ComboBox &box, int labelWidth = 82);

  void applySetup(const juce::AudioDeviceManager::AudioDeviceSetup &setup);

  void changeListenerCallback(juce::ChangeBroadcaster *) override;

  void beginDeviceChange(std::function<juce::String()> operation,
                         bool rescanDevices);

  void finishDeviceChange(const juce::String &error, bool rescanDevices);

  void timerCallback() override;

  void refreshControls(bool rescanDevices);

  void updateControlEnabledState();

  midi::Core &core;
  FluentLookAndFeel &fluentLookAndFeel;
  AudioDeviceState deviceState;

  juce::Label sectionLabel;
  juce::Label driverLabel, deviceLabel, sampleRateLabel, bufferSizeLabel;
  juce::Label channelPairLabel, statusTitleLabel, statusDetailLabel;
  juce::ComboBox driverBox, deviceBox, sampleRateBox, bufferSizeBox;
  juce::ComboBox channelPairBox;
  juce::TextButton controlPanelButton, testButton;

  juce::StringArray outputDevices;
  juce::Array<double> sampleRates;
  juce::Array<int> bufferSizes;
  juce::Array<int> channelStarts;
  juce::String lastError;
  int outputChannelCount = 0;
  bool refreshing = false;
  bool deviceChangePending = false;
  AudioStatusAnimation statusAnimation;

  static constexpr int deviceCardHeight = 234;
  static constexpr int cardGap = 12;
  static constexpr int statusVerticalPadding = 12;
  static constexpr int statusTextGap = 2;
  static constexpr int statusDetailVerticalSafetyPadding = 4;
  static constexpr int statusButtonHeight = 34;
  static constexpr int statusIndicatorSize = 10;

  juce::Rectangle<int> deviceCardBounds;
  juce::Rectangle<int> statusCardBounds;
  juce::Rectangle<int> statusIndicator;
  EmbeddedTooltip tooltipOverlay;
};
