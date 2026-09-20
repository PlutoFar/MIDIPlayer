#include "ExportDialog.h"
#include "LegacyExportTask.h"
#include "MainContentComponent.h"

void MainContentComponent::playlistTrackRevealRequested(int index) {
  const auto path = core.trackFileAt(index);
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
      static_cast<int>(core.playlistState().trackNames.size());
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
  for (const auto &name : core.playlistState().trackNames)
    trackNames.add(juce::String(name.c_str()));

  const int initialIndex = core.currentTrackIndex();

  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);
  auto *dlg = new ExportDialog(
      fluentLookAndFeel, trackNames, initialIndex,
      [safeThis](int selectedTrackIdx, const ExportSettings &settings) {
        if (safeThis != nullptr)
          safeThis->chooseExportTarget(selectedTrackIdx, settings);
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

void MainContentComponent::chooseExportTarget(int selectedTrackIndex,
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
      [safeThis, selectedTrackIndex,
       settings](const juce::FileChooser &chooser) {
        if (safeThis == nullptr)
          return;

        auto chosenFile = chooser.getResult();
        if (chosenFile == juce::File{})
          return;

        const auto targetFile =
            normaliseExportTargetFile(chosenFile, settings.formatName);
        if (!exportTargetNeedsOverwriteConfirmation(targetFile)) {
          safeThis->performExport(selectedTrackIndex, settings, targetFile);
          return;
        }

        juce::AlertWindow::showOkCancelBox(
            juce::AlertWindow::WarningIcon, L"替换现有文件？",
            L"目标文件已经存在：\n" + targetFile.getFullPathName(), L"替换",
            L"取消", safeThis.getComponent(),
            juce::ModalCallbackFunction::create([safeThis, selectedTrackIndex,
                                                 settings,
                                                 targetFile](int result) {
              if (result == 1 && safeThis != nullptr)
                safeThis->performExport(selectedTrackIndex, settings,
                                        targetFile);
            }));
      });
}

void MainContentComponent::performExport(int selectedTrackIndex,
                                         const ExportSettings &settings,
                                         const juce::File &targetFile) {
  auto thread = std::make_unique<OfflineExportThread>(
      core, selectedTrackIndex, targetFile, settings, this);
  const bool completed = thread->runThread();
  if (!completed && !thread->exportFailed)
    thread->exportCancelled = true;

  if (thread->exportFailed) {
    auto error = thread->errorMessage;
    if (error.isEmpty())
      error = L"无法创建文件或渲染引擎出现错误。";
    juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                                           L"导出失败", error);
  } else if (thread->exportCancelled) {
    juce::AlertWindow::showMessageBoxAsync(
        juce::AlertWindow::InfoIcon, L"导出已取消", L"未写入目标音频文件。");
  } else if (thread->exportSucceeded) {
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
  if (currentPlaylistFile.existsAsFile()) {
    const bool saved = core.saveList(std::wstring(
        currentPlaylistFile.getFullPathName().toWideCharPointer()));
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
    if (saved) {
      currentPlaylistFile = targetFile;
    } else {
      showOperationError(L"保存播放列表失败", core.lastPlaylistError());
    }
    return saved;
  }
  return false;
}

bool MainContentComponent::registerFileAssociation() {
  return registerMidiFileAssociation();
}

void MainContentComponent::showFileAssociationPrompt() {
#if JUCE_WINDOWS
  if (isFileAssociatedToSelf() || getAppSettings().getDontShowFileAssocPrompt())
    return;

  auto *alertWindow = fluentLookAndFeel.createAlertWindow(
      L"\u6587\u4EF6\u5173\u8054",
      L"\u662F\u5426\u5C06 .mid \u548C .midi \u6587\u4EF6\u5173\u8054\u5230 "
      L"MIDI \u64AD\u653E\u5668\uFF1F\n\n"
      L"\u5173\u8054\u540E\uFF0C\u53CC\u51FB MIDI "
      L"\u6587\u4EF6\u5373\u53EF\u81EA\u52A8\u6253\u5F00\u672C\u5E94\u7528"
      L"\u8FDB\u884C\u64AD\u653E\u3002",
      {}, {}, {}, juce::MessageBoxIconType::QuestionIcon, 0, this);

  alertWindow->addButton(L"\u5173\u8054", 1);
  alertWindow->addButton(L"\u4e0d\u5173\u8054", 0);

  auto dontShowToggle = createDontShowAgainToggle();
  auto *togglePtr = dontShowToggle.get();
  alertWindow->addCustomComponent(dontShowToggle.release());

  auto safeThis = juce::Component::SafePointer<MainContentComponent>(this);

  alertWindow->enterModalState(
      true,
      juce::ModalCallbackFunction::create([safeThis, togglePtr](int result) {
        if (safeThis == nullptr)
          return;

        bool dontShowAgain =
            (togglePtr != nullptr) ? togglePtr->getToggleState() : false;

        if (result == 1) {
          if (safeThis->registerFileAssociation()) {
            getAppSettings().setFileAssociated(true);
            getAppSettings().setDontShowFileAssocPrompt(true);
            getAppSettings().save();
          }
        } else {
          if (dontShowAgain) {
            getAppSettings().setDontShowFileAssocPrompt(true);
            getAppSettings().save();
          }
        }
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
      });

  playlistPanel.refresh();
  playlistPanel.setCurrentTrackIndex(core.currentTrackIndex());

  pendingShellOpen = false;
}
