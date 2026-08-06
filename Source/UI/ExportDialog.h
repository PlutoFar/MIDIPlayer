#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../AudioEngine/AudioEngine.h"
#include "CustomLookAndFeel.h"
#include "ExportHintState.h"
#include "MarqueeLabelSupport.h"
#include "ExportPresetSupport.h"

#include <vector>

class MarqueeLabel : public juce::Component, public juce::Timer {
public:
    MarqueeLabel() = default;

    ~MarqueeLabel() override {
        stopTimer();
    }

    void setText(const juce::String& newText) {
        if (text != newText) {
            text = newText;
            scrollPos = 0.0f;
            waitCounter = 0;
            updateTimerState();
            repaint();
        }
    }

    void setFont(const juce::Font& newFont) {
        font = newFont;
        updateTimerState();
        repaint();
    }

    void setColour(juce::Colour newColour) {
        colour = newColour;
        repaint();
    }

    void resized() override {
        updateTimerState();
    }

    void paint(juce::Graphics& g) override {
        g.setFont(font);
        g.setColour(colour);

        int textWidth = juce::GlyphArrangement::getStringWidthInt(font, text);
        int labelWidth = getWidth();

        if (textWidth <= labelWidth) {
            g.drawText(text, getLocalBounds(), juce::Justification::centredLeft, true);
        } else {
            float loopGap = 40.0f;
            float totalWidth = (float)textWidth + loopGap;

            g.saveState();
            g.reduceClipRegion(getLocalBounds());

            g.drawText(text, -(int)scrollPos, 0, textWidth, getHeight(), juce::Justification::centredLeft, false);
            g.drawText(text, -(int)scrollPos + (int)totalWidth, 0, textWidth, getHeight(), juce::Justification::centredLeft, false);

            g.restoreState();
        }
    }

    void timerCallback() override {
        int textWidth = juce::GlyphArrangement::getStringWidthInt(font, text);
        int labelWidth = getWidth();

        if (shouldRunMarqueeTimer(textWidth, labelWidth)) {
            if (waitCounter < 60) { // 约 2 秒后开始滚动。
                waitCounter++;
            } else {
                float loopGap = 40.0f;
                float totalWidth = (float)textWidth + loopGap;

                scrollPos += 0.8f;
                if (scrollPos >= totalWidth) {
                    scrollPos = 0.0f;
                    waitCounter = 0;
                }
                repaint();
            }
        } else {
            if (scrollPos != 0.0f) {
                scrollPos = 0.0f;
                repaint();
            }
            stopTimer();
        }
    }

private:
    void updateTimerState() {
        const int textWidth = juce::GlyphArrangement::getStringWidthInt(font, text);
        if (shouldRunMarqueeTimer(textWidth, getWidth())) {
            if (!isTimerRunning())
                startTimerHz(30);
        } else {
            stopTimer();
            scrollPos = 0.0f;
            waitCounter = 0;
        }
    }

    juce::String text;
    juce::Font font { juce::FontOptions() };
    juce::Colour colour = juce::Colours::white;
    float scrollPos = 0.0f;
    int waitCounter = 0;
};

class HintAccentBar : public juce::Component {
public:
    void setAccentColour(juce::Colour colour) {
        accentColour = colour;
        repaint();
    }

    void paint(juce::Graphics& g) override {
        g.setColour(accentColour);
        g.fillRoundedRectangle(getLocalBounds().toFloat(), 1.5f);
    }

private:
    juce::Colour accentColour = juce::Colours::cornflowerblue;
};

class ExportDialog : public juce::Component,
                     public juce::ComboBox::Listener,
                     public juce::Button::Listener,
                     public juce::Timer {
public:
    ExportDialog(FluentLookAndFeel &applicationLookAndFeel,
                 const juce::StringArray& trackNames, int currentTrackIdx,
                 std::function<void(int, const ExportSettings&)> onConfirm)
        : fluentLookAndFeel(applicationLookAndFeel),
          onExportConfirmed(std::move(onConfirm)) {
        setLookAndFeel(&fluentLookAndFeel);

        const auto fieldFont = fluentLookAndFeel.getDefaultFont(14.0f);
        addAndMakeVisible(trackLabel);
        trackLabel.setText(L"待导出曲目", juce::dontSendNotification);
        trackLabel.setFont(fieldFont);

        addAndMakeVisible(trackCombo);
        // ComboBox 自带文本透明，由 trackMarquee 覆盖绘制长曲名。
        trackCombo.setColour(juce::ComboBox::textColourId, juce::Colours::transparentBlack);
        for (int i = 0; i < trackNames.size(); ++i) {
            trackCombo.addItem(juce::String(i + 1) + ". " + trackNames[i], i + 1);
        }
        trackCombo.addListener(this);

        // 覆盖层只绘制文本，鼠标事件继续交给下拉框处理。
        addAndMakeVisible(trackMarquee);
        trackMarquee.setFont(fluentLookAndFeel.getDefaultFont(14.0f));
        trackMarquee.setColour(fluentLookAndFeel.getColors().textPrimary);
        trackMarquee.setInterceptsMouseClicks(false, false);

        addAndMakeVisible(presetLabel);
        presetLabel.setText(L"预设方案", juce::dontSendNotification);
        presetLabel.setFont(fieldFont);
        addAndMakeVisible(presetCombo);
        for (int presetId = 1; presetId <= 3; ++presetId)
            presetCombo.addItem(getExportPreset(presetId).displayName, presetId);
        presetCombo.addItem(L"自定义 (Custom)", 4);
        presetCombo.addListener(this);

        addAndMakeVisible(formatLabel);
        formatLabel.setText(L"文件格式", juce::dontSendNotification);
        formatLabel.setFont(fieldFont);
        addAndMakeVisible(formatCombo);
        formatCombo.addItem("WAV", 1);
        formatCombo.addItem("FLAC", 2);
        formatCombo.addItem("Ogg Vorbis", 3);
        formatCombo.addListener(this);

        addAndMakeVisible(srLabel);
        srLabel.setText(L"采样率", juce::dontSendNotification);
        srLabel.setFont(fieldFont);
        addAndMakeVisible(srCombo);
        srCombo.addItem("44100 Hz", 1);
        srCombo.addItem("48000 Hz", 2);
        srCombo.addItem("88200 Hz", 3);
        srCombo.addItem("96000 Hz", 4);
        srCombo.addItem("192000 Hz", 5);
        srCombo.addListener(this);

        addAndMakeVisible(bitLabel);
        bitLabel.setText(L"位深度", juce::dontSendNotification);
        bitLabel.setFont(fieldFont);
        addAndMakeVisible(bitCombo);
        bitCombo.addItem("16-bit", 16);
        bitCombo.addItem("24-bit", 24);
        bitCombo.addItem("32-bit Float", 32);
        bitCombo.addListener(this);

        addAndMakeVisible(qualityLabel);
        qualityLabel.setText(L"压缩等级", juce::dontSendNotification);
        qualityLabel.setFont(fieldFont);
        addAndMakeVisible(qualityCombo);
        qualityCombo.addListener(this);

        addAndMakeVisible(tailLabel);
        tailLabel.setText(L"尾音处理", juce::dontSendNotification);
        tailLabel.setFont(fieldFont);
        addAndMakeVisible(tailCombo);
        tailCombo.addItem(L"自动静音检测 (推荐)", 1);
        tailCombo.addItem(L"固定时长", 2);
        tailCombo.addListener(this);
        tailCombo.setSelectedId(1, juce::dontSendNotification);

        addAndMakeVisible(tailSlider);
        tailSlider.setRange(0.0, 10.0, 0.5);
        tailSlider.setTextValueSuffix(L" 秒");
        tailSlider.setValue(3.0);
        tailSlider.setEnabled(false);
        tailSlider.onValueChange = [this] {
            presetCombo.setSelectedId(4, juce::dontSendNotification);
        };

        addAndMakeVisible(exportBtn);
        exportBtn.setButtonText(L"选择路径并导出");
        exportBtn.addListener(this);

        addAndMakeVisible(cancelBtn);
        cancelBtn.setButtonText(L"取消");
        cancelBtn.addListener(this);

        const auto hintFont = fluentLookAndFeel.getDefaultFont(14.0f);
        addAndMakeVisible(hintAccent);
        hintAccent.setAccentColour(fluentLookAndFeel.getColors().accentPrimary);

        addAndMakeVisible(hintTitleLabel);
        hintTitleLabel.setFont(hintFont.boldened());
        hintTitleLabel.setMinimumHorizontalScale(1.0f);
        hintTitleLabel.setColour(juce::Label::textColourId,
                                 fluentLookAndFeel.getColors().textPrimary);
        hintTitleLabel.setJustificationType(juce::Justification::centredLeft);

        addAndMakeVisible(hintDescriptionLabel);
        hintDescriptionLabel.setFont(hintFont);
        hintDescriptionLabel.setMinimumHorizontalScale(1.0f);
        hintDescriptionLabel.setColour(juce::Label::textColourId,
                                       fluentLookAndFeel.getColors().textSecondary);
        hintDescriptionLabel.setJustificationType(juce::Justification::centredLeft);

        registerHintTarget(trackCombo, ExportHintTarget::track);
        registerHintTarget(presetCombo, ExportHintTarget::preset);
        registerHintTarget(formatCombo, ExportHintTarget::format);
        registerHintTarget(srCombo, ExportHintTarget::sampleRate);
        registerHintTarget(bitCombo, ExportHintTarget::bitDepth);
        registerHintTarget(qualityCombo, ExportHintTarget::quality);
        registerHintTarget(tailCombo, ExportHintTarget::tailMode);
        registerHintTarget(tailSlider, ExportHintTarget::tailDuration);
        registerHintTarget(exportBtn, ExportHintTarget::exportAction);
        registerHintTarget(cancelBtn, ExportHintTarget::cancelAction);
        refreshHint();

        // 定时器只检查已注册 ComboBox 的弹出状态，不做坐标命中扫描。
        startTimerHz(10);

        if (currentTrackIdx >= 0 && currentTrackIdx < trackNames.size()) {
            trackCombo.setSelectedId(currentTrackIdx + 1, juce::dontSendNotification);
            trackMarquee.setText(trackCombo.getText());
        } else if (trackNames.size() > 0) {
            trackCombo.setSelectedId(1, juce::dontSendNotification);
            trackMarquee.setText(trackCombo.getText());
        }

        presetCombo.setSelectedId(2, juce::sendNotification);

        setSize(420, 430);
    }

    ~ExportDialog() override {
        stopTimer();
        for (auto& binding : hintBindings)
            binding.component->removeMouseListener(this);
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(fluentLookAndFeel.getColors().background);
        g.setColour(fluentLookAndFeel.getColors().cardBackground);
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(10), 8.0f);

        if (!hintPanelBounds.isEmpty()) {
            g.setColour(fluentLookAndFeel.getColors().textSecondary.withAlpha(0.07f));
            g.fillRoundedRectangle(hintPanelBounds.toFloat(), 5.0f);
        }
    }

    void resized() override {
        auto area = getLocalBounds().reduced(20);

        auto rowArea = area.removeFromTop(30);
        trackLabel.setBounds(rowArea.removeFromLeft(100));
        trackCombo.setBounds(rowArea);

        auto marqueeRect = trackCombo.getBounds();
        marqueeRect.removeFromRight(30); // 保留右侧下拉箭头空间。
        marqueeRect.removeFromLeft(6);
        trackMarquee.setBounds(marqueeRect);

        area.removeFromTop(10);

        rowArea = area.removeFromTop(30);
        presetLabel.setBounds(rowArea.removeFromLeft(100));
        presetCombo.setBounds(rowArea);

        area.removeFromTop(10);

        rowArea = area.removeFromTop(30);
        formatLabel.setBounds(rowArea.removeFromLeft(100));
        formatCombo.setBounds(rowArea);

        area.removeFromTop(10);

        rowArea = area.removeFromTop(30);
        srLabel.setBounds(rowArea.removeFromLeft(100));
        srCombo.setBounds(rowArea);

        area.removeFromTop(10);

        rowArea = area.removeFromTop(30);
        bitLabel.setBounds(rowArea.removeFromLeft(100));
        bitCombo.setBounds(rowArea);

        area.removeFromTop(10);

        rowArea = area.removeFromTop(30);
        qualityLabel.setBounds(rowArea.removeFromLeft(100));
        qualityCombo.setBounds(rowArea);

        area.removeFromTop(10);

        rowArea = area.removeFromTop(30);
        tailLabel.setBounds(rowArea.removeFromLeft(100));
        tailCombo.setBounds(rowArea.removeFromLeft(150));
        rowArea.removeFromLeft(10);
        tailSlider.setBounds(rowArea);

        area.removeFromTop(15);
        auto btnArea = area.removeFromTop(30);
        cancelBtn.setBounds(btnArea.removeFromRight(80));
        btnArea.removeFromRight(10);
        exportBtn.setBounds(btnArea.removeFromRight(150));

        area.removeFromTop(12);
        hintPanelBounds = area;
        hintAccent.setBounds(area.removeFromLeft(3));
        area.removeFromLeft(10);
        area.reduce(0, 5);
        hintTitleLabel.setBounds(area.removeFromTop(20));
        hintDescriptionLabel.setBounds(area);
    }

    void comboBoxChanged(juce::ComboBox* box) override {
        if (box == &trackCombo) {
            trackMarquee.setText(trackCombo.getText());
            return;
        } else if (box == &presetCombo) {
            applyPreset(presetCombo.getSelectedId());
        } else if (box == &formatCombo || box == &srCombo ||
                   box == &bitCombo || box == &qualityCombo ||
                   box == &tailCombo) {
            if (box == &formatCombo)
                applyFormatCapabilities();
            syncPresetSelection();
        }

        tailSlider.setEnabled(tailCombo.getSelectedId() == 2);
        refreshHint();
    }

    void buttonClicked(juce::Button* b) override {
        if (b == &exportBtn) {
            ExportSettings s;
            s.formatName = formatCombo.getText();
            s.sampleRate = srCombo.getText().upToFirstOccurrenceOf(" ", false, false).getDoubleValue();
            s.bitDepth = bitCombo.getSelectedId();
            s.useFloatingPoint = s.formatName.containsIgnoreCase("Ogg") ||
                                 bitCombo.getText().containsIgnoreCase("Float");
            s.autoTail = (tailCombo.getSelectedId() == 1);
            s.fixedTailSeconds = tailSlider.getValue();
            s.qualityIndex =
                exportQualityIndexFromComboId(qualityCombo.getSelectedId());

            const auto validation = validateExportFormatSettings(
                s.formatName, s.sampleRate, s.bitDepth, s.useFloatingPoint,
                s.qualityIndex);
            if (validation.failed()) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, L"导出设置无效",
                    validation.getErrorMessage());
                return;
            }

            juce::String trackTitle = trackCombo.getText();
            if (trackTitle.contains(". "))
                trackTitle = trackTitle.fromFirstOccurrenceOf(". ", false, false);
            s.title = trackTitle.trim();

            int selectedIdx = trackCombo.getSelectedId() - 1;
            if (selectedIdx < 0) {
                juce::AlertWindow::showMessageBoxAsync(
                    juce::AlertWindow::WarningIcon, L"无法导出",
                    L"请先选择待导出的曲目。");
                return;
            }

            if (onExportConfirmed)
                onExportConfirmed(selectedIdx, s);

            if (auto* parent = findParentComponentOfClass<juce::DialogWindow>())
                parent->exitModalState(1);
        } else if (b == &cancelBtn) {
            if (auto* parent = findParentComponentOfClass<juce::DialogWindow>())
                parent->exitModalState(0);
        }
    }

    void mouseEnter(const juce::MouseEvent& event) override {
        hintState.setHoveredTarget(findTargetForComponent(event.eventComponent));
        refreshHint();
    }

    void mouseExit(const juce::MouseEvent&) override {
        if (hintState.getPopupTarget() == ExportHintTarget::none) {
            hintState.setHoveredTarget(findHoveredTarget());
            refreshHint();
        }
    }

    void timerCallback() override {
        const auto openPopup = findOpenPopupTarget();
        const auto previousPopup = hintState.getPopupTarget();

        if (openPopup != ExportHintTarget::none) {
            hintState.setPopupTarget(openPopup, true);
        } else {
            if (previousPopup != ExportHintTarget::none)
                hintState.setPopupTarget(previousPopup, false);
            hintState.setHoveredTarget(findHoveredTarget());
        }

        refreshHint();
    }

private:
    struct HintBinding {
        juce::Component* component = nullptr;
        ExportHintTarget target = ExportHintTarget::none;
    };

    FluentLookAndFeel &fluentLookAndFeel;
    juce::Label trackLabel, presetLabel, formatLabel, srLabel, bitLabel, tailLabel, qualityLabel;
    juce::ComboBox trackCombo, presetCombo, formatCombo, srCombo, bitCombo, tailCombo, qualityCombo;
    MarqueeLabel trackMarquee;
    juce::Slider tailSlider;
    HintAccentBar hintAccent;
    juce::Label hintTitleLabel, hintDescriptionLabel;
    juce::TextButton exportBtn, cancelBtn;
    std::function<void(int, const ExportSettings&)> onExportConfirmed;
    std::vector<HintBinding> hintBindings;
    ExportHintState hintState;
    juce::Rectangle<int> hintPanelBounds;

    void applyPreset(int presetId) {
        const auto preset = getExportPreset(presetId);
        if (preset.id == 4)
            return;

        const int formatId = preset.formatName == "WAV" ? 1 :
                             preset.formatName == "FLAC" ? 2 : 3;
        formatCombo.setSelectedId(formatId, juce::dontSendNotification);
        applyFormatCapabilities();

        const int sampleRateId =
            preset.sampleRate == 44100.0 ? 1 :
            preset.sampleRate == 48000.0 ? 2 :
            preset.sampleRate == 88200.0 ? 3 :
            preset.sampleRate == 96000.0 ? 4 : 5;
        srCombo.setSelectedId(sampleRateId, juce::dontSendNotification);
        bitCombo.setSelectedId(preset.bitDepth, juce::dontSendNotification);
        qualityCombo.setSelectedId(
                                   exportQualityComboIdFromIndex(
                                       preset.qualityIndex),
                                   juce::dontSendNotification);
        tailCombo.setSelectedId(preset.autoTail ? 1 : 2,
                                juce::dontSendNotification);
        tailSlider.setEnabled(!preset.autoTail);
    }

    void syncPresetSelection() {
        const auto formatName = formatCombo.getText();
        const auto sampleRate =
            srCombo.getText().upToFirstOccurrenceOf(" ", false, false)
                .getDoubleValue();
        const bool useFloatingPoint =
            formatName.containsIgnoreCase("Ogg") ||
            bitCombo.getText().containsIgnoreCase("Float");
        const int qualityIndex =
            exportQualityIndexFromComboId(qualityCombo.getSelectedId());

        const int matchingPreset = findMatchingExportPreset(
            formatName, sampleRate, bitCombo.getSelectedId(),
            useFloatingPoint, qualityIndex, tailCombo.getSelectedId() == 1);
        presetCombo.setSelectedId(matchingPreset, juce::dontSendNotification);
    }

    void applyFormatCapabilities() {
        const auto formatName = formatCombo.getText();
        const auto capabilities = getExportFormatCapabilities(formatName);
        const int previousBitDepth = bitCombo.getSelectedId();

        exportBtn.setEnabled(capabilities.available);

        const int sampleRates[] = { 44100, 48000, 88200, 96000, 192000 };
        for (int i = 0; i < 5; ++i) {
            const bool supported = capabilities.available &&
                                   capabilities.sampleRates.contains(sampleRates[i]);
            srCombo.setItemEnabled(i + 1, supported);
        }
        if (!capabilities.sampleRates.contains(
                srCombo.getText().getIntValue())) {
            for (int i = 0; i < 5; ++i) {
                if (capabilities.sampleRates.contains(sampleRates[i])) {
                    srCombo.setSelectedId(i + 1, juce::dontSendNotification);
                    break;
                }
            }
        }

        bitCombo.clear(juce::dontSendNotification);
        if (formatName == "WAV") {
            bitCombo.addItem(L"16-bit PCM", 16);
            bitCombo.addItem(L"24-bit PCM", 24);
            bitCombo.addItem(L"32-bit Float", 32);
        } else if (formatName == "FLAC") {
            bitCombo.addItem(L"16-bit PCM", 16);
            bitCombo.addItem(L"24-bit PCM", 24);
        } else if (formatName.containsIgnoreCase("Ogg")) {
            bitCombo.addItem(L"32-bit Float (编码器输入)", 32);
        }

        if (capabilities.bitDepths.contains(previousBitDepth))
            bitCombo.setSelectedId(previousBitDepth, juce::dontSendNotification);
        else if (capabilities.bitDepths.contains(24))
            bitCombo.setSelectedId(24, juce::dontSendNotification);
        else if (capabilities.bitDepths.contains(32))
            bitCombo.setSelectedId(32, juce::dontSendNotification);
        else
            bitCombo.setSelectedId(16, juce::dontSendNotification);

        bitCombo.setEnabled(capabilities.available &&
                            !formatName.containsIgnoreCase("Ogg"));

        qualityCombo.clear(juce::dontSendNotification);
        if (!capabilities.available || capabilities.qualityOptions.isEmpty()) {
            qualityCombo.setEnabled(false);
            qualityCombo.addItem(L"无损无压缩", 1);
            qualityCombo.setSelectedId(1, juce::dontSendNotification);
        } else {
            qualityCombo.setEnabled(true);
            for (int i = 0; i < capabilities.qualityOptions.size(); ++i) {
                auto label = capabilities.qualityOptions[i];
                if (formatName == "FLAC")
                    label = L"压缩等级 " + juce::String(i);
                qualityCombo.addItem(label, i + 1);
            }

            int defaultIndex = juce::jmin(5, capabilities.qualityOptions.size() - 1);
            if (formatName.containsIgnoreCase("Ogg")) {
                for (int i = 0; i < capabilities.qualityOptions.size(); ++i) {
                    if (capabilities.qualityOptions[i].contains("192")) {
                        defaultIndex = i;
                        break;
                    }
                }
            }
            qualityCombo.setSelectedId(defaultIndex + 1,
                                       juce::dontSendNotification);
        }
    }

    void registerHintTarget(juce::Component& component, ExportHintTarget target) {
        hintBindings.push_back({ &component, target });
        component.addMouseListener(this, true);
    }

    ExportHintTarget findTargetForComponent(const juce::Component* component) const {
        if (component == nullptr)
            return ExportHintTarget::none;

        for (const auto& binding : hintBindings) {
            if (binding.component == component ||
                binding.component->isParentOf(component))
                return binding.target;
        }
        return ExportHintTarget::none;
    }

    ExportHintTarget findHoveredTarget() const {
        for (const auto& binding : hintBindings) {
            if (binding.component->isMouseOver(true))
                return binding.target;
        }
        return ExportHintTarget::none;
    }

    ExportHintTarget findOpenPopupTarget() const {
        if (trackCombo.isPopupActive()) return ExportHintTarget::track;
        if (presetCombo.isPopupActive()) return ExportHintTarget::preset;
        if (formatCombo.isPopupActive()) return ExportHintTarget::format;
        if (srCombo.isPopupActive()) return ExportHintTarget::sampleRate;
        if (bitCombo.isPopupActive()) return ExportHintTarget::bitDepth;
        if (qualityCombo.isPopupActive()) return ExportHintTarget::quality;
        if (tailCombo.isPopupActive()) return ExportHintTarget::tailMode;
        return ExportHintTarget::none;
    }

    void refreshHint() {
        juce::String title = L"上下文检查器";
        juce::String description = L"将鼠标移至设置项上查看参数含义和格式约束。";

        switch (hintState.getActiveTarget()) {
            case ExportHintTarget::track:
                title = L"待导出曲目";
                description = L"从播放列表选择 MIDI；导出后会恢复原曲目和播放位置。";
                break;
            case ExportHintTarget::preset:
                title = L"预设方案";
                description = L"一次设置格式、采样率和位深；手动修改后切换为自定义。";
                break;
            case ExportHintTarget::format:
                title = L"文件格式";
                description = L"WAV 无压缩，FLAC 无损压缩，Ogg Vorbis 为有损编码。";
                break;
            case ExportHintTarget::sampleRate:
                title = L"采样率";
                description = L"决定每秒采样次数；仅显示当前编码器实际支持的规格。";
                break;
            case ExportHintTarget::bitDepth:
                if (!bitCombo.isEnabled()) {
                    title = L"位深度已锁定";
                    description = L"Ogg Vorbis 编码器固定接收 32-bit 浮点输入。";
                } else {
                    title = L"位深度";
                    description = L"16-bit 适合常规发行，24-bit 适合制作，32-bit WAV 使用浮点。";
                }
                break;
            case ExportHintTarget::quality:
                if (!qualityCombo.isEnabled()) {
                    title = L"压缩等级不适用";
                    description = L"WAV 不使用质量参数，音频数据按所选位深直接写入。";
                } else {
                    title = L"压缩等级";
                    description = L"FLAC 调整编码耗时与体积；Ogg 选择目标编码码率。";
                }
                break;
            case ExportHintTarget::tailMode:
                title = L"尾音处理";
                description = L"自动模式检测静音并保留释放尾音；固定模式按时长停止。";
                break;
            case ExportHintTarget::tailDuration:
                title = L"固定尾音时长";
                description = L"仅固定模式生效，从 MIDI 结束后继续渲染指定秒数。";
                break;
            case ExportHintTarget::exportAction:
                title = L"开始离线导出";
                description = L"选择保存位置后，以非实时模式渲染并原子替换目标文件。";
                break;
            case ExportHintTarget::cancelAction:
                title = L"取消";
                description = L"关闭窗口且不启动导出，当前播放状态不会改变。";
                break;
            case ExportHintTarget::none:
                break;
        }

        hintTitleLabel.setText(title, juce::dontSendNotification);
        hintDescriptionLabel.setText(description, juce::dontSendNotification);
    }
};
