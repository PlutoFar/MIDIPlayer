#include "BackgroundHistory.h"
#include "BackgroundEffectSupport.h"
#include "BackgroundHistorySupport.h"

BackgroundHistory::BackgroundHistory(juce::File location)
    : directory(std::move(location)) {}

juce::Array<juce::File> BackgroundHistory::sortedFiles() const {
  auto files = directory.findChildFiles(juce::File::findFiles, false, "bg_*.png");
  std::sort(files.begin(), files.end(), [](const auto &first, const auto &second) {
    return first.getLastModificationTime() > second.getLastModificationTime();
  });
  return files;
}

juce::Array<juce::File> BackgroundHistory::recentImages() const {
  return getUniqueRecentBackgroundFiles(sortedFiles(), 10);
}

juce::Result BackgroundHistory::store(const juce::Image &image,
                                      juce::File &storedFile) const {
  if (const auto created = directory.createDirectory(); created.failed())
    return created;
  const auto target = directory.getNonexistentChildFile(
      "bg_" + juce::String(juce::Time::currentTimeMillis()), ".png");
  auto files = sortedFiles();
  // 导入期间的临时文件使用独立扩展名，目录扫描只处理已提交的 PNG。
  juce::TemporaryFile temporary(
      target, directory.getNonexistentChildFile("background-import", ".tmp"));
  {
    juce::FileOutputStream stream(temporary.getFile());
    if (stream.failedToOpen())
      return stream.getStatus();
    const bool encoded = juce::PNGImageFormat().writeImageToStream(
        prepareBackgroundImage(image), stream);
    stream.flush();
    if (stream.getStatus().failed())
      return stream.getStatus();
    if (!encoded)
      return juce::Result::fail(L"无法编码背景图片。");
  }

  storedFile = target;
  for (const auto &file : files) {
    if (file.hasIdenticalContentTo(temporary.getFile())) {
      storedFile = file;
      storedFile.setLastModificationTime(juce::Time::getCurrentTime());
      break;
    }
  }
  if (storedFile == target && !temporary.overwriteTargetFileWithTemporary())
    return juce::Result::fail(L"无法保存背景图片。");

  files.removeAllInstancesOf(storedFile);
  files.insert(0, storedFile);
  const auto retained = getUniqueRecentBackgroundFiles(files, 5);
  for (const auto &file : files)
    if (!retained.contains(file) && !file.deleteFile())
      juce::Logger::writeToLog(L"无法清理背景历史: " + file.getFullPathName());
  return juce::Result::ok();
}

juce::Result BackgroundHistory::remove(const juce::File &file,
                                       const juce::File &activeFile) const {
  if (!canRemoveRecentBackgroundCache(directory, activeFile, file))
    return juce::Result::fail(L"只能移除未使用的背景历史图片。");
  if (!file.deleteFile())
    return juce::Result::fail(L"背景缓存文件正在使用或没有删除权限。");
  return juce::Result::ok();
}
