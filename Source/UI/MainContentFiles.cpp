#include "ExportDialog.h"
#include "OfflineExportThread.h"
#include "MainContentComponent.h"
#include "SettingsPersistence.h"

#include <map>

void MainContentComponent::playlistTrackRevealRequested(midi::TrackId id) {
  const auto path = core.trackFile(id);
  const juce::File file(juce::String(path.c_str()));
  if (!file.existsAsFile()) {
    showPluginMessage(L"无法打开文件位置", L"曲目文件已移动或不可用。");
    return;
  }
  file.revealToUser();
}

void MainContentComponent::playlistFilesDropped(
    const juce::StringArray &files) {
  juce::StringArray newFiles;
  juce::StringArray duplicateFiles;

  for (auto &f : files) {
    juce::File file(f);
    if (core.findTrackIndex(
            std::wstring(file.getFullPathName().toWideCharPointer())) >= 0) {
      duplicateFiles.add(f);
    } else {
      newFiles.add(f);
    }
  }

  bool overwriteDuplicates = false;

  if (!duplicateFiles.isEmpty()) {
    const int result = juce::AlertWindow::showYesNoCancelBox(
        juce::AlertWindow::QuestionIcon, L"发现重复文件",
        L"检测到 " + juce::String(duplicateFiles.size()) +
            L" 个文件已在列表中。\n\n"
            L"可仅添加新曲目，或同时刷新重复条目。",
        L"仅添加新曲目", L"添加并刷新", L"取消", this);

    if (result == 0)
      return;
    overwriteDuplicates = result == 2;
  }

  std::map<juce::String, int> failureCounts;
  int failedCount = 0;
  const auto recordFailure = [&](const juce::Result &result) {
    ++failedCount;
    ++failureCounts[result.getErrorMessage()];
  };
  const int firstInsertedRow =
      static_cast<int>(core.playlistState().tracks.size());
  int insertedRowCount = 0;
  for (const auto &path : newFiles) {
    const auto result = core.addToPlaylist(path.toWideCharPointer());
    if (result.wasOk())
      ++insertedRowCount;
    else
      recordFailure(result);
  }

  std::vector<int> refreshedRows;
  if (overwriteDuplicates) {
    for (const auto &path : duplicateFiles) {
      const int row = core.findTrackIndex(path.toWideCharPointer());
      const auto result = core.refreshTrack(row);
      if (result.wasOk())
        refreshedRows.push_back(row);
      else
        recordFailure(result);
    }
  }

  if (insertedRowCount > 0)
    playlistPanel.refreshWithInsertedRows(firstInsertedRow, insertedRowCount);
  else if (overwriteDuplicates)
    playlistPanel.refresh(); // 刷新失败也可能改变曲目的可用状态。
  if (!refreshedRows.empty())
    playlistPanel.startDropAnimation(refreshedRows, false);

  juce::StringArray summary;
  if (insertedRowCount > 0)
    summary.add(L"已添加 " + juce::String(insertedRowCount) + L" 首");
  if (!refreshedRows.empty())
    summary.add(L"已刷新 " + juce::String(refreshedRows.size()) + L" 首");
  if (!overwriteDuplicates && !duplicateFiles.isEmpty())
    summary.add(L"已跳过 " + juce::String(duplicateFiles.size()) + L" 首重复曲目");
  if (failedCount > 0)
    summary.add(L"未处理 " + juce::String(failedCount) + L" 首");

  const auto message = summary.joinIntoString(juce::String(L"，"));
  if (failedCount > 0) {
    juce::StringArray reasons;
    for (const auto &[reason, count] : failureCounts)
      reasons.add(juce::String(count) + L" 首 · " + reason);
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::WarningIcon, L"曲目导入结果",
        message + L"\n\n" + reasons.joinIntoString("\n"),
        L"确定", this);
  } else if (message.isNotEmpty()) {
    operationToast.showTopCenter(message, getLocalBounds(), 3000);
  }
}

void MainContentComponent::showExportDialog() {
  auto playlist = core.exportPlaylistState();
  if (playlist.tracks.empty()) {
    exportBtn.setEnabled(false);
    showOperationError(L"无法导出", L"播放列表中没有可导出的 MIDI 文件。");
    return;
  }
  juce::StringArray trackNames;
  for (const auto &track : playlist.tracks)
    trackNames.add(juce::String(track.name.c_str()));

  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  auto *dlg = new ExportDialog(
      fluentLookAndFeel, trackNames, playlist.currentTrackIndex,
      [safeThis, tracks = std::move(playlist.tracks)](
          int selectedTrackIdx, const ExportSettings &settings) {
        if (safeThis != nullptr && juce::isPositiveAndBelow(selectedTrackIdx, tracks.size()))
          safeThis->chooseExportTarget(tracks[static_cast<size_t>(selectedTrackIdx)].path,
                                       settings);
      });

  juce::DialogWindow::LaunchOptions options;
  options.content.setOwned(dlg);
  options.dialogTitle = L"高保真离线导出";
  options.dialogBackgroundColour = fluentLookAndFeel.getColors().background;
  options.escapeKeyTriggersCloseButton = true;
  options.useNativeTitleBar = false;
  options.resizable = false;
  options.componentToCentreAround = this;
  FluentSettingsStyle::launchDialogAsync(options);
}

void MainContentComponent::chooseExportTarget(std::wstring sourcePath,
                                              const ExportSettings &settings) {
  auto exportDir =
      UserSettings::getSettingsDirectory().getChildFile("ExportedAudio");
  exportDir.createDirectory();
  const auto extension = getExportFileExtension(settings.formatName);
  fileChooser = std::make_unique<juce::FileChooser>(
      L"保存音频文件",
      exportDir.getChildFile(settings.title.isNotEmpty()
                                 ? settings.title + extension
                                 : "export" + extension),
      "*" + extension);

  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  fileChooser->launchAsync(
      juce::FileBrowserComponent::saveMode,
      [safeThis, sourcePath = std::move(sourcePath),
       settings](const juce::FileChooser &chooser) {
        if (safeThis == nullptr)
          return;

        auto chosenFile = chooser.getResult();
        if (chosenFile == juce::File{})
          return;

        const auto targetFile =
            normaliseExportTargetFile(chosenFile, settings.formatName);
        if (!exportTargetNeedsOverwriteConfirmation(targetFile)) {
          safeThis->performExport(sourcePath, settings, targetFile);
          return;
        }

        juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::WarningIcon, L"替换现有文件？",
            L"目标文件已经存在：\n" + targetFile.getFullPathName(), L"替换",
            L"取消", safeThis.getComponent(),
            juce::ModalCallbackFunction::create([safeThis, sourcePath,
                                                 settings,
                                                 targetFile](int result) {
              if (result == 1 && safeThis != nullptr)
                safeThis->performExport(sourcePath, settings,
                                        targetFile);
            }));
      });
}

void MainContentComponent::performExport(const std::wstring &sourcePath,
                                         const ExportSettings &settings,
                                         const juce::File &targetFile) {
  auto thread = std::make_unique<OfflineExportThread>(
      core, sourcePath, targetFile, settings, this);
  thread->runThread();

  const auto &outcome = thread->outcome;
  if (outcome.status == ExportStatus::failed) {
    const auto message = outcome.targetWritten
                             ? L"音频已保存到 " + targetFile.getFullPathName() +
                                   L"\n\n" + outcome.error
                             : outcome.error;
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
        outcome.targetWritten ? L"实时音频恢复失败" : L"导出失败", message);
  } else if (outcome.status == ExportStatus::cancelled) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon, L"导出已取消", L"未写入目标音频文件。");
  } else {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon, L"导出完成",
        L"音频文件已保存到:\n" + targetFile.getFullPathName());
  }
}

bool MainContentComponent::hasUnsavedChanges() const {
  return core.state().playlist.hasUnsavedChanges;
}

juce::String MainContentComponent::getPlaylistChangeSummary() const {
  return juce::String(core.state().playlist.changeSummary.c_str());
}

bool MainContentComponent::savePlaylist() {
  const auto path = core.state().playlist.currentListPath;
  if (!path.empty() && juce::File(juce::String(path.c_str())).existsAsFile()) {
    const bool saved = core.saveList(path);
    if (!saved)
      showOperationError(L"保存播放列表失败", core.lastPlaylistError());
    return saved;
  } else {
    return savePlaylistAs();
  }
}

bool MainContentComponent::savePlaylistAs() {
  fileChooser = std::make_unique<juce::FileChooser>(
      L"保存播放列表",
      juce::File(getAppSettings().getLastMidiDirectory())
          .getChildFile("playlist.json"),
      "*.json");

  // Ordering: 关闭流程必须得到保存/取消结果后才能决定是否退出，文件选择使用同步入口。
  if (fileChooser->browseForFileToSave(true)) {
    auto targetFile = fileChooser->getResult().withFileExtension(".json");
    const bool saved = core.saveList(
        std::wstring(targetFile.getFullPathName().toWideCharPointer()));
    if (!saved)
      showOperationError(L"保存播放列表失败", core.lastPlaylistError());
    return saved;
  }
  return false;
}

void MainContentComponent::updateFileAssociation() {
#if JUCE_WINDOWS
  const auto result = registerMidiFileAssociation();
  if (result.failed()) {
    showPluginMessage(L"文件关联失败", result.getErrorMessage());
    return;
  }

  // 已有默认项指向本程序时，更新打开命令即可；其他默认选择交由系统确认。
  if (isMidiFileAssociatedToSelf())
    return;
  if (!juce::URL("ms-settings:defaultapps").launchInDefaultBrowser())
    showPluginMessage(L"无法打开默认应用设置",
                      L"请在 Windows 设置中为 .mid 和 .midi 选择 MIDI 播放器。");
#endif
}

void MainContentComponent::showFileAssociationPrompt() {
#if JUCE_WINDOWS
  if (isMidiFileAssociatedToSelf())
    return;

  auto *alertWindow = fluentLookAndFeel.createAlertWindow(
      L"\u6587\u4EF6\u5173\u8054",
      L".mid 或 .midi 文件尚未关联到当前播放器。是否更新文件关联？",
      {}, {}, {}, juce::MessageBoxIconType::QuestionIcon, 0, this);

  alertWindow->addButton(L"更新关联", 1);
  alertWindow->addButton(L"暂不更新", 0);

  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);

  alertWindow->enterModalState(
      true,
      juce::ModalCallbackFunction::create([safeThis](int result) {
        if (safeThis != nullptr && result == 1)
          safeThis->updateFileAssociation();
      }),
      true);
#endif
}

void MainContentComponent::openMidiFileFromShell(const juce::File &file) {
  showPage("playlist", L"\u97F3\u4E50\u5217\u8868");

  core.openMidiFromShell(
      std::wstring(file.getFullPathName().toWideCharPointer()),
      [safeThis = juce::Component::SafePointer<MainContentComponent>(this)]() {
        if (safeThis != nullptr)
          safeThis->tryLoadLastPluginWithDialog();
      },
      [safeThis = juce::Component::SafePointer<MainContentComponent>(this)](bool succeeded) {
        if (safeThis != nullptr && succeeded) {
          safeThis->playlistPanel.refresh();
          safeThis->playlistPanel.setCurrentTrackIndex(safeThis->core.currentTrackIndex());
        }
      });

  pendingShellOpen = false;
}
