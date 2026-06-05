#pragma once

#include "../Playlist/PlaylistManager.h"
#include "../Utils/UserSettings.h"
#include "CustomLookAndFeel.h"
#include <algorithm>
#include <juce_gui_basics/juce_gui_basics.h>
#include <vector>

/**
    PlaylistPanel: Fluent-styled playlist for Windows 11.
*/
class PlaylistPanel : public juce::Component,
                      public juce::ListBoxModel,
                      public juce::FileDragAndDropTarget,
                      public juce::DragAndDropTarget,
                      public juce::Timer {
public:
  class Listener {
  public:
    virtual ~Listener() = default;
    virtual void playlistTrackSelected(int index) = 0;
    virtual void playlistTrackDoubleClicked(int index) = 0;
    virtual void playlistFilesDropped(const juce::StringArray &files) = 0;
    virtual void playlistLoaded() = 0; // 播放列表加载/重置通知
    virtual void
    playlistTrackReordered(int newCurrentIndex) = 0; // 拖拽排序后同步播放索引
  };

  PlaylistPanel(PlaylistManager &pm) : playlistManager(pm) {
    addAndMakeVisible(listBox);
    listBox.setModel(this);
    listBox.setRowHeight(48); // Increased for better readability
    listBox.setColour(juce::ListBox::backgroundColourId,
                      juce::Colours::transparentBlack);
    listBox.setColour(juce::ListBox::outlineColourId,
                      juce::Colours::transparentBlack);
    listBox.setMultipleSelectionEnabled(false);
    listBox.setOpaque(false);

    addAndMakeVisible(headerLabel);
    headerLabel.setText(L"播放列表", juce::dontSendNotification);
    headerLabel.setJustificationType(juce::Justification::centredLeft);
    // Use 黑体 (SimHei) font - different from page title
    headerLabel.setFont(
        juce::Font(juce::FontOptions("SimHei", 16.0f, juce::Font::plain)));
    headerLabel.setColour(juce::Label::textColourId,
                          juce::Colours::white.withAlpha(0.85f));
    headerLabel.setInterceptsMouseClicks(false, false);

    // Toolbar Buttons - Using text labels instead of icons
    addAndMakeVisible(addBtn);
    addBtn.setButtonText(L"添加");
    addBtn.onClick = [this]() { showAddFileDialog(); };

    addAndMakeVisible(clearBtn);
    clearBtn.setButtonText(L"清空");
    clearBtn.onClick = [this]() { clearPlaylist(); };

    addAndMakeVisible(saveBtn);
    saveBtn.setButtonText(L"保存");
    saveBtn.onClick = [this]() { savePlaylist(); };

    addAndMakeVisible(loadBtn);
    loadBtn.setButtonText(L"加载");
    loadBtn.onClick = [this]() { loadPlaylist(); };

    addAndMakeVisible(countLabel);
    countLabel.setColour(juce::Label::textColourId,
                         juce::Colours::white.withAlpha(0.5f));
    countLabel.setInterceptsMouseClicks(false, false);
    updateCountLabel();
  }

  void setListener(Listener *l) { listener = l; }

  void setCurrentTrackIndex(int index) {
    currentTrackIndex = index;
    listBox.repaint();
    listBox.scrollToEnsureRowIsOnscreen(index);
  }

  void refresh() {
    updateRowHeight();
    listBox.updateContent();
    listBox.repaint();
    updateCountLabel();
    repaint(); // Force panel to repaint (clears drag overlays)
  }

  // 启动时自动加载上次的播放列表（静默加载，不弹窗）
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
    // Increase row height generously for touch/ease of use
    // Using 3.0x multiplier to restore the original spacious feeling (e.g. 16px
    // font -> 48px row)
    listBox.setRowHeight((int)(size * 3.0f));
  }

  void deselectAllRows() {
    listBox.deselectAllRows();
    listBox.repaint();
  }

  // 多行闪烁动画（公共接口，供外部调用）
  void startDropAnimation(const std::vector<int> &rows, bool isAccent) {
    animRows = rows;
    dropAnimIsAccent = isAccent;
    dropAnimProgress = 0.0f;
    startTimerHz(45);
  }

  // --- ListBoxModel ---
  int getNumRows() override { return playlistManager.size(); }

  void paintListBoxItem(int row, juce::Graphics &g, int width, int height,
                        bool rowIsSelected) override {
    auto *track = playlistManager.getTrack(row);
    if (track == nullptr)
      return;

    auto area = juce::Rectangle<int>(0, 0, width, height).reduced(6, 2);

    // Item Background (Rounded)
    // Dynamic color access - simplified and safer
    auto *laf = dynamic_cast<FluentLookAndFeel *>(&getLookAndFeel());
    auto colors = laf ? laf->getColors() : FluentLookAndFeel::FluentColors();

    if (row == currentTrackIndex) {
      g.setColour(colors.accentPrimary.withAlpha(0.12f)); // Tint
      g.fillRoundedRectangle(area.toFloat(), 4.0f);

      // Selection indicator
      g.setColour(colors.accentPrimary);
      g.fillRoundedRectangle(area.removeFromLeft(3).reduced(0, 8).toFloat(),
                             1.5f);
    } else if (rowIsSelected) {
      g.setColour(juce::Colour(0x1AFFFFFF)); // Generic hover/select
      g.fillRoundedRectangle(area.toFloat(), 4.0f);
    }

    // 拖拽落地脉冲动画：在正常背景之上叠加一层呼吸高亮（支持多行）
    if (dropAnimProgress >= 0.0f &&
        std::find(animRows.begin(), animRows.end(), row) != animRows.end()) {
      // 3 次 sin 脉冲，progress 从 0→1 对应 3 个完整周期
      float pulse =
          std::sin(dropAnimProgress * juce::MathConstants<float>::twoPi * 3.0f);
      // 将 sin 值映射到 0~1 范围：只取正半波
      float alpha = juce::jmax(0.0f, pulse);
      // 使用主题强调色 or 白色
      auto highlightColor = dropAnimIsAccent
                                ? colors.accentPrimary.withAlpha(alpha * 0.18f)
                                : juce::Colours::white.withAlpha(alpha * 0.12f);
      auto drawArea =
          juce::Rectangle<int>(0, 0, width, height).reduced(6, 2).toFloat();
      g.setColour(highlightColor);
      g.fillRoundedRectangle(drawArea, 4.0f);
    }

    // Index number (left side)
    auto indexArea = area.removeFromLeft(35);
    g.setColour(juce::Colours::white.withAlpha(0.4f));
    float fontSize = getAppSettings().getPlaylistFontSize();
    g.setFont(juce::FontOptions(fontSize * 0.8f)); // Smaller index
    g.drawText(juce::String(row + 1), indexArea,
               juce::Justification::centredRight);

    // Track name (remaining area)
    auto textColour = row == currentTrackIndex
                          ? juce::Colours::white
                          : juce::Colours::white.withAlpha(0.85f);
    if (track->missing)
      textColour = juce::Colours::white.withAlpha(0.45f);
    g.setColour(textColour);

    // Use the configured playlist font
    if (laf)
      g.setFont(laf->getPlaylistFont(fontSize, row == currentTrackIndex));
    else
      g.setFont(juce::FontOptions(fontSize));
    auto trackName =
        track->missing ? track->name + L" [Missing]" : track->name;
    g.drawText(trackName, area.reduced(8, 0),
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
    // Deselect when clicking empty area INSIDE listbox
    deselectAllRows();
  }

  void mouseDown(const juce::MouseEvent &) override {
    // Deselect when clicking empty area of the PANEL itself (header/footer)
    deselectAllRows();
  }

  juce::var
  getDragSourceDescription(const juce::SparseSet<int> &selectedRows) override {
    // 新拖拽开始时取消上一次的落地动画
    if (!animRows.empty())
      stopDropAnimation();

    if (selectedRows.size() == 1)
      return "trackIdx:" + juce::String(selectedRows[0]);
    return {};
  }

  // --- DragAndDropTarget ---
  bool isInterestedInDragSource(
      const juce::DragAndDropTarget::SourceDetails &) override {
    return true;
  }

  void
  itemDragMove(const juce::DragAndDropTarget::SourceDetails &details) override {
    // Calculate which row the mouse is over - convert from this coordinates to
    // listBox coordinates
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
    // Calculate final insert index - convert from this coordinates to listBox
    // coordinates
    auto localPoint = listBox.getLocalPoint(this, details.localPosition);
    auto *viewport = listBox.getViewport();
    int scrollY = viewport ? viewport->getViewPositionY() : 0;
    int rowHeight = listBox.getRowHeight();

    int insertIndex =
        juce::jlimit(0, playlistManager.size(),
                     (localPoint.y + scrollY + rowHeight / 2) / rowHeight);

    // Parse source row from drag description
    auto desc = details.description.toString();
    if (desc.startsWith("trackIdx:")) {
      int srcRow = desc.substring(9).getIntValue();
      if (srcRow >= 0 && srcRow < playlistManager.size() &&
          srcRow != insertIndex) {
        // Calculate adjusted target index
        int targetRow = insertIndex;
        if (srcRow < insertIndex)
          targetRow--;

        if (targetRow != srcRow && targetRow >= 0) {
          // 记录是否拖拽的是正在播放的曲目（在索引更新之前判断）
          bool movedPlayingTrack = (srcRow == currentTrackIndex);

          // Fix: Only update currentTrackIndex if the playing track itself was
          // moved, or if the move affected the playing track's position.
          if (srcRow == currentTrackIndex) {
            currentTrackIndex = targetRow;
          } else if (srcRow < currentTrackIndex &&
                     targetRow >= currentTrackIndex) {
            currentTrackIndex--;
          } else if (srcRow > currentTrackIndex &&
                     targetRow <= currentTrackIndex) {
            currentTrackIndex++;
          }

          playlistManager.moveTrack(srcRow, targetRow);

          // 选中高亮跟随拖拽曲目到新位置
          listBox.selectRow(targetRow);

          // 启动落地脉冲动画
          startDropAnimation(targetRow, movedPlayingTrack);
        }
      }
    }

    // Ensure UI is fully refreshed and indicators are cleared
    dropInsertIndex = -1;
    refresh();
    repaint();
    // 通知 MainContentComponent 同步播放索引
    if (listener)
      listener->playlistTrackReordered(currentTrackIndex);
  }

  // Draw drop indicator line
  void paintOverChildren(juce::Graphics &g) override {
    if (dropInsertIndex >= 0) {
      auto *viewport = listBox.getViewport();
      int scrollY = viewport ? viewport->getViewPositionY() : 0;
      int rowHeight = listBox.getRowHeight();
      int y = listBox.getY() + (dropInsertIndex * rowHeight) - scrollY - 1;

      // Only draw if within listbox bounds (avoid drawing over header)
      g.saveState();
      g.reduceClipRegion(listBox.getBounds());

      // Draw insertion line
      g.setColour(juce::Colour(0xFF0078D4)); // Accent blue
      g.fillRoundedRectangle(listBox.getX() + 10.0f, (float)y,
                             listBox.getWidth() - 20.0f, 3.0f, 1.5f);

      // Draw small circles at ends
      g.fillEllipse(listBox.getX() + 6.0f, (float)y - 2.0f, 7.0f, 7.0f);
      g.fillEllipse(listBox.getRight() - 13.0f, (float)y - 2.0f, 7.0f, 7.0f);

      g.restoreState();
    }
  }

  // --- FileDragAndDropTarget ---
  bool isInterestedInFileDrag(const juce::StringArray &files) override {
    for (auto &f : files)
      if (f.endsWithIgnoreCase(".mid") || f.endsWithIgnoreCase(".midi"))
        return true;
    return false;
  }

  void filesDropped(const juce::StringArray &files, int, int) override {
    if (listener)
      listener->playlistFilesDropped(files);
    refresh();
  }

  void resized() override {
    auto area = getLocalBounds();

    auto toolbarArea = area.removeFromTop(48);
    // Increase width to prevent horizontal squeezing of text
    headerLabel.setBounds(toolbarArea.removeFromLeft(140).reduced(12, 0));

    // Wider buttons for text labels
    loadBtn.setBounds(toolbarArea.removeFromRight(60).reduced(4));
    saveBtn.setBounds(toolbarArea.removeFromRight(60).reduced(4));
    clearBtn.setBounds(toolbarArea.removeFromRight(60).reduced(4));
    addBtn.setBounds(toolbarArea.removeFromRight(60).reduced(4));

    auto footerArea = area.removeFromBottom(32);
    countLabel.setBounds(footerArea.reduced(12, 0));

    listBox.setBounds(area);
  }

private:
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
          if (!files.isEmpty() && safeThis->listener)
            safeThis->listener->playlistFilesDropped(files);
          safeThis->refresh();
        });
  }

  void clearPlaylist() {
    // 使用异步对话框避免阻塞消息循环
    juce::AlertWindow::showOkCancelBox(
        juce::AlertWindow::QuestionIcon, L"清空列表", L"确定要移除所有曲目吗？",
        L"确定", L"取消", this,
        juce::ModalCallbackFunction::create(
            [safeThis = juce::Component::SafePointer<PlaylistPanel>(this)](
                int result) {
          if (result == 1 && safeThis != nullptr) {
            safeThis->playlistManager.clear();
            safeThis->currentTrackIndex = -1;
            // 清除上次加载的播放列表路径，防止下次启动时自动加载旧文件
            getAppSettings().setLastPlaylistPath("");
            safeThis->refresh();
            // 通知 MainContentComponent 重置播放索引
            if (safeThis->listener)
              safeThis->listener->playlistLoaded();
          }
        }));
  }

  void savePlaylist() {
    fileChooser = std::make_unique<juce::FileChooser>(L"保存播放列表",
                                                      juce::File(), "*.json");
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode,
                             [safeThis = juce::Component::SafePointer<
                                  PlaylistPanel>(this)](
                                 const juce::FileChooser &fc) {
                               if (safeThis == nullptr)
                                 return;
                               auto r = fc.getResult();
                               if (r != juce::File()) {
                                 auto file = r.withFileExtension(".json");
                                 if (safeThis->playlistManager.save(file)) {
                                   getAppSettings().setLastPlaylistPath(
                                       file.getFullPathName());
                                 }
                               }
                             });
  }

  void loadPlaylist() {
    auto lastPath = getAppSettings().getLastPlaylistPath();
    auto lastFile = juce::File(lastPath);

    if (lastPath.isNotEmpty() && lastFile.existsAsFile()) {
      // 弹出提示框: 是=加载最近, 否=正常打开, 取消=取消行为
      juce::AlertWindow::showYesNoCancelBox(
          juce::AlertWindow::QuestionIcon, L"加载播放列表",
          L"检测到上次使用的播放列表，是否直接加载？\n" +
              lastFile.getFileName(),
          L"加载最近", L"选择其他", L"取消", this,
          juce::ModalCallbackFunction::create(
              [safeThis = juce::Component::SafePointer<PlaylistPanel>(this),
               lastFile](int result) {
            if (result == 1) { // 是 (加载最近)
              if (safeThis != nullptr)
                safeThis->performLoad(lastFile);
            } else if (result == 2) { // 否 (选择其他)
              if (safeThis != nullptr)
                safeThis->showLoadFileChooser();
            }
            // result == 0 (取消) -> 不执行任何操作
          }));
    } else {
      showLoadFileChooser();
    }
  }

  void showLoadFileChooser() {
    fileChooser = std::make_unique<juce::FileChooser>(L"加载播放列表",
                                                      juce::File(), "*.json");
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode,
                             [safeThis = juce::Component::SafePointer<
                                  PlaylistPanel>(this)](
                                 const juce::FileChooser &fc) {
                               if (safeThis == nullptr)
                                 return;
                               auto file = fc.getResult();
                               if (file.existsAsFile()) {
                                 safeThis->performLoad(file);
                               }
                             });
  }

  void performLoad(const juce::File &file) {
    if (playlistManager.load(file)) {
      getAppSettings().setLastPlaylistPath(file.getFullPathName());
      currentTrackIndex = -1;
      refresh();
      if (playlistManager.hasMissingFiles()) {
        juce::AlertWindow::showMessageBoxAsync(
            juce::AlertWindow::WarningIcon, L"播放列表包含缺失文件",
            L"已加载播放列表，但其中部分 MIDI 文件当前不存在。\n"
            L"这些条目已被保留，并在列表中标记为 Missing。");
      }
      // 通知 MainContentComponent 重置播放索引
      if (listener)
        listener->playlistLoaded();
    }
  }

  void showContextMenu(int row) {
    juce::PopupMenu m;
    m.addItem(1, L"播放");
    m.addItem(2, L"移除");
    m.addSeparator();
    m.addItem(3, L"打开文件位置");
    m.showMenuAsync({}, [this, row](int r) {
      if (r == 1 && listener)
        listener->playlistTrackDoubleClicked(row);
      else if (r == 2) {
        // 更新播放高亮索引：
        // - 如果移除的是正在播放的曲目，重置高亮
        // - 如果移除的是播放曲目之前的曲目，索引需要减1以保持正确位置
        if (row == currentTrackIndex) {
          currentTrackIndex = -1; // 移除正在播放的曲目，清除高亮
        } else if (row < currentTrackIndex && currentTrackIndex > 0) {
          currentTrackIndex--; // 移除前面的曲目，索引前移
        }
        playlistManager.removeTrack(row);
        refresh();
        // 同步播放索引到 MainContentComponent，防止切歌错位
        if (listener)
          listener->playlistTrackReordered(currentTrackIndex);
      } else if (r == 3)
        if (auto *t = playlistManager.getTrack(row))
          t->file.revealToUser();
    });
  }

  // --- 拖拽落地动画（内部单行重载）---
  void startDropAnimation(int row, bool isAccent) {
    startDropAnimation(std::vector<int>{row}, isAccent);
  }

  void stopDropAnimation() {
    stopTimer();
    animRows.clear();
    dropAnimProgress = -1.0f;
    listBox.repaint();
  }

  void timerCallback() override {
    // 总时长约 1.2 秒（~54 帧 @ 45fps）
    constexpr float animDuration = 54.0f;
    dropAnimProgress += 1.0f / animDuration;

    if (dropAnimProgress >= 1.0f) {
      stopDropAnimation();
      return;
    }

    // 只重绘动画所在行，避免全组件重绘
    for (int r : animRows)
      listBox.repaintRow(r);
  }

  PlaylistManager &playlistManager;
  Listener *listener = nullptr;
  juce::ListBox listBox;
  juce::Label headerLabel;
  juce::Label countLabel;
  juce::TextButton addBtn, clearBtn, saveBtn, loadBtn;
  std::unique_ptr<juce::FileChooser> fileChooser;
  int currentTrackIndex = -1;
  int dropInsertIndex = -1; // For drag-drop visual feedback

  // 脉冲动画状态（支持多行同时闪烁）
  std::vector<int> animRows;      // 正在动画的行号列表
  bool dropAnimIsAccent = false;  // 是否使用强调色（正在播放的曲目）
  float dropAnimProgress = -1.0f; // 动画进度 0.0~1.0

  JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(PlaylistPanel)
};
