#pragma once

#include "BackgroundComponent.h"
#include "BackgroundHistory.h"
#include "BackgroundHistorySupport.h"
#include "BackgroundScrollMotion.h"
#include "BackgroundSettingsListener.h"
#include "CustomControls.h"
#include "FluentSettingsStyle.h"
#include "ImageCropperComponent.h"
#include "ImagePreviewButton.h"
#include "PaletteSelector.h"
#include "SettingsPersistence.h"

// Responsibilities: 背景文件选择、裁剪、缓存和材质设置；缩略图任务持有独立输入。
// Ownership: 借用背景组件、外观和监听器，宿主保证其覆盖对话框寿命。
// Concurrency: 公开控件接口及监听器回调仅在消息线程执行；后台结果用安全组件引用交付。
class BackgroundSettingsDialog : public juce::Component,
                                 public juce::ChangeListener {
public:
  class VectorIconButton : public juce::Button {
  public:
    VectorIconButton(const juce::String &name, bool isLeft)
        : juce::Button(name), left(isLeft) {}
    void paintButton(juce::Graphics &g, bool isMouseOver,
                     bool isButtonDown) override {
      auto bounds = getLocalBounds().toFloat();
      auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel());
      const auto hoverColour =
          laf != nullptr ? laf->getColors().controlHover
                         : juce::Colours::white.withAlpha(0.05f);
      const auto iconColour =
          laf != nullptr ? laf->getColors().textPrimary
                         : juce::Colours::white;
      if (isMouseOver || isButtonDown) {
        g.setColour(isButtonDown ? hoverColour.brighter(0.1f) : hoverColour);
        g.fillRoundedRectangle(bounds, 6.0f);
      }

      g.setColour(isEnabled() ? iconColour : iconColour.withAlpha(0.3f));
      if (laf != nullptr)
        laf->drawSystemIconGlyph(g, left ? L"\uE76B" : L"\uE76C", bounds,
                                 DesignTokens::Icon::small);

      if (hasKeyboardFocus(false)) {
        g.setColour(iconColour);
        g.drawRoundedRectangle(bounds.reduced(1.5f), 6.0f, 1.5f);
      }
    }
    bool left;
  };

  /**
      HorizontalViewport: 内容超出宽度时，将纵向滚轮转换为横向滚动。
  */
  class HorizontalViewport : public juce::Viewport,
                             private juce::Timer {
  public:
    std::function<void()> onVisibleAreaChanged;

    HorizontalViewport() {
      setScrollBarsShown(false, false);
    }

    ~HorizontalViewport() override {
      stopTimer();
    }

    void setScrollBounds(int maximumPosition) {
      motion.setBounds(0.0f, (float)juce::jmax(0, maximumPosition));
      if (!isTimerRunning())
        motion.setPosition((float)juce::jlimit(
            0, juce::jmax(0, maximumPosition), getViewPositionX()));
    }

    void jumpTo(int position) {
      stopTimer();
      motion.setPosition((float)position);
      setViewPosition(juce::roundToInt(motion.getPosition()), 0);
    }

    void animateTo(int position) {
      motion.setPosition((float)getViewPositionX());
      motion.animateTo((float)position);
      startTimerHz(60);
    }

    void visibleAreaChanged(const juce::Rectangle<int> &) override {
      if (onVisibleAreaChanged)
        onVisibleAreaChanged();
    }

    void mouseWheelMove(const juce::MouseEvent &e,
                        const juce::MouseWheelDetails &wheel) override {
      if (auto *viewedComp = getViewedComponent()) {
        const int maximum = juce::jmax(0, viewedComp->getWidth() - getWidth());
        if (maximum > 0) {
          motion.setBounds(0.0f, (float)maximum);
          if (!isTimerRunning())
            motion.setPosition((float)getViewPositionX());
          const float wheelDelta =
              std::abs(wheel.deltaX) > 0.001f ? -wheel.deltaX
                                               : -wheel.deltaY;
          motion.addImpulse(wheelDelta * (wheel.isSmooth ? 72.0f : 110.0f));
          startTimerHz(60);
          return;
        }
      }
      juce::Viewport::mouseWheelMove(e, wheel);
    }

  private:
    void timerCallback() override {
      const bool moving = motion.tick();
      setViewPosition(juce::roundToInt(motion.getPosition()), 0);

      if (!moving) {
        stopTimer();
      }

      if (onVisibleAreaChanged)
        onVisibleAreaChanged();
    }

    BackgroundScrollMotion motion;
  };

  BackgroundSettingsDialog(BackgroundComponent &bg, BackgroundSettingsListener *l,
                           FluentLookAndFeel &laf)
      : background(bg), listener(l), fluentLookAndFeel(laf),
        tooltipOverlay(laf) {
    background.addChangeListener(this);
    setLookAndFeel(&fluentLookAndFeel);
    setOpaque(false);

    addAndMakeVisible(titleLabel);
    titleLabel.setText(L"背景图片", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(titleLabel, fluentLookAndFeel, true);

    addAndMakeVisible(effectsSectionLabel);
    effectsSectionLabel.setText(L"图像效果", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(effectsSectionLabel,
                                        fluentLookAndFeel, true);

    addAndMakeVisible(behaviorSectionLabel);
    behaviorSectionLabel.setText(L"窗口与界面", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(behaviorSectionLabel,
                                        fluentLookAndFeel, true);

    addAndMakeVisible(dialogMaterialLabel);
    dialogMaterialLabel.setText(L"窗口材质", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(dialogMaterialLabel,
                                        fluentLookAndFeel);

    addAndMakeVisible(dialogMaterialCombo);
    dialogMaterialCombo.addItem(L"纯透明", 1);
    dialogMaterialCombo.addItem(L"高斯模糊", 2);
    dialogMaterialCombo.addItem(L"Aero (毛玻璃)", 3);
    dialogMaterialCombo.addItem(L"Acrylic (亚克力)", 4);
    dialogMaterialCombo.setSelectedId(
        static_cast<int>(getAppSettings().getDialogMaterialType()),
        juce::dontSendNotification);
    dialogMaterialCombo.onChange = [this]() {
      getAppSettings().setDialogMaterialType(
          static_cast<WindowMaterial::Type>(dialogMaterialCombo.getSelectedId()));
      updateDialogMaterialControlState();
      applyDialogMaterialSettings(true);
      saveAppSettingsWithFeedback();
    };

    addAndMakeVisible(dialogOpacityLabel);
    dialogOpacityLabel.setText(L"背景不透明度", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(dialogOpacityLabel,
                                        fluentLookAndFeel);

    addAndMakeVisible(dialogOpacitySlider);
    dialogOpacitySlider.setRange(25.0, 98.0, 1.0);
    dialogOpacitySlider.setValue(
        getAppSettings().getDialogMaterialOpacity() * 100.0,
        juce::dontSendNotification);
    dialogOpacitySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58,
                                        24);
    dialogOpacitySlider.setTextValueSuffix("%");
    dialogOpacitySlider.onValueChange = [this]() {
      getAppSettings().setDialogMaterialOpacity(
          (float)dialogOpacitySlider.getValue() / 100.0f);
      applyDialogMaterialSettings(false);
    };
    dialogOpacitySlider.onDragEnd = []() { saveAppSettingsWithFeedback(); };

    addAndMakeVisible(dialogStrengthLabel);
    dialogStrengthLabel.setText(L"效果强度", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(dialogStrengthLabel,
                                        fluentLookAndFeel);

    addAndMakeVisible(dialogStrengthSlider);
    dialogStrengthSlider.setRange(1.0, 50.0, 1.0);
    dialogStrengthSlider.setValue(
        getAppSettings().getDialogMaterialStrength(),
        juce::dontSendNotification);
    dialogStrengthSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 58,
                                         24);
    dialogStrengthSlider.onValueChange = [this]() {
      getAppSettings().setDialogMaterialStrength(
          juce::roundToInt(dialogStrengthSlider.getValue()));
      applyDialogMaterialSettings(false);
    };
    dialogStrengthSlider.onDragEnd = [this]() {
      saveAppSettingsWithFeedback();
      applyDialogMaterialSettings(true);
    };

    addAndMakeVisible(imageLabel);
    imageLabel.setText(L"图片", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(imageLabel, fluentLookAndFeel);

    addAndMakeVisible(selectImageBtn);
    selectImageBtn.setButtonText(L"选择新图片");
    selectImageBtn.setColour(juce::TextButton::buttonColourId,
                             fluentLookAndFeel.getColors().controlBackground);
    selectImageBtn.onClick = [this]() { selectImage(); };

    addAndMakeVisible(clearImageBtn);
    clearImageBtn.setButtonText(L"清除");
    clearImageBtn.setColour(juce::TextButton::buttonColourId,
                            fluentLookAndFeel.getColors().controlBackground);
    clearImageBtn.onClick = [this]() { clearImage(); };

    addAndMakeVisible(recentImagesLabel);
    recentImagesLabel.setText(L"最近使用", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(recentImagesLabel,
                                        fluentLookAndFeel);

    addAndMakeVisible(historyViewport);
    historyViewport.setViewedComponent(&recentImagesContainer);
    historyViewport.setScrollBarsShown(false, false);
    historyViewport.onVisibleAreaChanged =
        [this]() { updateHistoryNavigationState(); };

    addAndMakeVisible(vectorLeftBtn);
    vectorLeftBtn.onClick = [this] {
      historyViewport.animateTo(
          historyLayout.getPreviousPagePosition(
              historyViewport.getViewPositionX()));
    };

    addAndMakeVisible(vectorRightBtn);
    vectorRightBtn.onClick = [this] {
      historyViewport.animateTo(
          historyLayout.getNextPagePosition(
              historyViewport.getViewPositionX()));
    };

    refreshRecentImages();

    addAndMakeVisible(blurModeLabel);
    blurModeLabel.setText(L"效果", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(blurModeLabel, fluentLookAndFeel);

    addAndMakeVisible(blurModeCombo);
    blurModeCombo.addItem(L"无", 1);
    blurModeCombo.addItem(L"高斯模糊", 2);
    blurModeCombo.addItem(L"Aero (毛玻璃)", 3);
    blurModeCombo.addItem(L"Acrylic (亚克力)", 4);

    // MaterialType 编号与 ComboBox 选项 ID 保持一致
    int selectedId = 1;
    switch (background.getMaterialType()) {
    case BackgroundComponent::MaterialType::GaussianBlur:
      selectedId = 2;
      break;
    case BackgroundComponent::MaterialType::Aero:
      selectedId = 3;
      break;
    case BackgroundComponent::MaterialType::Acrylic:
      selectedId = 4;
      break;
    default:
      selectedId = 1;
      break;
    }
    blurModeCombo.setSelectedId(selectedId, juce::dontSendNotification);

    blurModeCombo.onChange = [this]() {
      int id = blurModeCombo.getSelectedId();
      BackgroundComponent::MaterialType type =
          BackgroundComponent::MaterialType::None;
      switch (id) {
      case 2:
        type = BackgroundComponent::MaterialType::GaussianBlur;
        break;
      case 3:
        type = BackgroundComponent::MaterialType::Aero;
        break;
      case 4:
        type = BackgroundComponent::MaterialType::Acrylic;
        break;
      default:
        type = BackgroundComponent::MaterialType::None;
        break;
      }
      background.setMaterialType(type);
      updateBlurRadiusVisibility();
      if (listener)
        listener->backgroundSettingsChanged();
    };

    addAndMakeVisible(blurRadiusLabel);
    blurRadiusLabel.setText(L"强度", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(blurRadiusLabel,
                                        fluentLookAndFeel);

    addAndMakeVisible(blurRadiusSlider);
    blurRadiusSlider.setRange(1, 50, 1);
    blurRadiusSlider.setValue(background.getBlurRadius(),
                              juce::dontSendNotification);
    blurRadiusSlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 24);

    blurRadiusSlider.onValueChange = nullptr;
    blurRadiusSlider.onDragEnd = [this]() {
      background.setBlurRadius((int)blurRadiusSlider.getValue());
      if (listener)
        listener->backgroundSettingsChanged();
    };

    addAndMakeVisible(overlayLabel);
    overlayLabel.setText(L"遮罩", juce::dontSendNotification);
    FluentSettingsStyle::configureLabel(overlayLabel, fluentLookAndFeel);

    addAndMakeVisible(overlaySlider);
    overlaySlider.setRange(0.0, 1.0, 0.05);
    overlaySlider.setValue(background.getOverlayOpacity(),
                           juce::dontSendNotification);
    overlaySlider.setTextBoxStyle(juce::Slider::TextBoxRight, false, 50, 24);

    overlaySlider.onValueChange = [this]() {
      background.setOverlayOpacity((float)overlaySlider.getValue());
      if (listener)
        listener->backgroundSettingsChanged();
    };
    overlaySlider.onDragEnd = nullptr;

    addAndMakeVisible(monetToggle);
    monetToggle.setButtonText(L"自动提取主题色");

    // 背景可能仍在加载，因此同时检查已加载图像和设置中的路径
    bool hasBG = hasConfiguredBackground();
    monetToggle.setEnabled(hasBG);

    bool shouldBeOn = getAppSettings().getMonetEnabled() && hasBG;
    monetToggle.setToggleState(shouldBeOn, juce::dontSendNotification);

    monetToggle.onClick = [this]() {
      bool enabled = monetToggle.getToggleState();
      getAppSettings().setMonetEnabled(enabled);
      saveAppSettingsWithFeedback();
      paletteSelector.setVisible(enabled);

      background.loadAsync(true);
      if (!enabled) {
        background.setCurrentAccentColor(juce::Colour(0xFF0078D4));
      }

      if (listener)
        listener->backgroundSettingsChanged();
    };

    addAndMakeVisible(paletteSelector);
    paletteSelector.setPalette(background.getPalette());
    // 使用目标色同步选中标记，避免过渡中间色导致不匹配
    paletteSelector.setSelectedColor(background.getTargetAccentColor());
    paletteSelector.setVisible(getAppSettings().getMonetEnabled());
    paletteSelector.onColorSelected = [this](juce::Colour c) {
      background.setCurrentAccentColor(c);
      paletteSelector.setSelectedColor(c);
    };

    addAndMakeVisible(sequentialIconToggle);
    sequentialIconToggle.setButtonText(L"更换原生图标样式");
    sequentialIconToggle.setToggleState(
        getAppSettings().getSequentialIconListStyle(),
        juce::dontSendNotification);
    sequentialIconToggle.onClick = [this]() {
      getAppSettings().setSequentialIconListStyle(
          sequentialIconToggle.getToggleState());
      if (listener)
        listener->backgroundSettingsChanged();
    };

    addAndMakeVisible(rememberWindowToggle);
    rememberWindowToggle.setButtonText(L"记住窗口位置和大小");
    rememberWindowToggle.setToggleState(
        getAppSettings().getRememberWindowBounds(), juce::dontSendNotification);
    rememberWindowToggle.onClick = [this]() {
      getAppSettings().setRememberWindowBounds(
          rememberWindowToggle.getToggleState());
      saveAppSettingsWithFeedback();
    };

    addAndMakeVisible(currentPathLabel);
    FluentSettingsStyle::configureLabel(currentPathLabel, fluentLookAndFeel,
                                        false, true);
    currentPathLabel.setJustificationType(juce::Justification::centredLeft);
    updateCurrentPath();

    updateBlurRadiusVisibility();
    updateImageDependentControls();
    updateDialogMaterialControlState();

    // Ordering: 完成所有控件文本设置后再布局，ToggleButton 宽度依赖最终文案。
    setSize(640, getPreferredHeight());

    addChildComponent(tooltipOverlay);
    addMouseListener(&tooltipOverlay, true);
  }

  void paint(juce::Graphics &g) override {
    FluentSettingsStyle::paintPanel(g, fluentLookAndFeel, getLocalBounds());
    FluentSettingsStyle::paintCard(g, fluentLookAndFeel, sourceCardBounds);
    FluentSettingsStyle::paintCard(g, fluentLookAndFeel, effectsCardBounds);
    FluentSettingsStyle::paintCard(g, fluentLookAndFeel, behaviorCardBounds);
  }

  void resized() override {
    auto area =
        getLocalBounds().reduced(FluentSettingsStyle::panelMargin);
    auto topCards = area.removeFromTop(getTopCardsHeight());
    const int columnWidth = (topCards.getWidth() - 12) / 2;
    sourceCardBounds = topCards.removeFromLeft(columnWidth);
    topCards.removeFromLeft(12);
    effectsCardBounds = topCards;
    area.removeFromTop(12);
    behaviorCardBounds = area;

    auto source =
        sourceCardBounds.reduced(FluentSettingsStyle::cardPadding);
    titleLabel.setBounds(source.removeFromTop(22));
    source.removeFromTop(8);
    auto imageRow =
        source.removeFromTop(
            FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    imageLabel.setBounds(imageRow.removeFromLeft(64));
    clearImageBtn.setBounds(imageRow.removeFromRight(64));
    imageRow.removeFromRight(8);
    selectImageBtn.setBounds(imageRow);
    source.removeFromTop(12);
    auto historyHeader = source.removeFromTop(24);
    auto navigation = historyHeader.removeFromRight(60);
    vectorRightBtn.setBounds(navigation.removeFromRight(28));
    navigation.removeFromRight(4);
    vectorLeftBtn.setBounds(navigation.removeFromRight(28));
    recentImagesLabel.setBounds(historyHeader);
    source.removeFromTop(6);
    currentPathLabel.setBounds(source.removeFromBottom(24));
    source.removeFromBottom(8);
    historyViewport.setBounds(source);
    layoutRecentImageButtons();

    auto effects =
        effectsCardBounds.reduced(FluentSettingsStyle::cardPadding);
    effectsSectionLabel.setBounds(effects.removeFromTop(22));
    effects.removeFromTop(8);
    constexpr int effectLabelWidth = 64;
    auto blurRow =
        effects.removeFromTop(
            FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    blurModeLabel.setBounds(blurRow.removeFromLeft(effectLabelWidth));
    blurModeCombo.setBounds(blurRow);
    effects.removeFromTop(FluentSettingsStyle::rowGap);
    auto radiusRow =
        effects.removeFromTop(
            FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    blurRadiusLabel.setBounds(radiusRow.removeFromLeft(effectLabelWidth));
    blurRadiusSlider.setBounds(radiusRow);
    effects.removeFromTop(FluentSettingsStyle::rowGap);
    auto overlayRow =
        effects.removeFromTop(
            FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    overlayLabel.setBounds(overlayRow.removeFromLeft(effectLabelWidth));
    overlaySlider.setBounds(overlayRow);
    effects.removeFromTop(12);
    auto monetRow = effects.removeFromTop(
        FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    monetToggle.setBounds(monetRow.removeFromLeft(juce::jmin(
        monetRow.getWidth(),
        fluentLookAndFeel.getToggleButtonPreferredWidth(monetToggle))));
    effects.removeFromTop(8);
    paletteSelector.setBounds(effects.removeFromTop(40));

    auto behavior =
        behaviorCardBounds.reduced(FluentSettingsStyle::cardPadding);
    behaviorSectionLabel.setBounds(behavior.removeFromTop(22));
    behavior.removeFromTop(8);
    auto toggleRow =
        behavior.removeFromTop(
            FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    const int toggleWidth = (toggleRow.getWidth() - 16) / 2;
    auto sequentialArea = toggleRow.removeFromLeft(toggleWidth);
    sequentialIconToggle.setBounds(sequentialArea.removeFromLeft(juce::jmin(
        sequentialArea.getWidth(), fluentLookAndFeel
                                       .getToggleButtonPreferredWidth(
                                           sequentialIconToggle))));
    toggleRow.removeFromLeft(16);
    rememberWindowToggle.setBounds(toggleRow.removeFromLeft(juce::jmin(
        toggleRow.getWidth(), fluentLookAndFeel.getToggleButtonPreferredWidth(
                                  rememberWindowToggle))));

    constexpr int materialLabelWidth = 112;
    behavior.removeFromTop(FluentSettingsStyle::rowGap);
    auto materialRow = behavior.removeFromTop(
        FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    dialogMaterialLabel.setBounds(
        materialRow.removeFromLeft(materialLabelWidth));
    dialogMaterialCombo.setBounds(materialRow);

    behavior.removeFromTop(FluentSettingsStyle::rowGap);
    auto opacityRow = behavior.removeFromTop(
        FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    dialogOpacityLabel.setBounds(
        opacityRow.removeFromLeft(materialLabelWidth));
    dialogOpacitySlider.setBounds(opacityRow);

    behavior.removeFromTop(FluentSettingsStyle::rowGap);
    auto strengthRow = behavior.removeFromTop(
        FluentSettingsStyle::controlHeight(fluentLookAndFeel));
    dialogStrengthLabel.setBounds(
        strengthRow.removeFromLeft(materialLabelWidth));
    dialogStrengthSlider.setBounds(strengthRow);
  }

  ~BackgroundSettingsDialog() override {
    removeMouseListener(&tooltipOverlay);
    tooltipOverlay.hideTooltip();
    background.removeChangeListener(this);
    setLookAndFeel(nullptr);
  }

  void changeListenerCallback(juce::ChangeBroadcaster *) override {
    bool hasBG = hasConfiguredBackground();

    if (getAppSettings().getMonetEnabled() && !hasBG) {
      getAppSettings().setMonetEnabled(false);
      monetToggle.setToggleState(false, juce::dontSendNotification);
      paletteSelector.setVisible(false);
    } else {
      paletteSelector.setVisible(getAppSettings().getMonetEnabled());
      paletteSelector.setPalette(background.getPalette());
      // setPalette() 会刷新列表，之后恢复当前目标色选中状态
      paletteSelector.setSelectedColor(background.getTargetAccentColor());
    }

    updateImageDependentControls();
    repaint();
  }

  int getTopCardsHeight() const {
    const int rowHeight =
        FluentSettingsStyle::controlHeight(fluentLookAndFeel);
    return FluentSettingsStyle::cardPadding * 2 + 22 + 8 +
           rowHeight * 4 + FluentSettingsStyle::rowGap * 2 + 12 + 8 + 40;
  }

  int getBehaviorCardHeight() const {
    return FluentSettingsStyle::cardPadding * 2 + 22 + 8 +
           FluentSettingsStyle::controlHeight(fluentLookAndFeel) * 4 +
           FluentSettingsStyle::rowGap * 3;
  }

  int getPreferredHeight() const {
    return FluentSettingsStyle::panelMargin * 2 + getTopCardsHeight() + 12 +
           getBehaviorCardHeight();
  }

private:
  void updateDialogMaterialControlState() {
    const bool supportsStrength = WindowMaterial::supportsStrength(
        static_cast<WindowMaterial::Type>(dialogMaterialCombo.getSelectedId()));
    dialogStrengthLabel.setEnabled(supportsStrength);
    dialogStrengthSlider.setEnabled(supportsStrength);
    dialogStrengthLabel.setTooltip({});
    dialogStrengthSlider.setTooltip({});
  }

  void applyDialogMaterialSettings(bool backdropChanged) {
    if (listener) {
      listener->dialogMaterialChanged(backdropChanged);
    } else if (auto *window = findParentComponentOfClass<juce::DialogWindow>()) {
      if (backdropChanged)
        FluentSettingsStyle::refreshDialogMaterial(window);
      else
        FluentSettingsStyle::refreshDialogSurface(window);
    }
  }

  void selectImage() {
    fileChooser = std::make_unique<juce::FileChooser>(
        L"选择背景图片",
        juce::File::getSpecialLocation(juce::File::userPicturesDirectory),
        "*.png;*.jpg;*.jpeg;*.bmp;*.gif");

    auto safeThis =
        juce::Component::SafePointer<BackgroundSettingsDialog>(this);
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode,
        [safeThis](const juce::FileChooser &fc) {
          if (safeThis == nullptr)
            return;
          auto result = fc.getResult();
          if (result.existsAsFile())
            safeThis->showCropDialog(result);
        });
  }

  class CropDialog : public juce::Component {
  public:
    CropDialog(const juce::Image &img,
               FluentLookAndFeel &lookAndFeel,
               std::function<void(const juce::Image &)> onComplete)
        : callback(onComplete) {
      setSize(600, 500);
      addAndMakeVisible(cropper);
      cropper.setImage(img);
      addAndMakeVisible(applyBtn);
      applyBtn.setButtonText(L"应用");
      applyBtn.onClick = [this]() {
        if (callback)
          callback(cropper.getCroppedImage());
        if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
          dw->closeButtonPressed();
      };
      addAndMakeVisible(cancelBtn);
      cancelBtn.setButtonText(L"取消");
      cancelBtn.onClick = [this]() {
        if (auto *dw = findParentComponentOfClass<juce::DialogWindow>())
          dw->closeButtonPressed();
      };
      addAndMakeVisible(resetBtn);
      resetBtn.setButtonText(L"重置");
      resetBtn.onClick = [this]() { cropper.setCropRegion({0, 0, 1, 1}); };
      addAndMakeVisible(instructionLabel);
      instructionLabel.setText(L"拖动边框调整裁剪区域，或拖动中央移动选区",
                               juce::dontSendNotification);
      FluentSettingsStyle::configureLabel(instructionLabel, lookAndFeel,
                                          false, true);
      instructionLabel.setJustificationType(juce::Justification::centred);
    }

    void resized() override {
      auto area = getLocalBounds().reduced(16);
      instructionLabel.setBounds(area.removeFromTop(24));
      area.removeFromTop(8);
      auto buttonRow = area.removeFromBottom(36);
      int btnWidth = 80;
      int gap = 12;
      auto centered =
          buttonRow.withSizeKeepingCentre(btnWidth * 3 + gap * 2, 36);
      cancelBtn.setBounds(centered.removeFromLeft(btnWidth));
      centered.removeFromLeft(gap);
      resetBtn.setBounds(centered.removeFromLeft(btnWidth));
      centered.removeFromLeft(gap);
      applyBtn.setBounds(centered.removeFromLeft(btnWidth));
      area.removeFromBottom(12);
      cropper.setBounds(area);
    }

  private:
    ImageCropperComponent cropper;
    juce::TextButton applyBtn, cancelBtn, resetBtn;
    juce::Label instructionLabel;
    std::function<void(const juce::Image &)> callback;
  };

  void showCropDialog(const juce::File &imageFile) {
    auto img = juce::ImageFileFormat::loadFrom(imageFile);
    if (img.isNull())
      return;
    auto safeThis =
        juce::Component::SafePointer<BackgroundSettingsDialog>(this);
    auto *dialogContent = new CropDialog(
        img, fluentLookAndFeel,
        [safeThis](const juce::Image &croppedImage) {
          if (safeThis != nullptr && !croppedImage.isNull())
            safeThis->applyCroppedImage(croppedImage);
        });
    dialogContent->setLookAndFeel(&fluentLookAndFeel);
    juce::DialogWindow::LaunchOptions options;
    options.dialogTitle = L"裁剪图片";
    options.dialogBackgroundColour =
        fluentLookAndFeel.getColors().cardBackground;
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = false;
    options.resizable = true;
    options.componentToCentreAround = this;
    options.content.setOwned(dialogContent);
    FluentSettingsStyle::launchDialogAsync(options);
  }

  void applyCroppedImage(const juce::Image &croppedImage) {
    juce::File stored;
    if (const auto result = history.store(croppedImage, stored); result.failed()) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, L"无法保存背景",
          result.getErrorMessage(), L"确定", this);
      return;
    }

    getAppSettings().setBackgroundImagePath(stored.getFullPathName());
    saveAppSettingsWithFeedback();
    updateImageDependentControls();

    background.loadAsync(true);

    updateCurrentPath();
    refreshRecentImages();
    if (listener)
      listener->backgroundSettingsChanged();
  }

  bool isCurrentBackgroundFile(const juce::File &file) const {
    const auto currentPath = getAppSettings().getBackgroundImagePath();
    return currentPath.isNotEmpty() &&
           juce::File(currentPath).getFullPathName().equalsIgnoreCase(
               file.getFullPathName());
  }

  void showRecentImageContextMenu(
      const juce::File &file,
      juce::Component::SafePointer<ImagePreviewButton> target) {
    if (target == nullptr)
      return;

    const bool isCurrent = isCurrentBackgroundFile(file);
    juce::PopupMenu menu;
    menu.addItem(1,
                 isCurrent ? L"正在使用，无法移除"
                           : L"从最近使用中移除",
                 !isCurrent);

    auto safeThis =
        juce::Component::SafePointer<BackgroundSettingsDialog>(this);
    menu.showMenuAsync(
        juce::PopupMenu::Options().withTargetComponent(target.getComponent()),
        [safeThis, file](int result) {
          if (result == 1 && safeThis != nullptr)
            safeThis->removeRecentImageCache(file);
        });
  }

  void removeRecentImageCache(const juce::File &file) {
    const auto currentFile =
        juce::File(getAppSettings().getBackgroundImagePath());
    if (const auto result = history.remove(file, currentFile); result.failed()) {
      juce::AlertWindow::showMessageBoxAsync(
          juce::AlertWindow::WarningIcon, L"无法移除背景",
          result.getErrorMessage(), L"确定", this);
      return;
    }

    refreshRecentImages();
  }

  void refreshRecentImages() {
    recentImagesContainer.removeAllChildren();
    recentButtons.clear();

    const auto files = history.recentImages();
    int limit = files.size();

    for (int i = 0; i < limit; ++i) {
      auto f = files[i];
      auto *btn = new ImagePreviewButton(f);
      recentButtons.add(btn);
      btn->onClick = [this, f] {
        getAppSettings().setBackgroundImagePath(f.getFullPathName());
        saveAppSettingsWithFeedback();
        updateImageDependentControls();
        background.loadAsync(true);
        updateCurrentPath();
        if (listener)
          listener->backgroundSettingsChanged();
      };
      btn->onRemoveRequested = [this, f, safeButton =
                                    juce::Component::SafePointer<
                                        ImagePreviewButton>(btn)] {
        showRecentImageContextMenu(f, safeButton);
      };
      recentImagesContainer.addAndMakeVisible(btn);
    }

    // 图片数量决定箭头是否占位以及 Viewport 的实际宽度。刷新后必须重新
    // 执行整块布局，否则从 1 张变为多张时箭头仍保持空 bounds。
    historyViewport.jumpTo(0);
    resized();
    repaint();
  }

  void layoutRecentImageButtons() {
    const int count = recentButtons.size();
    const int viewportWidth = juce::jmax(1, historyViewport.getWidth());
    const int height = juce::jmax(1, historyViewport.getHeight());
    constexpr int gap = 8;
    historyLayout =
        makeBackgroundHistoryLayout(count, viewportWidth, gap);
    historyViewport.setScrollBounds(historyLayout.maxScrollX);

    if (count == 0) {
      recentImagesContainer.setSize(viewportWidth, height);
      updateHistoryNavigationState();
      return;
    }

    recentImagesContainer.setSize(historyLayout.totalWidth, height);

    for (int index = 0; index < count; ++index)
      recentButtons[index]->setBounds(
          index * (historyLayout.tileWidth + gap), 0,
          historyLayout.tileWidth, height);

    historyViewport.jumpTo(juce::jmin(historyViewport.getViewPositionX(),
                                      historyLayout.maxScrollX));
    updateHistoryNavigationState();
  }

  void updateHistoryNavigationState() {
    const bool hasOverflow = historyLayout.maxScrollX > 0;
    const int position = historyViewport.getViewPositionX();
    const bool canGoLeft =
        hasOverflow && historyLayout.canScrollLeft(position);
    const bool canGoRight =
        hasOverflow && historyLayout.canScrollRight(position);

    vectorLeftBtn.setVisible(hasOverflow);
    vectorRightBtn.setVisible(hasOverflow);
    vectorLeftBtn.setEnabled(canGoLeft);
    vectorRightBtn.setEnabled(canGoRight);
    vectorLeftBtn.setTooltip(canGoLeft ? L"查看较新的背景"
                                       : L"已经是第一组");
    vectorRightBtn.setTooltip(canGoRight ? L"查看更多背景"
                                         : L"已经是最后一组");
  }

  void clearImage() {
    getAppSettings().setBackgroundImagePath("");
    getAppSettings().setMonetEnabled(false);
    saveAppSettingsWithFeedback();
    monetToggle.setToggleState(false, juce::dontSendNotification);
    paletteSelector.setVisible(false);

    background.loadAsync(true);
    updateCurrentPath();
    updateImageDependentControls();

    if (listener)
      listener->backgroundSettingsChanged();
  }

  void updateCurrentPath() {
    auto path = getAppSettings().getBackgroundImagePath();
    if (path.isEmpty())
      currentPathLabel.setText(L"未选择图片", juce::dontSendNotification);
    else
      currentPathLabel.setText(juce::File(path).getFileName(),
                               juce::dontSendNotification);
  }

  void updateBlurRadiusVisibility() {
    const bool canEdit = isBackgroundEffectStrengthEnabled(
        blurModeCombo.getSelectedId(), hasConfiguredBackground());
    // 保留强度行的布局和语义。无效果时沿用窗口材质“纯透明”的禁用状态。
    blurRadiusLabel.setVisible(true);
    blurRadiusSlider.setVisible(true);
    blurRadiusSlider.setEnabled(canEdit);
    blurRadiusLabel.setEnabled(canEdit);
  }

  bool hasConfiguredBackground() const {
    const auto path = getAppSettings().getBackgroundImagePath();
    return path.isNotEmpty() && juce::File(path).existsAsFile();
  }

  void updateImageDependentControls() {
    const bool hasBackground = hasConfiguredBackground();
    blurModeCombo.setEnabled(hasBackground);
    blurModeLabel.setEnabled(hasBackground);
    overlaySlider.setEnabled(hasBackground);
    overlayLabel.setEnabled(hasBackground);
    monetToggle.setEnabled(hasBackground);
    clearImageBtn.setEnabled(hasBackground);

    const auto unavailableTip =
        hasBackground ? juce::String() : juce::String(L"选择背景图片后可用");
    blurModeCombo.setTooltip(unavailableTip);
    blurRadiusSlider.setTooltip(unavailableTip);
    overlaySlider.setTooltip(unavailableTip);
    monetToggle.setTooltip(unavailableTip);
    updateBlurRadiusVisibility();
  }

private:
  std::unique_ptr<juce::FileChooser> fileChooser;

  BackgroundComponent &background;
  BackgroundSettingsListener *listener;
  FluentLookAndFeel &fluentLookAndFeel;
  juce::Label titleLabel, effectsSectionLabel, behaviorSectionLabel, imageLabel,
      recentImagesLabel, blurModeLabel, blurRadiusLabel, overlayLabel,
      currentPathLabel, dialogMaterialLabel, dialogOpacityLabel,
      dialogStrengthLabel;
  juce::TextButton selectImageBtn, clearImageBtn;
  VectorIconButton vectorLeftBtn{"<", true}, vectorRightBtn{">", false};
  HorizontalViewport historyViewport;
  juce::Component recentImagesContainer;
  juce::OwnedArray<ImagePreviewButton> recentButtons;
  juce::ComboBox blurModeCombo;
  juce::ComboBox dialogMaterialCombo;
  juce::Slider blurRadiusSlider, overlaySlider, dialogOpacitySlider,
      dialogStrengthSlider;
  juce::ToggleButton monetToggle;
  PaletteSelector paletteSelector;
  juce::ToggleButton sequentialIconToggle;
  juce::ToggleButton rememberWindowToggle;
  juce::Rectangle<int> sourceCardBounds, effectsCardBounds,
      behaviorCardBounds;
  BackgroundHistoryLayout historyLayout;
  EmbeddedTooltip tooltipOverlay;

  BackgroundHistory history{UserSettings::getSettingsDirectory().getChildFile("Backgrounds")};

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(BackgroundSettingsDialog)
};
