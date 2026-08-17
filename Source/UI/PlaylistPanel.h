#pragma once

#include "../Playlist/PlaylistManager.h"
#include "../Utils/UserSettings.h"
#include "CustomLookAndFeel.h"
#include "PlaylistAnimationSupport.h"
#include <algorithm>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

class PlaylistPanel : public juce::Component,
                      public juce::ListBoxModel,
                      public juce::FileDragAndDropTarget,
                      public juce::DragAndDropTarget,
                      public juce::KeyListener,
                      public juce::Timer {
public:
  class AnimatedRowComponent : public juce::Component {
  public:
    explicit AnimatedRowComponent(PlaylistPanel &owner) : panel(owner) {
      setInterceptsMouseClicks(false, false);
    }

    void setRow(int newRow) {
      row = newRow;
      repaint();
    }

    void paint(juce::Graphics &g) override {
      panel.paintPlaylistRow(row, g, getWidth(), getHeight(),
                            panel.listBox.isRowSelected(row));
    }

  private:
    PlaylistPanel &panel;
    int row = -1;
  };

  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void playlistTrackSelected(int index) = 0;
    virtual void playlistTrackDoubleClicked(int index) = 0;
    virtual void playlistFilesDropped(const juce::StringArray &files) = 0;
    virtual bool playlistSaveRequested() = 0;
    virtual bool playlistClearRequested() = 0;
    virtual bool playlistLoadRequested(const juce::File &playlistFile) = 0;
    virtual bool playlistTrackMoveRequested(int fromIndex, int toIndex,
                                           int newCurrentIndex) = 0;
    virtual bool playlistTrackRemoveRequested(int index,
                                             int newCurrentIndex) = 0;
    virtual void playlistTrackRevealRequested(int index) = 0;
    virtual void playlistLoaded(const juce::File &playlistFile) = 0;
    virtual void playlistTrackReordered(int newCurrentIndex) = 0;
  };

  PlaylistPanel(PlaylistManager &pm) : playlistManager(pm) {
    addAndMakeVisible(listBox);
    listBox.setModel(this);
    listBox.setRowHeight(48);
    listBox.setColour(juce::ListBox::backgroundColourId,
                      juce::Colours::transparentBlack);
    listBox.setColour(juce::ListBox::outlineColourId,
                      juce::Colours::transparentBlack);
    listBox.setMultipleSelectionEnabled(false);
    listBox.setOpaque(false);
    listBox.setTitle(L"播放列表曲目");
    listBox.addKeyListener(this);

    addAndMakeVisible(headerLabel);
    headerLabel.setText(L"播放列表", juce::dontSendNotification);
    headerLabel.setJustificationType(juce::Justification::centredLeft);
    // 标题字体独立于页面标题，保持侧栏面板的密度。
    headerLabel.setFont(
        juce::Font(juce::FontOptions("SimHei", 16.0f, juce::Font::plain)));
    headerLabel.setColour(juce::Label::textColourId,
                          juce::Colours::white.withAlpha(0.85f));
    headerLabel.setInterceptsMouseClicks(false, false);

    addAndMakeVisible(addBtn);
    addBtn.setButtonText(L"添加");
    addBtn.setMouseClickGrabsKeyboardFocus(false);
    addBtn.onClick = [this]() { showAddFileDialog(); };

    addAndMakeVisible(clearBtn);
    clearBtn.setButtonText(L"清空");
    clearBtn.setMouseClickGrabsKeyboardFocus(false);
    clearBtn.onClick = [this]() { clearPlaylist(); };

    addAndMakeVisible(saveBtn);
    saveBtn.setButtonText(L"保存");
    saveBtn.setMouseClickGrabsKeyboardFocus(false);
    saveBtn.onClick = [this]() { savePlaylist(); };

    addAndMakeVisible(loadBtn);
    loadBtn.setButtonText(L"加载");
    loadBtn.setMouseClickGrabsKeyboardFocus(false);
    loadBtn.onClick = [this]() { loadPlaylist(); };

    addAndMakeVisible(countLabel);
    countLabel.setColour(juce::Label::textColourId,
                         juce::Colours::white.withAlpha(0.5f));
    countLabel.setInterceptsMouseClicks(false, false);
    updateCountLabel();
  }

  ~PlaylistPanel() override { listBox.removeKeyListener(this); }

  bool keyPressed(const juce::KeyPress &key,
                  juce::Component *) override {
    const int selected = listBox.getSelectedRow();
    if (key == juce::KeyPress::deleteKey && selected >= 0) {
      beginRemoveTrack(selected);
      return true;
    }
    if (key == juce::KeyPress::returnKey && selected >= 0) {
      if (auto *asyncListener = getAsyncListener())
        asyncListener->playlistTrackDoubleClicked(selected);
      return true;
    }
    return false;
  }

  void setListener(Listener *l) {
    listener = l;
    listenerComponent = juce::Component::SafePointer<juce::Component>(
        dynamic_cast<juce::Component *>(l));
  }

  void setCurrentTrackIndex(int index) {
    if (currentTrackIndex != index) {
      animationState.startCurrentTrackTransition(currentTrackIndex, index);
      ensureAnimationTimer();
    }
    currentTrackIndex = index;
    listBox.repaint();
    listBox.scrollToEnsureRowIsOnscreen(index);
  }

  int getCurrentTrackIndex() const { return currentTrackIndex; }

  void refresh() {
    updateRowHeight();
    listBox.updateContent();
    listBox.repaint();
    updateCountLabel();
    repaint();
  }

  void refreshWithInsertedRows(int firstRow, int rowCount) {
    refresh();
    animationState.startInsert(firstRow, rowCount);
    ensureAnimationTimer();
    updateVisibleRowTransforms();
  }

  void autoLoadLastPlaylist() {
    auto lastPath = getAppSettings().getLastPlaylistPath();
    if (lastPath.isNotEmpty()) {
      juce::File lastFile(lastPath);
      if (lastFile.existsAsFile()) {
        performLoad(lastFile);
      }
    }
  }

  void updateRowHeight() {
    float size = getAppSettings().getPlaylistFontSize();
    // 行高按字体 3 倍计算，避免较大字号时列表项显得拥挤。
    listBox.setRowHeight((int)(size * 3.0f));
  }

  void deselectAllRows() {
    listBox.deselectAllRows();
    listBox.repaint();
  }

  void startDropAnimation(const std::vector<int> &rows, bool isAccent) {
    animRows = rows;
    dropAnimIsAccent = isAccent;
    dropAnimProgress = 0.0f;
    ensureAnimationTimer();
  }

  int getNumRows() override { return playlistManager.size(); }

  void paintListBoxItem(int, juce::Graphics &, int, int, bool) override {}

  juce::Component *
  refreshComponentForRow(int row, bool,
                         juce::Component *existingComponent) override {
    auto *component =
        dynamic_cast<AnimatedRowComponent *>(existingComponent);
    if (component == nullptr)
      component = new AnimatedRowComponent(*this);
    component->setRow(row);
    return component;
  }

  void paintPlaylistRow(int row, juce::Graphics &g, int width, int height,
                        bool rowIsSelected) {
    auto *track = playlistManager.getTrack(row);
    if (track == nullptr)
      return;

    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(6, 2);

    auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel());
    auto colors = laf ? laf->getColors() : FluentLookAndFeel::FluentColors();

    const float currentHighlight =
        animationState.getCurrentHighlight(row);
    const float stableHighlight =
        row == currentTrackIndex && currentHighlight <= 0.0f
            ? 1.0f
            : currentHighlight;

    if (stableHighlight > 0.001f) {
      g.setColour(
          colors.accentPrimary.withAlpha(0.12f * stableHighlight));
      g.fillRoundedRectangle(area.toFloat(), 4.0f);

      auto indicatorArea = area.removeFromLeft(3).reduced(0, 8);
      const int indicatorHeight = juce::roundToInt(
          indicatorArea.getHeight() * stableHighlight);
      indicatorArea =
          indicatorArea.withSizeKeepingCentre(3, indicatorHeight);
      g.setColour(colors.accentPrimary.withAlpha(stableHighlight));
      g.fillRoundedRectangle(indicatorArea.toFloat(), 1.5f);
    } else if (rowIsSelected) {
      g.setColour(juce::Colour(0x1AFFFFFF));
      g.fillRoundedRectangle(area.toFloat(), 4.0f);
    }

    if (dropAnimProgress >= 0.0f &&
        std::find(animRows.begin(), animRows.end(), row) != animRows.end()) {
      float pulse =
          std::sin(dropAnimProgress * juce::MathConstants<float>::twoPi * 3.0f);
      float alpha = juce::jmax(0.0f, pulse);
      auto highlightColor = dropAnimIsAccent
                                ? colors.accentPrimary.withAlpha(alpha * 0.18f)
                                : juce::Colours::white.withAlpha(alpha * 0.12f);
      auto drawArea =
          juce::Rectangle<int>(0, 0, width, height).reduced(6, 2).toFloat();
      g.setColour(highlightColor);
      g.fillRoundedRectangle(drawArea, 4.0f);
    }

    auto indexArea = area.removeFromLeft(35);
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    float fontSize = getAppSettings().getPlaylistFontSize();
    g.setFont(juce::FontOptions(fontSize * 0.8f));
    g.drawText(juce::String(row + 1), indexArea,
               juce::Justification::centredRight);

    g.setColour(!track->available
                    ? colors.textDisabled
                    : stableHighlight > 0.5f
                          ? juce::Colours::white
                          : juce::Colours::white.withAlpha(0.85f));

    if (laf)
      g.setFont(
          laf->getPlaylistFont(fontSize, stableHighlight > 0.5f));
    else
      g.setFont(juce::FontOptions(fontSize));
    const auto displayName = track->available
                                 ? track->name
                                 : track->name + L"（文件不可用）";
    g.drawText(displayName, area.reduced(8, 0),
               juce::Justification::centredLeft, true);
  }

  void listBoxItemClicked(int row, const juce::MouseEvent &e) override {
    if (e.mods.isPopupMenu())
      showContextMenu(row);
    else if (listener)
      listener->playlistTrackSelected(row);
  }

  void listBoxItemDoubleClicked(int row, const juce::MouseEvent &) override {
    if (listener)
      listener->playlistTrackDoubleClicked(row);
  }

  void backgroundClicked(const juce::MouseEvent &) override {
    deselectAllRows();
  }

  void mouseDown(const juce::MouseEvent &) override {
    deselectAllRows();
  }

  juce::var
  getDragSourceDescription(const juce::SparseSet<int> &selectedRows) override {
    if (!animRows.empty())
      stopDropAnimation();

    if (selectedRows.size() == 1)
      return "trackIdx:" + juce::String(selectedRows[0]);
    return {};
  }

  bool isInterestedInDragSource(
      const juce::DragAndDropTarget::SourceDetails &) override {
    return true;
  }

  void
  itemDragMove(const juce::DragAndDropTarget::SourceDetails &details) override {
    // 拖拽坐标来自面板，需要换算到 ListBox 视口坐标。
    auto localPoint = listBox.getLocalPoint(this, details.localPosition);
    auto *viewport = listBox.getViewport();
    int scrollY = viewport ? viewport->getViewPositionY() : 0;
    int rowHeight = listBox.getRowHeight();

    int newInsertIndex =
        juce::jlimit(0, playlistManager.size(),
                     (localPoint.y + scrollY + rowHeight / 2) / rowHeight);

    if (newInsertIndex != dropInsertIndex) {
      dropInsertIndex = newInsertIndex;
      repaint();
    }
  }

  void itemDragExit(const juce::DragAndDropTarget::SourceDetails &) override {
    dropInsertIndex = -1;
    repaint();
  }

  void
  itemDropped(const juce::DragAndDropTarget::SourceDetails &details) override {
    // 最终插入位置同样按 ListBox 视口坐标计算。
    auto localPoint = listBox.getLocalPoint(this, details.localPosition);
    auto *viewport = listBox.getViewport();
    int scrollY = viewport ? viewport->getViewPositionY() : 0;
    int rowHeight = listBox.getRowHeight();

    int insertIndex =
        juce::jlimit(0, playlistManager.size(),
                     (localPoint.y + scrollY + rowHeight / 2) / rowHeight);

    bool movedAnyTrack = false;
    auto desc = details.description.toString();
    if (desc.startsWith("trackIdx:")) {
      int srcRow = desc.substring(9).getIntValue();
      if (srcRow >= 0 && srcRow < playlistManager.size() &&
          srcRow != insertIndex) {
        int targetRow = insertIndex;
        if (srcRow < insertIndex)
          targetRow--;

        if (targetRow != srcRow && targetRow >= 0) {
          bool movedPlayingTrack = (srcRow == currentTrackIndex);
          int newCurrentTrackIndex = currentTrackIndex;
          if (srcRow == currentTrackIndex) {
            newCurrentTrackIndex = targetRow;
          } else if (srcRow < currentTrackIndex &&
                     targetRow >= currentTrackIndex) {
            newCurrentTrackIndex--;
          } else if (srcRow > currentTrackIndex &&
                     targetRow <= currentTrackIndex) {
            newCurrentTrackIndex++;
          }

          bool moved = false;
          if (auto *asyncListener = getAsyncListener()) {
            moved = asyncListener->playlistTrackMoveRequested(
                srcRow, targetRow, newCurrentTrackIndex);
          } else {
            moved = playlistManager.moveTrack(srcRow, targetRow);
          }

          if (moved) {
            movedAnyTrack = true;
            currentTrackIndex = newCurrentTrackIndex;
            animationState.setCurrentTrackImmediate(currentTrackIndex);
            if (movedPlayingTrack) {
              animationState.clearReorder();
            } else {
              animationState.startReorder(
                  srcRow, targetRow, (float)listBox.getRowHeight());
            }
            listBox.selectRow(targetRow);
            startDropAnimation(targetRow, movedPlayingTrack);
            ensureAnimationTimer();
          }
        }
      }
    }

    dropInsertIndex = -1;
    refresh();
    repaint();
    if (movedAnyTrack && listener)
      listener->playlistTrackReordered(currentTrackIndex);
  }

  void paintOverChildren(juce::Graphics &g) override {
    auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel());
    const auto colors = laf != nullptr ? laf->getColors()
                                       : FluentLookAndFeel::FluentColors{};

    if (playlistManager.isEmpty()) {
      g.setColour(colors.textSecondary);
      if (laf != nullptr)
        g.setFont(laf->getBodyLargeFont());
      g.drawFittedText(L"拖入 MIDI 文件，或点击“添加”建立播放列表",
                       listBox.getBounds().reduced(24),
                       juce::Justification::centred, 2);
    }

    if (isFileDragOver) {
      g.setColour(colors.accentPrimary.withAlpha(0.12f));
      g.fillRoundedRectangle(listBox.getBounds().reduced(4).toFloat(), 8.0f);
      g.setColour(colors.accentPrimary);
      g.drawRoundedRectangle(listBox.getBounds().reduced(4).toFloat(), 8.0f,
                             2.0f);
    }

    if (dropInsertIndex >= 0) {
      auto *viewport = listBox.getViewport();
      int scrollY = viewport ? viewport->getViewPositionY() : 0;
      int rowHeight = listBox.getRowHeight();
      int y = listBox.getY() + (dropInsertIndex * rowHeight) - scrollY - 1;

      g.saveState();
      g.reduceClipRegion(listBox.getBounds());

      g.setColour(juce::Colour(0xFF0078D4));
      g.fillRoundedRectangle(listBox.getX() + 10.0f, (float)y,
                             listBox.getWidth() - 20.0f, 3.0f, 1.5f);

      g.fillEllipse(listBox.getX() + 6.0f, (float)y - 2.0f, 7.0f, 7.0f);
      g.fillEllipse(listBox.getRight() - 13.0f, (float)y - 2.0f, 7.0f, 7.0f);

      g.restoreState();
    }
  }

  bool isInterestedInFileDrag(const juce::StringArray &files) override {
    for (auto &f : files)
      if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
        return true;
    return false;
  }

  void fileDragEnter(const juce::StringArray &, int, int) override {
    isFileDragOver = true;
    repaint();
  }

  void fileDragExit(const juce::StringArray &) override {
    isFileDragOver = false;
    repaint();
  }

  void filesDropped(const juce::StringArray &files, int, int) override {
    isFileDragOver = false;
    if (listener) {
      listener->playlistFilesDropped(files);
    } else {
      refresh();
    }
  }

  void resized() override {
    auto area = getLocalBounds();

    auto toolbarArea = area.removeFromTop(48);
    headerLabel.setBounds(toolbarArea.removeFromLeft(140).reduced(12, 0));

    loadBtn.setBounds(toolbarArea.removeFromRight(60).reduced(4));
    saveBtn.setBounds(toolbarArea.removeFromRight(60).reduced(4));
    clearBtn.setBounds(toolbarArea.removeFromRight(60).reduced(4));
    addBtn.setBounds(toolbarArea.removeFromRight(60).reduced(4));

    auto footerArea = area.removeFromBottom(32);
    countLabel.setBounds(footerArea.reduced(12, 0));

    listBox.setBounds(area);
  }

private:
  Listener *getAsyncListener() const {
    // 异步回调中重新获取 listener，避免面板销毁后调用悬空对象。
    auto *component = listenerComponent.getComponent();
    return component != nullptr ? dynamic_cast<Listener *>(component) : nullptr;
  }

  void updateCountLabel() {
    countLabel.setText(juce::String(playlistManager.size()) + L" 个曲目",
                       juce::dontSendNotification);
  }

  void showAddFileDialog() {
    fileChooser = std::make_unique<juce::FileChooser>(
        L"添加 MIDI",
        juce::File::getSpecialLocation(juce::File::userMusicDirectory),
        "*.mid;*.midi");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode |
            juce::FileBrowserComponent::canSelectMultipleItems,
        [safeThis = juce::Component::SafePointer<PlaylistPanel>(this)](
            const juce::FileChooser &fc) {
          if (safeThis == nullptr)
            return;

          juce::StringArray files;
          for (auto &f : fc.getResults())
            files.add(f.getFullPathName());
          if (!files.isEmpty()) {
            if (auto *listener = safeThis->getAsyncListener()) {
              listener->playlistFilesDropped(files);
            } else {
              safeThis->refresh();
            }
          } else if (safeThis != nullptr) {
            safeThis->refresh();
          }
        });
  }

  void clearPlaylist() {
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon, L"清空列表", L"确定要移除所有曲目吗？",
        L"确定", L"取消", this,
        juce::ModalCallbackFunction::create(
            [safeThis = juce::Component::SafePointer<PlaylistPanel>(this)](
                int result) {
          if (safeThis == nullptr)
            return;

          if (result == 1) {
            bool cleared = false;
            if (auto *listener = safeThis->getAsyncListener())
              cleared = listener->playlistClearRequested();
            else {
              safeThis->playlistManager.clear();
              getAppSettings().setLastPlaylistPath("");
              cleared = true;
            }
            if (cleared) {
              safeThis->currentTrackIndex = -1;
              safeThis->refresh();
              if (auto *listener = safeThis->getAsyncListener())
                listener->playlistLoaded(juce::File());
            }
          }
        }));
  }

  void savePlaylist() {
    if (auto *playlistListener = getAsyncListener()) {
      playlistListener->playlistSaveRequested();
      return;
    }

    fileChooser = std::make_unique<juce::FileChooser>(L"保存播放列表",
                                                       juce::File(), "*.json");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::saveMode,
        [safeThis = juce::Component::SafePointer<PlaylistPanel>(this)](
            const juce::FileChooser &fc) {
          if (safeThis == nullptr)
            return;

          auto r = fc.getResult();
          if (r != juce::File()) {
            auto file = r.withFileExtension(".json");
            if (safeThis->playlistManager.save(file))
              getAppSettings().setLastPlaylistPath(file.getFullPathName());
          }
        });
  }

  void loadPlaylist() {
    auto lastPath = getAppSettings().getLastPlaylistPath();
    auto lastFile = juce::File(lastPath);

    if (lastPath.isNotEmpty() && lastFile.existsAsFile()) {
      juce::AlertWindow::showYesNoCancelBox(
          juce::AlertWindow::QuestionIcon, L"加载播放列表",
          L"检测到上次使用的播放列表，是否直接加载？\n" +
              lastFile.getFileName(),
          L"加载最近", L"选择其他", L"取消", this,
          juce::ModalCallbackFunction::create(
              [safeThis = juce::Component::SafePointer<PlaylistPanel>(this),
               lastFile](int result) {
            if (safeThis == nullptr)
              return;
            if (result == 1) {
              safeThis->performLoad(lastFile);
            } else if (result == 2) {
              safeThis->showLoadFileChooser();
            }
          }));
    } else {
      showLoadFileChooser();
    }
  }

  void showLoadFileChooser() {
    fileChooser = std::make_unique<juce::FileChooser>(L"加载播放列表",
                                                      juce::File(), "*.json");
    fileChooser->launchAsync(
        juce::FileBrowserComponent::openMode,
        [safeThis = juce::Component::SafePointer<PlaylistPanel>(this)](
            const juce::FileChooser &fc) {
          if (safeThis == nullptr)
            return;

          auto file = fc.getResult();
          if (file.existsAsFile())
            safeThis->performLoad(file);
        });
  }

  void performLoad(const juce::File &file) {
    bool loaded = false;
    if (auto *asyncListener = getAsyncListener())
      loaded = asyncListener->playlistLoadRequested(file);
    else {
      loaded = playlistManager.load(file);
      if (loaded)
        getAppSettings().setLastPlaylistPath(file.getFullPathName());
    }

    if (loaded) {
      currentTrackIndex = -1;
      refresh();
      if (listener)
        listener->playlistLoaded(file);
    }
  }

  void showContextMenu(int row) {
    juce::PopupMenu m;
    m.addItem(1, L"播放");
    m.addItem(2, L"移除");
    m.addSeparator();
    m.addItem(3, L"打开文件位置");
    m.showMenuAsync(
        {}, [safeThis = juce::Component::SafePointer<PlaylistPanel>(this),
             row](int r) {
      if (safeThis == nullptr)
        return;

      if (r == 1) {
        if (auto *listener = safeThis->getAsyncListener())
          listener->playlistTrackDoubleClicked(row);
      }
      else if (r == 2) {
        safeThis->beginRemoveTrack(row);
      } else if (r == 3) {
        if (auto *listener = safeThis->getAsyncListener())
          listener->playlistTrackRevealRequested(row);
        else if (auto *track = safeThis->playlistManager.getTrack(row))
          track->file.revealToUser();
      }
    });
  }

  void startDropAnimation(int row, bool isAccent) {
    startDropAnimation(std::vector<int>{row}, isAccent);
  }

  void stopDropAnimation() {
    animRows.clear();
    dropAnimProgress = -1.0f;
    listBox.repaint();
  }

  void timerCallback() override {
    bool needsMore = animationState.tick();

    if (dropAnimProgress >= 0.0f) {
      // 两次短脉冲，避免长时间闪烁抢夺注意力。
      constexpr float animDuration = 36.0f;
      dropAnimProgress += 1.0f / animDuration;
      if (dropAnimProgress >= 1.0f)
        stopDropAnimation();
      else
        needsMore = true;
    }

    if (animationState.isRemoveComplete())
      finishPendingRemoval();

    updateVisibleRowTransforms();

    for (int r : animRows)
      listBox.repaintRow(r);

    if (animationState.isAnimating()) {
      listBox.repaint();
      needsMore = true;
    }

    if (!needsMore && dropAnimProgress < 0.0f)
      stopTimer();
  }

  void ensureAnimationTimer() {
    if (!isTimerRunning())
      startTimerHz(60);
  }

  void updateVisibleRowTransforms() {
    for (int row = 0; row < playlistManager.size(); ++row) {
      auto *component = listBox.getComponentForRowNumber(row);
      if (component == nullptr)
        continue;

      const float scale = animationState.getRowScale(row);
      const float offset = animationState.getRowOffset(row);
      component->setAlpha(animationState.getRowAlpha(row));
      component->setTransform(
          juce::AffineTransform::scale(
              1.0f, scale, component->getWidth() * 0.5f,
              component->getHeight() * 0.5f)
              .translated(0.0f, offset));
      component->repaint();
    }
  }

  void beginRemoveTrack(int row) {
    if (pendingRemovalRow >= 0 ||
        !juce::isPositiveAndBelow(row, playlistManager.size()))
      return;

    pendingRemovalRow = row;
    animationState.startRemove(row);
    ensureAnimationTimer();
  }

  void finishPendingRemoval() {
    const int row = pendingRemovalRow;
    if (!juce::isPositiveAndBelow(row, playlistManager.size())) {
      pendingRemovalRow = -1;
      animationState.clearRemoval();
      return;
    }

    const int newCurrentIndex =
        getCurrentTrackIndexAfterRemoval(currentTrackIndex, row);

    bool removed = false;
    if (auto *asyncListener = getAsyncListener())
      removed = asyncListener->playlistTrackRemoveRequested(row, newCurrentIndex);
    else
      removed = playlistManager.removeTrack(row);

    if (!removed) {
      pendingRemovalRow = -1;
      animationState.clearRemoval();
      refresh();
      return;
    }

    currentTrackIndex = newCurrentIndex;
    animationState.clearRemoval();
    animationState.startCurrentTrackTransition(-1, currentTrackIndex);
    refresh();
    animationState.startShiftAfterRemoval(
        row, (float)listBox.getRowHeight());
    pendingRemovalRow = -1;

    if (auto *asyncListener = getAsyncListener())
      asyncListener->playlistTrackReordered(currentTrackIndex);
  }

  PlaylistManager &playlistManager;
  Listener *listener = nullptr;
  juce::Component::SafePointer<juce::Component> listenerComponent;
  juce::ListBox listBox;
  juce::Label headerLabel;
  juce::Label countLabel;
  juce::TextButton addBtn, clearBtn, saveBtn, loadBtn;
  std::unique_ptr<juce::FileChooser> fileChooser;
  int currentTrackIndex = -1;
  int dropInsertIndex = -1;
  int pendingRemovalRow = -1;
  bool isFileDragOver = false;
  PlaylistAnimationState animationState;

  std::vector<int> animRows;
  bool dropAnimIsAccent = false;
  float dropAnimProgress = -1.0f;

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistPanel)
};
