#include "ExportDialog.h"
#include "OfflineExportThread.h"
#include "MainContentComponent.h"
#include "SettingsPersistence.h"

void MainContentComponent::playlistTrackRevealRequested(midi::TrackId id) {
  const auto path = core.trackFile(id);
  juce::File file(juce::String(path.c_str()));
  if (file.existsAsFile())
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

  enum class DupAction { AddNewOnly, OverwriteExisting, Cancel };
  DupAction action = DupAction::AddNewOnly;

  if (!duplicateFiles.isEmpty()) {
    int result = juce::AlertWindow::showYesNoCancelBox(
        juce::AlertWindow::QuestionIcon, L"发现重复文件",
        L"检测到 " + juce::String(duplicateFiles.size()) +
            L" 个文件已在列表中。\n\n"
            L"「仅保存新的」= 跳过重复，只添加新文件\n"
            L"「保存并覆盖」= 添加新文件，并刷新已有条目",
        L"仅保存新的", L"保存并覆盖", L"取消");

    if (result == 0)
      return;
    if (result == 1)
      action = DupAction::AddNewOnly;
    else if (result == 2)
      action = DupAction::OverwriteExisting;
  }

  bool anythingChanged = false;
  const int firstInsertedRow =
      static_cast<int>(core.playlistState().tracks.size());
  int insertedRowCount = 0;
  for (const auto &f : newFiles) {
    const juce::File file(f);
    if (core.addToPlaylist(
            std::wstring(file.getFullPathName().toWideCharPointer()))) {
      anythingChanged = true;
      ++insertedRowCount;
    }
  }

  std::vector<int> overwrittenRows;
  if (action == DupAction::OverwriteExisting) {
    for (const auto &f : duplicateFiles) {
      const juce::File file(f);
      const int idx = core.findTrackIndex(
          std::wstring(file.getFullPathName().toWideCharPointer()));
      if (idx >= 0) {
        core.refreshTrack(idx);
        overwrittenRows.push_back(idx);
        anythingChanged = true;
      }
    }
  }

  if (anythingChanged) {
    if (insertedRowCount > 0)
      playlistPanel.refreshWithInsertedRows(firstInsertedRow, insertedRowCount);
    else
      playlistPanel.refresh();

    if (!overwrittenRows.empty()) {
      playlistPanel.startDropAnimation(overwrittenRows, false);
    }
  }
}

void MainContentComponent::showExportDialog() {
  juce::StringArray trackNames;
  auto playlist = core.playlistState();
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
