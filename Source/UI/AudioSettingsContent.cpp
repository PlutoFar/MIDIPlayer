#include "AudioSettingsContent.h"

AudioSettingsContent::AudioSettingsContent(midi::Core &c,
                                           FluentLookAndFeel &laf)
    : core(c), fluentLookAndFeel(laf), tooltipOverlay(laf) {
  setLookAndFeel(&fluentLookAndFeel);
  setOpaque(false);

  addAndMakeVisible(sectionLabel);
  sectionLabel.setText(L"输出设备", juce::dontSendNotification);
  FluentSettingsStyle::configureLabel(sectionLabel, fluentLookAndFeel, true);

  configureField(driverLabel, L"驱动类型", driverBox);
  configureField(deviceLabel, L"输出设备", deviceBox);
  configureField(sampleRateLabel, L"采样率", sampleRateBox);
  configureField(bufferSizeLabel, L"缓冲大小", bufferSizeBox);
  configureField(channelPairLabel, L"输出通道", channelPairBox);

  addAndMakeVisible(statusTitleLabel);
  addAndMakeVisible(statusDetailLabel);
  FluentSettingsStyle::configureLabel(statusTitleLabel, fluentLookAndFeel,
                                      true);
  FluentSettingsStyle::configureLabel(statusDetailLabel, fluentLookAndFeel,
                                      false, true);
  statusDetailLabel.setFont(fluentLookAndFeel.getCaptionFont());
  statusTitleLabel.setJustificationType(juce::Justification::centredLeft);
  statusDetailLabel.setJustificationType(juce::Justification::centredLeft);

  addAndMakeVisible(controlPanelButton);
  controlPanelButton.setButtonText(L"设备控制面板");
  controlPanelButton.onClick = [this]() {
    beginDeviceChange([this] { return core.showAudioControlPanel(); }, false);
  };

  addAndMakeVisible(testButton);
  testButton.setButtonText(L"播放测试音");
  testButton.onClick = [this]() { core.playTestSound(); };

  driverBox.onChange = [this]() {
    if (refreshing || deviceChangePending)
      return;
    const int index = driverBox.getSelectedItemIndex();
    const auto &types = deviceState.driverTypes;
    if (juce::isPositiveAndBelow(index, types.size())) {
      const auto typeName = types[index];
      beginDeviceChange(
          [this, typeName]() { return core.setAudioDriver(typeName); }, true);
    }
  };

  deviceBox.onChange = [this]() {
    if (refreshing || deviceChangePending)
      return;
    const int index = deviceBox.getSelectedItemIndex();
    if (!juce::isPositiveAndBelow(index, outputDevices.size()))
      return;

    auto setup = deviceState.setup;
    setup.outputDeviceName = outputDevices[index];
    setup.useDefaultOutputChannels = true;
    if (!deviceState.separateInputsAndOutputs)
      setup.inputDeviceName = setup.outputDeviceName;
    applySetup(setup);
  };

  sampleRateBox.onChange = [this]() {
    if (refreshing || deviceChangePending)
      return;
    const int index = sampleRateBox.getSelectedItemIndex();
    if (!juce::isPositiveAndBelow(index, sampleRates.size()))
      return;
    auto setup = deviceState.setup;
    setup.sampleRate = sampleRates.getUnchecked(index);
    applySetup(setup);
  };

  bufferSizeBox.onChange = [this]() {
    if (refreshing || deviceChangePending)
      return;
    const int index = bufferSizeBox.getSelectedItemIndex();
    if (!juce::isPositiveAndBelow(index, bufferSizes.size()))
      return;
    auto setup = deviceState.setup;
    setup.bufferSize = bufferSizes.getUnchecked(index);
    applySetup(setup);
  };

  channelPairBox.onChange = [this]() {
    if (refreshing || deviceChangePending)
      return;
    const int index = channelPairBox.getSelectedItemIndex();
    if (!juce::isPositiveAndBelow(index, channelStarts.size()))
      return;
    auto setup = deviceState.setup;
    setup.useDefaultOutputChannels = false;
    setup.outputChannels = makeStereoOutputMask(
        outputChannelCount, channelStarts.getUnchecked(index));
    applySetup(setup);
  };

  core.addAudioDeviceListener(this);
  refreshControls(true);
  setSize(660, getPreferredHeight());

  addChildComponent(tooltipOverlay);
  addMouseListener(&tooltipOverlay, true);
}

AudioSettingsContent::~AudioSettingsContent() {
  removeMouseListener(&tooltipOverlay);
  tooltipOverlay.hideTooltip();
  stopTimer();
  core.removeAudioDeviceListener(this);
  setLookAndFeel(nullptr);
}

void AudioSettingsContent::paint(juce::Graphics &g) {
  FluentSettingsStyle::paintPanel(g, fluentLookAndFeel, getLocalBounds());
  FluentSettingsStyle::paintCard(g, fluentLookAndFeel, deviceCardBounds);
  FluentSettingsStyle::paintCard(g, fluentLookAndFeel, statusCardBounds);

  const auto &colors = fluentLookAndFeel.getColors();
  const auto phase = statusAnimation.getPhase();
  if (phase == AudioStatusAnimation::Phase::busy) {
    const auto indicator = statusIndicator.toFloat().expanded(2.0f);
    g.setColour(colors.accentPrimary.withAlpha(0.18f));
    g.drawEllipse(indicator, 2.0f);

    const float start =
        statusAnimation.getBusyRotation() * juce::MathConstants<float>::twoPi;
    juce::Path spinner;
    spinner.addCentredArc(indicator.getCentreX(), indicator.getCentreY(),
                          indicator.getWidth() * 0.5f,
                          indicator.getHeight() * 0.5f, 0.0f, start,
                          start + juce::MathConstants<float>::pi * 1.35f, true);
    g.setColour(colors.accentPrimary);
    g.strokePath(spinner,
                 juce::PathStrokeType(2.0f, juce::PathStrokeType::curved,
                                      juce::PathStrokeType::rounded));
  } else {
    const auto indicatorColour =
        lastError.isNotEmpty()
            ? juce::Colour(0xffd95555)
            : (deviceState.hasDevice ? juce::Colour(0xff61c454)
                                     : colors.textDisabled);
    const float glow = statusAnimation.getSuccessGlow();
    if (glow > 0.0f) {
      g.setColour(indicatorColour.withAlpha(glow * 0.28f));
      g.fillEllipse(statusIndicator.toFloat().expanded(7.0f * glow));
    }
    g.setColour(indicatorColour);
    g.fillEllipse(statusIndicator.toFloat());
  }
}

void AudioSettingsContent::resized() {
  auto area = getLocalBounds().reduced(FluentSettingsStyle::panelMargin);
  deviceCardBounds = area.removeFromTop(deviceCardHeight);
  area.removeFromTop(cardGap);
  statusCardBounds = area;

  auto content = deviceCardBounds.reduced(FluentSettingsStyle::cardPadding);
  sectionLabel.setBounds(content.removeFromTop(24));
  content.removeFromTop(12);

  layoutField(content.removeFromTop(34), driverLabel, driverBox);
  content.removeFromTop(10);
  layoutField(content.removeFromTop(34), deviceLabel, deviceBox);
  content.removeFromTop(10);

  auto timingRow = content.removeFromTop(34);
  auto sampleArea = timingRow.removeFromLeft(timingRow.getWidth() / 2 - 6);
  timingRow.removeFromLeft(12);
  layoutField(sampleArea, sampleRateLabel, sampleRateBox);
  layoutField(timingRow, bufferSizeLabel, bufferSizeBox);
  content.removeFromTop(10);
  layoutField(content.removeFromTop(34), channelPairLabel, channelPairBox);

  auto status = statusCardBounds.reduced(FluentSettingsStyle::cardPadding,
                                         statusVerticalPadding);

  auto buttons = status;
  auto testBounds = buttons.removeFromRight(112);
  testBounds = testBounds.withSizeKeepingCentre(112, statusButtonHeight);
  testButton.setBounds(testBounds);
  buttons.removeFromRight(8);
  if (controlPanelButton.isVisible()) {
    auto controlBounds = buttons.removeFromRight(126);
    controlBounds =
        controlBounds.withSizeKeepingCentre(126, statusButtonHeight);
    controlPanelButton.setBounds(controlBounds);
    buttons.removeFromRight(8);
  } else {
    controlPanelButton.setBounds({});
  }

  auto statusText = buttons;
  statusText.removeFromLeft(22);
  const int titleHeight = getStatusTitleHeight();
  const int detailHeight = getStatusDetailHeight();
  const int textBlockHeight = titleHeight + statusTextGap + detailHeight;
  statusText.setY(status.getCentreY() - textBlockHeight / 2);
  statusText.setHeight(textBlockHeight);

  statusIndicator = juce::Rectangle<int>(
      status.getX(), statusText.getCentreY() - statusIndicatorSize / 2,
      statusIndicatorSize, statusIndicatorSize);
  statusTitleLabel.setBounds(statusText.removeFromTop(titleHeight));
  statusText.removeFromTop(statusTextGap);
  statusDetailLabel.setBounds(statusText.removeFromTop(detailHeight));
}

int AudioSettingsContent::getStatusTitleHeight() const {
  return LegacyDesignTokens::Typography::lineHeight(
      LegacyDesignTokens::Typography::body, fluentLookAndFeel.getUIFontSize());
}

int AudioSettingsContent::getStatusDetailHeight() const {
  return LegacyDesignTokens::Typography::lineHeight(
             LegacyDesignTokens::Typography::caption,
             fluentLookAndFeel.getUIFontSize()) +
         statusDetailVerticalSafetyPadding;
}

int AudioSettingsContent::getPreferredHeight() const {
  const int statusTextHeight =
      getStatusTitleHeight() + statusTextGap + getStatusDetailHeight();
  const int statusCardHeight =
      juce::jmax(statusButtonHeight, statusTextHeight) +
      statusVerticalPadding * 2;
  return juce::jmax(350, FluentSettingsStyle::panelMargin * 2 +
                             deviceCardHeight + cardGap + statusCardHeight);
}

void AudioSettingsContent::configureField(juce::Label &label,
                                          const juce::String &text,
                                          juce::ComboBox &box) {
  addAndMakeVisible(label);
  label.setText(text, juce::dontSendNotification);
  FluentSettingsStyle::configureLabel(label, fluentLookAndFeel);

  addAndMakeVisible(box);
  box.setJustificationType(juce::Justification::centredLeft);
  box.setTextWhenNothingSelected(L"请选择");
  box.setTextWhenNoChoicesAvailable(L"无可用选项");
}

void AudioSettingsContent::layoutField(juce::Rectangle<int> area,
                                       juce::Label &label, juce::ComboBox &box,
                                       int labelWidth) {
  label.setBounds(area.removeFromLeft(labelWidth));
  area.removeFromLeft(10);
  box.setBounds(area);
}

void AudioSettingsContent::applySetup(
    const juce::AudioDeviceManager::AudioDeviceSetup &setup) {
  beginDeviceChange(
      [this, setup]() { return core.configureAudioDevice(setup); }, false);
}

void AudioSettingsContent::changeListenerCallback(juce::ChangeBroadcaster *) {
  refreshControls(false);
}

void AudioSettingsContent::beginDeviceChange(
    std::function<juce::String()> operation, bool rescanDevices) {
  if (deviceChangePending)
    return;

  deviceChangePending = true;
  statusAnimation.beginBusy();
  statusTitleLabel.setText(L"正在切换音频设备", juce::dontSendNotification);
  statusDetailLabel.setText(L"正在重新初始化音频引擎",
                            juce::dontSendNotification);
  updateControlEnabledState();
  startTimerHz(60);
  repaint(statusCardBounds);

  auto safeThis = juce::Component::SafePointer<AudioSettingsContent>(this);
  juce::Timer::callAfterDelay(45, [safeThis, operation = std::move(operation),
                                   rescanDevices]() mutable {
    if (safeThis == nullptr)
      return;

    const auto error = operation();
    if (safeThis != nullptr)
      safeThis->finishDeviceChange(error, rescanDevices);
  });
}

void AudioSettingsContent::finishDeviceChange(const juce::String &error,
                                              bool rescanDevices) {
  lastError = error;
  deviceChangePending = false;
  refreshControls(rescanDevices);
  statusAnimation.complete(lastError.isEmpty());
  startTimerHz(60);
  repaint(statusCardBounds);
}

void AudioSettingsContent::timerCallback() {
  const bool active = statusAnimation.tick();
  const auto transform = juce::AffineTransform::translation(
      statusAnimation.getShakeOffset(), 0.0f);
  statusTitleLabel.setTransform(transform);
  statusDetailLabel.setTransform(transform);
  repaint(statusCardBounds);

  if (!active) {
    statusTitleLabel.setTransform(juce::AffineTransform());
    statusDetailLabel.setTransform(juce::AffineTransform());
    stopTimer();
  }
}

void AudioSettingsContent::refreshControls(bool rescanDevices) {
  juce::ScopedValueSetter<bool> guard(refreshing, true);
  deviceState = core.audioDeviceState(rescanDevices);

  const auto &types = deviceState.driverTypes;
  driverBox.clear(juce::dontSendNotification);
  int selectedDriver = 0;
  for (int i = 0; i < types.size(); ++i) {
    driverBox.addItem(types[i], i + 1);
    if (types[i] == deviceState.currentDriver)
      selectedDriver = i + 1;
  }
  driverBox.setSelectedId(selectedDriver, juce::dontSendNotification);

  outputDevices = deviceState.outputDevices;
  deviceBox.clear(juce::dontSendNotification);
  for (int i = 0; i < outputDevices.size(); ++i)
    deviceBox.addItem(outputDevices[i], i + 1);

  const auto setup = deviceState.setup;
  const int selectedDevice = outputDevices.indexOf(setup.outputDeviceName);
  deviceBox.setSelectedId(selectedDevice >= 0 ? selectedDevice + 1 : 0,
                          juce::dontSendNotification);

  sampleRates.clear();
  bufferSizes.clear();
  channelStarts.clear();
  sampleRateBox.clear(juce::dontSendNotification);
  bufferSizeBox.clear(juce::dontSendNotification);
  channelPairBox.clear(juce::dontSendNotification);

  outputChannelCount = 0;
  if (deviceState.hasDevice) {
    sampleRates = deviceState.sampleRates;
    for (int i = 0; i < sampleRates.size(); ++i)
      sampleRateBox.addItem(formatAudioSampleRate(sampleRates.getUnchecked(i)),
                            i + 1);
    sampleRateBox.setText(formatAudioSampleRate(deviceState.sampleRate),
                          juce::dontSendNotification);

    bufferSizes = deviceState.bufferSizes;
    for (int i = 0; i < bufferSizes.size(); ++i)
      bufferSizeBox.addItem(formatAudioBufferSize(bufferSizes.getUnchecked(i),
                                                  deviceState.sampleRate),
                            i + 1);
    bufferSizeBox.setText(
        formatAudioBufferSize(deviceState.bufferSize, deviceState.sampleRate),
        juce::dontSendNotification);

    const auto channelNames = deviceState.channelNames;
    outputChannelCount = channelNames.size();
    const auto activeChannels = deviceState.activeChannels;
    const int activeFirst = activeChannels.findNextSetBit(0);

    if (outputChannelCount == 1) {
      channelStarts.add(0);
      channelPairBox.addItem("1  " + channelNames[0], 1);
    } else {
      for (int first = 0; first + 1 < outputChannelCount; first += 2) {
        channelStarts.add(first);
        channelPairBox.addItem(
            juce::String(first + 1) + "-" + juce::String(first + 2) + "  " +
                channelNames[first] + " / " + channelNames[first + 1],
            channelStarts.size());
      }
    }

    int selectedPair = 0;
    for (int i = 0; i < channelStarts.size(); ++i) {
      const int first = channelStarts.getUnchecked(i);
      if (activeFirst == first ||
          (activeFirst >= first &&
           activeFirst <= first + (outputChannelCount > 1 ? 1 : 0))) {
        selectedPair = i + 1;
        break;
      }
    }
    channelPairBox.setSelectedId(selectedPair, juce::dontSendNotification);

    if (!deviceChangePending)
      statusTitleLabel.setText(L"设备已连接", juce::dontSendNotification);
    const auto latency = formatAudioBufferSize(deviceState.outputLatency,
                                               deviceState.sampleRate);
    if (!deviceChangePending)
      statusDetailLabel.setText(juce::String(deviceState.bitDepth) +
                                    L"-bit \u00B7 输出延迟 " + latency,
                                juce::dontSendNotification);
    const auto audioState = core.state().audio;
    statusDetailLabel.setTooltip(deviceState.deviceName + L" \u00B7 " +
                                 formatAudioSampleRate(deviceState.sampleRate) +
                                 L" \u00B7 " +
                                 juce::String(deviceState.bitDepth) +
                                 L"-bit \u00B7 输出延迟 " + latency +
                                 L"\n附加播放延迟上限 " +
                                 formatAudioBufferSize(audioState.renderLatencySamples,
                                                       deviceState.sampleRate) +
                                 L"\n缓冲欠载 " + juce::String(audioState.underrunCount));
  } else if (!deviceChangePending) {
    statusTitleLabel.setText(lastError.isNotEmpty() ? L"设备切换失败"
                                                    : L"未连接输出设备",
                             juce::dontSendNotification);
    statusDetailLabel.setText(
        lastError.isNotEmpty() ? lastError : L"请选择驱动类型和输出设备",
        juce::dontSendNotification);
  }

  updateControlEnabledState();
  controlPanelButton.setVisible(deviceState.hasDevice &&
                                deviceState.hasControlPanel);
  repaint();
}

void AudioSettingsContent::updateControlEnabledState() {
  const bool canInteract = !deviceChangePending;
  driverBox.setEnabled(canInteract);
  deviceBox.setEnabled(canInteract && deviceState.hasDriver &&
                       !outputDevices.isEmpty());
  sampleRateBox.setEnabled(canInteract && deviceState.hasDevice &&
                           !sampleRates.isEmpty());
  bufferSizeBox.setEnabled(canInteract && deviceState.hasDevice &&
                           !bufferSizes.isEmpty());
  channelPairBox.setEnabled(canInteract && deviceState.hasDevice &&
                            !channelStarts.isEmpty());
  testButton.setEnabled(canInteract && deviceState.hasDevice);
  controlPanelButton.setEnabled(canInteract && deviceState.hasDevice);
}
