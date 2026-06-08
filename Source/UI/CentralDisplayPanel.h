#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

/**
    VST 插件编辑器容器。

    插件界面超过可用区域时交给 Viewport 滚动，容器本身不添加装饰。
*/
class CentralDisplayPanel : public juce::Component,
                            public juce::ComponentListener {
public:
  CentralDisplayPanel() {
    setOpaque(false);

    addAndMakeVisible(viewport);
    viewport.setScrollBarsShown(true, true, true, true);
    viewport.setScrollBarThickness(8);
    viewport.setScrollOnDragMode(juce::Viewport::ScrollOnDragMode::nonHover);

    addAndMakeVisible(editorContainer);
    viewport.setViewedComponent(&editorContainer, false);
  }

  ~CentralDisplayPanel() override { clearVstEditor(); }

  void setVstEditor(juce::AudioProcessorEditor *editor, int editorWidth,
                    int editorHeight) {
    if (vstEditor == editor)
      return;
    clearVstEditor();

    if (editor != nullptr) {
      vstEditor = editor;
      originalWidth = editorWidth > 0 ? editorWidth : 800;
      originalHeight = editorHeight > 0 ? editorHeight : 600;

      editorContainer.addAndMakeVisible(vstEditor);
      vstEditor->addComponentListener(this);
      vstEditor->setBounds(0, 0, originalWidth, originalHeight);
      editorContainer.setSize(originalWidth, originalHeight);

      hasEditor = true;
      resized();
    }
  }

  void clearVstEditor() {
    if (vstEditor != nullptr) {
      vstEditor->removeComponentListener(this);
      editorContainer.removeChildComponent(vstEditor);
      vstEditor = nullptr;
    }
    hasEditor = false;
    originalWidth = 0;
    originalHeight = 0;
    repaint();
  }

  bool hasVstEditor() const { return hasEditor; }

  void componentMovedOrResized(juce::Component &component, bool,
                               bool wasResized) override {
    if (&component == vstEditor && wasResized) {
      originalWidth = vstEditor->getWidth();
      originalHeight = vstEditor->getHeight();
      editorContainer.setSize(originalWidth, originalHeight);
      resized();
    }
  }

  void paint(juce::Graphics &g) override {
    if (!hasEditor) {
      g.setColour(juce::Colours::white.withAlpha(0.2f));
      g.setFont(juce::Font(juce::FontOptions(16.0f)));
      g.drawText(L"选择一个 VST3 乐器", getLocalBounds(),
                 juce::Justification::centred, true);
    }
  }

  void resized() override {
    auto area = getLocalBounds();

    if (hasEditor && vstEditor != nullptr) {
      bool fitsWidth = originalWidth <= area.getWidth();
      bool fitsHeight = originalHeight <= area.getHeight();

      if (fitsWidth && fitsHeight) {
        int offsetX = (area.getWidth() - originalWidth) / 2;
        int offsetY = (area.getHeight() - originalHeight) / 2;
        viewport.setBounds(offsetX, offsetY, originalWidth, originalHeight);
        viewport.setScrollBarsShown(false, false);
      } else {
        viewport.setBounds(area);
        viewport.setScrollBarsShown(!fitsHeight, !fitsWidth);
      }
    } else {
      viewport.setBounds(area);
    }
  }

private:
  juce::Viewport viewport;

  class EditorContainer : public juce::Component {
  public:
    EditorContainer() { setOpaque(false); }
  } editorContainer;

  juce::AudioProcessorEditor *vstEditor = nullptr;
  int originalWidth = 0;
  int originalHeight = 0;
  bool hasEditor = false;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CentralDisplayPanel)
};
