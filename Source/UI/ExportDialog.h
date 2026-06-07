#pragma once

#include <juce_gui_basics/juce_gui_basics.h>
#include "../AudioEngine/AudioEngine.h"
#include "CustomLookAndFeel.h"

// Horizontal scrolling label for long song names (similar to FL Studio status bar/hint panels)
class MarqueeLabel : public juce::Component, public juce::Timer {
public:
    MarqueeLabel() {
        startTimerHz(30); // 30 FPS smooth scrolling
    }
    
    ~MarqueeLabel() override {
        stopTimer();
    }
    
    void setText(const juce::String& newText) {
        if (text != newText) {
            text = newText;
            scrollPos = 0.0f;
            waitCounter = 0;
            repaint();
        }
    }
    
    void setFont(const juce::Font& newFont) {
        font = newFont;
        repaint();
    }
    
    void setColour(juce::Colour newColour) {
        colour = newColour;
        repaint();
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
        
        if (textWidth > labelWidth) {
            if (waitCounter < 60) { // Wait 2 seconds before starting scroll
                waitCounter++;
            } else {
                float loopGap = 40.0f;
                float totalWidth = (float)textWidth + loopGap;
                
                scrollPos += 0.8f; // Smooth scroll speed
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
        }
    }

private:
    juce::String text;
    juce::Font font { juce::FontOptions() };
    juce::Colour colour = juce::Colours::white;
    float scrollPos = 0.0f;
    int waitCounter = 0;
};

class ExportDialog : public juce::Component, 
                     public juce::ComboBox::Listener, 
                     public juce::Button::Listener,
                     public juce::Timer {
public:
    ExportDialog(const juce::StringArray& trackNames, int currentTrackIdx, std::function<void(int, const ExportSettings&)> onConfirm)
        : onExportConfirmed(std::move(onConfirm)) {
        
        setLookAndFeel(&fluentLookAndFeel);

        // Track Selector
        addAndMakeVisible(trackLabel);
        trackLabel.setText(L"待导出曲目", juce::dontSendNotification);
        
        addAndMakeVisible(trackCombo);
        trackCombo.setColour(juce::ComboBox::textColourId, juce::Colours::transparentBlack); // Hide default ComboBox text rendering
        for (int i = 0; i < trackNames.size(); ++i) {
            trackCombo.addItem(juce::String(i + 1) + ". " + trackNames[i], i + 1);
        }
        trackCombo.addListener(this);

        // Marquee Label overlay for track selector
        addAndMakeVisible(trackMarquee);
        trackMarquee.setFont(fluentLookAndFeel.getDefaultFont(13.0f));
        trackMarquee.setColour(fluentLookAndFeel.getColors().textPrimary);
        trackMarquee.setInterceptsMouseClicks(false, false); // Click through to dropdown arrow

        // Preset
        addAndMakeVisible(presetLabel);
        presetLabel.setText(L"预设方案", juce::dontSendNotification);
        addAndMakeVisible(presetCombo);
        presetCombo.addItem(L"CD 标准发售级 (WAV / 44.1kHz / 16-bit)", 1);
        presetCombo.addItem(L"高解析度音频 (FLAC / 96kHz / 24-bit)", 2);
        presetCombo.addItem(L"专业母带级 (WAV / 192kHz / 32-bit)", 3);
        presetCombo.addItem(L"自定义 (Custom)", 4);
        presetCombo.addListener(this);

        // Format
        addAndMakeVisible(formatLabel);
        formatLabel.setText(L"文件格式", juce::dontSendNotification);
        addAndMakeVisible(formatCombo);
        formatCombo.addItem("WAV", 1);
        formatCombo.addItem("FLAC", 2);
        formatCombo.addItem("Ogg Vorbis", 3);
        formatCombo.addListener(this);

        // Sample Rate
        addAndMakeVisible(srLabel);
        srLabel.setText(L"采样率", juce::dontSendNotification);
        addAndMakeVisible(srCombo);
        srCombo.addItem("44100 Hz", 1);
        srCombo.addItem("48000 Hz", 2);
        srCombo.addItem("88200 Hz", 3);
        srCombo.addItem("96000 Hz", 4);
        srCombo.addItem("192000 Hz", 5);
        srCombo.addListener(this);

        // Bit Depth
        addAndMakeVisible(bitLabel);
        bitLabel.setText(L"位深度", juce::dontSendNotification);
        addAndMakeVisible(bitCombo);
        bitCombo.addItem("16-bit", 1);
        bitCombo.addItem("24-bit", 2);
        bitCombo.addItem("32-bit Float", 3);
        bitCombo.addListener(this);

        // Quality
        addAndMakeVisible(qualityLabel);
        qualityLabel.setText(L"压缩等级", juce::dontSendNotification);
        addAndMakeVisible(qualityCombo);
        updateQualityOptions();
        qualityCombo.addListener(this);

        // Tail Mode
        addAndMakeVisible(tailLabel);
        tailLabel.setText(L"尾音处理", juce::dontSendNotification);
        addAndMakeVisible(tailCombo);
        tailCombo.addItem(L"自动静音检测 (推荐)", 1);
        tailCombo.addItem(L"固定时长", 2);
        tailCombo.addListener(this);

        addAndMakeVisible(tailSlider);
        tailSlider.setRange(0.0, 10.0, 0.5);
        tailSlider.setTextValueSuffix(L" 秒");
        tailSlider.setValue(3.0);
        tailSlider.onValueChange = [this] { presetCombo.setSelectedId(4, juce::dontSendNotification); };

        // Buttons
        addAndMakeVisible(exportBtn);
        exportBtn.setButtonText(L"选择路径并导出");
        exportBtn.addListener(this);

        addAndMakeVisible(cancelBtn);
        cancelBtn.setButtonText(L"取消");
        cancelBtn.addListener(this);

        // Hint Label (Status hint bar like FL Studio)
        addAndMakeVisible(hintLabel);
        hintLabel.setFont(fluentLookAndFeel.getDefaultFont(13.0f));
        hintLabel.setMinimumHorizontalScale(1.0f); // Disable auto-scaling to keep font sizes 100% unified!
        hintLabel.setColour(juce::Label::textColourId, fluentLookAndFeel.getColors().textSecondary);
        hintLabel.setJustificationType(juce::Justification::centredLeft);
        
        // Start hover/marquee timer
        startTimerHz(20);

        // Set initial selected track
        if (currentTrackIdx >= 0 && currentTrackIdx < trackNames.size()) {
            trackCombo.setSelectedId(currentTrackIdx + 1, juce::dontSendNotification);
            trackMarquee.setText(trackCombo.getText());
        } else if (trackNames.size() > 0) {
            trackCombo.setSelectedId(1, juce::dontSendNotification);
            trackMarquee.setText(trackCombo.getText());
        }

        // Default to Hi-Res preset
        presetCombo.setSelectedId(2, juce::sendNotification);

        setSize(420, 430); // Comfortably fit all UI elements including quality combo
    }

    ~ExportDialog() override {
        stopTimer();
        setLookAndFeel(nullptr);
    }

    void paint(juce::Graphics& g) override {
        g.fillAll(fluentLookAndFeel.getColors().background);
        g.setColour(fluentLookAndFeel.getColors().cardBackground);
        g.fillRoundedRectangle(getLocalBounds().toFloat().reduced(10), 8.0f);
    }

    void resized() override {
        auto area = getLocalBounds().reduced(20); // Comfortable padding of 20px near borders

        // Row 1: Track Selector
        auto rowArea = area.removeFromTop(30);
        trackLabel.setBounds(rowArea.removeFromLeft(100));
        trackCombo.setBounds(rowArea);
        
        auto marqueeRect = trackCombo.getBounds();
        marqueeRect.removeFromRight(30); // Leave room for arrow
        marqueeRect.removeFromLeft(6);   // Small left margin
        trackMarquee.setBounds(marqueeRect);

        area.removeFromTop(10);

        // Row 2: Preset
        rowArea = area.removeFromTop(30);
        presetLabel.setBounds(rowArea.removeFromLeft(100));
        presetCombo.setBounds(rowArea);

        area.removeFromTop(10);
        
        // Row 3: Format
        rowArea = area.removeFromTop(30);
        formatLabel.setBounds(rowArea.removeFromLeft(100));
        formatCombo.setBounds(rowArea);

        area.removeFromTop(10);
        
        // Row 4: Sample Rate
        rowArea = area.removeFromTop(30);
        srLabel.setBounds(rowArea.removeFromLeft(100));
        srCombo.setBounds(rowArea);

        area.removeFromTop(10);
        
        // Row 5: Bit Depth
        rowArea = area.removeFromTop(30);
        bitLabel.setBounds(rowArea.removeFromLeft(100));
        bitCombo.setBounds(rowArea);

        area.removeFromTop(10);
        
        // Row 5.5: Quality
        rowArea = area.removeFromTop(30);
        qualityLabel.setBounds(rowArea.removeFromLeft(100));
        qualityCombo.setBounds(rowArea);

        area.removeFromTop(10);
        
        // Row 6: Tail Mode
        rowArea = area.removeFromTop(30);
        tailLabel.setBounds(rowArea.removeFromLeft(100));
        tailCombo.setBounds(rowArea.removeFromLeft(150));
        rowArea.removeFromLeft(10);
        tailSlider.setBounds(rowArea);

        // Row 7: Buttons
        area.removeFromTop(15);
        auto btnArea = area.removeFromTop(30);
        cancelBtn.setBounds(btnArea.removeFromRight(80));
        btnArea.removeFromRight(10);
        exportBtn.setBounds(btnArea.removeFromRight(150));

        // Row 8: Hint Label
        area.removeFromTop(15);
        hintLabel.setBounds(area);
    }

    void comboBoxChanged(juce::ComboBox* box) override {
        if (box == &trackCombo) {
            trackMarquee.setText(trackCombo.getText());
        } else if (box == &presetCombo) {
            int id = presetCombo.getSelectedId();
            if (id == 1) { // CD
                formatCombo.setSelectedId(1, juce::dontSendNotification);
                srCombo.setSelectedId(1, juce::dontSendNotification);
                bitCombo.setSelectedId(1, juce::dontSendNotification);
            } else if (id == 2) { // Hi-Res
                formatCombo.setSelectedId(2, juce::dontSendNotification);
                srCombo.setSelectedId(4, juce::dontSendNotification);
                bitCombo.setSelectedId(2, juce::dontSendNotification);
            } else if (id == 3) { // Mastering
                formatCombo.setSelectedId(1, juce::dontSendNotification);
                srCombo.setSelectedId(5, juce::dontSendNotification);
                bitCombo.setSelectedId(3, juce::dontSendNotification);
            }
        } else {
            presetCombo.setSelectedId(4, juce::dontSendNotification);
        }

        // Validate format compatibility
        auto formatName = formatCombo.getText();
        
        if (box == &formatCombo) {
            updateQualityOptions();
        }
        
        if (formatName == "FLAC" || formatName == "Ogg Vorbis") {
            bitCombo.setItemEnabled(3, false);
            if (bitCombo.getSelectedId() == 3) {
                bitCombo.setSelectedId(formatName == "FLAC" ? 2 : 1, juce::dontSendNotification);
            }
        } else {
            bitCombo.setItemEnabled(3, true);
        }

        if (formatName == "Ogg Vorbis") {
            srCombo.setItemEnabled(3, false); // 88.2k
            srCombo.setItemEnabled(4, false); // 96k
            srCombo.setItemEnabled(5, false); // 192k
            if (srCombo.getSelectedId() > 2) {
                srCombo.setSelectedId(2, juce::dontSendNotification);
            }
        } else {
            srCombo.setItemEnabled(3, true);
            srCombo.setItemEnabled(4, true);
            srCombo.setItemEnabled(5, true);
        }

        tailSlider.setEnabled(tailCombo.getSelectedId() == 2);
    }

    void buttonClicked(juce::Button* b) override {
        if (b == &exportBtn) {
            ExportSettings s;
            s.formatName = formatCombo.getText();
            s.sampleRate = srCombo.getText().upToFirstOccurrenceOf(" ", false, false).getDoubleValue();
            s.bitDepth = bitCombo.getText().upToFirstOccurrenceOf("-", false, false).getIntValue();
            s.autoTail = (tailCombo.getSelectedId() == 1);
            s.fixedTailSeconds = tailSlider.getValue();
            
            s.qualityIndex = qualityCombo.getSelectedId() - 1;
            
            juce::String trackTitle = trackCombo.getText().upToLastOccurrenceOf(".", false, false);
            s.title = trackTitle.isEmpty() ? trackCombo.getText() : trackTitle;
            
            int selectedIdx = trackCombo.getSelectedId() - 1;

            if (auto* parent = findParentComponentOfClass<juce::DialogWindow>())
                parent->exitModalState(1);

            if (onExportConfirmed)
                onExportConfirmed(selectedIdx, s);
        } else if (b == &cancelBtn) {
            if (auto* parent = findParentComponentOfClass<juce::DialogWindow>())
                parent->exitModalState(0);
        }
    }

    void timerCallback() override {
        auto* hoveredComponent = getComponentAt(getMouseXYRelative());
        
        juce::String hintText;
        if (hoveredComponent != nullptr) {
            auto* c = hoveredComponent;
            while (c != nullptr) {
                if (c == &trackCombo || c->getParentComponent() == &trackCombo) {
                    hintText = L"选择播放列表中任意 MIDI 文件进行离线导出";
                    break;
                }
                if (c == &presetCombo || c->getParentComponent() == &presetCombo) {
                    hintText = L"快速切换预设的音频导出规格组合 (CD级/高解析度/母带级)";
                    break;
                }
                if (c == &formatCombo || c->getParentComponent() == &formatCombo) {
                    hintText = L"选择音频输出的封装格式 (WAV无损/FLAC压缩/Ogg有损)";
                    break;
                }
                if (c == &srCombo || c->getParentComponent() == &srCombo) {
                    hintText = L"提高采样率可显著降低合成算法在离线渲染时的高频失真";
                    break;
                }
                if (c == &bitCombo || c->getParentComponent() == &bitCombo) {
                    hintText = L"16位适合发行，24位为行业标准，32位浮点动态无限防削波";
                    break;
                }
                if (c == &qualityCombo || c->getParentComponent() == &qualityCombo) {
                    hintText = L"控制无损压缩算法的计算深度或有损格式的目标比特率";
                    break;
                }
                if (c == &tailCombo || c->getParentComponent() == &tailCombo) {
                    hintText = L"智能静音检测能完整保留乐器尾音，固定时长则进行硬剪切";
                    break;
                }
                if (c == &tailSlider) {
                    hintText = L"在 MIDI 音符播放结束后，强制继续向后录制并渲染的秒数";
                    break;
                }
                if (c == &exportBtn) {
                    hintText = L"选择本地保存路径并开始高保真极速离线音频渲染";
                    break;
                }
                if (c == &cancelBtn) {
                    hintText = L"放弃当前所有导出设置并关闭离线导出对话框";
                    break;
                }
                c = c->getParentComponent();
            }
        }

        if (hintText.isEmpty()) {
            hintText = L"将鼠标移至设置项上查看专业释义";
        }

        if (hintLabel.getText() != hintText) {
            hintLabel.setText(hintText, juce::dontSendNotification);
        }
    }

private:
    FluentLookAndFeel fluentLookAndFeel;
    juce::Label trackLabel, presetLabel, formatLabel, srLabel, bitLabel, tailLabel, qualityLabel;
    juce::ComboBox trackCombo, presetCombo, formatCombo, srCombo, bitCombo, tailCombo, qualityCombo;
    MarqueeLabel trackMarquee;
    juce::Slider tailSlider;
    juce::Label hintLabel;
    juce::TextButton exportBtn, cancelBtn;
    std::function<void(int, const ExportSettings&)> onExportConfirmed;

    void updateQualityOptions() {
        auto formatName = formatCombo.getText();
        qualityCombo.clear();
        if (formatName == "WAV") {
            qualityCombo.setEnabled(false);
            qualityCombo.addItem(L"无损无压缩", 1);
            qualityCombo.setSelectedId(1, juce::dontSendNotification);
        } else if (formatName == "FLAC") {
            qualityCombo.setEnabled(true);
            for (int i = 0; i <= 8; ++i) {
                qualityCombo.addItem(L"级别 " + juce::String(i) + (i == 0 ? L" (文件大, 速度快)" : (i == 8 ? L" (文件小, 速度慢)" : L"")), i + 1);
            }
            qualityCombo.setSelectedId(6, juce::dontSendNotification); // Default FLAC level 5
        } else if (formatName == "Ogg Vorbis") {
            qualityCombo.setEnabled(true);
            for (int i = 1; i <= 10; ++i) {
                qualityCombo.addItem(L"质量 " + juce::String(i) + (i == 1 ? L" (最低音质)" : (i == 10 ? L" (最高音质)" : L"")), i);
            }
            qualityCombo.setSelectedId(6, juce::dontSendNotification); // Default Ogg level 6
        }
    }
};
