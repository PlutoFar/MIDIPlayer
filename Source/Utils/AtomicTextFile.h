#pragma once

#include <filesystem>
#include <juce_core/juce_core.h>

namespace midi {

// 临时文件位于目标目录。关闭写入流后，以一次重命名发布完整内容。
inline juce::Result writeTextFileAtomically(const juce::File &target,
                                            const juce::String &content) {
  const juce::TemporaryFile temporary(target);
  const auto failure = [&target](const juce::String &operation,
                                  const juce::String &reason) {
    return juce::Result::fail(operation + ": " + target.getFullPathName() +
                              "\n" + reason);
  };

  {
    // 无缓冲写入使短写直接反馈给调用方，flush 单独报告同步到磁盘的错误。
    juce::FileOutputStream stream(temporary.getFile(), 0);
    if (stream.failedToOpen())
      return failure("Unable to open temporary file",
                     stream.getStatus().getErrorMessage());
    if (!stream.write(content.toRawUTF8(), content.getNumBytesAsUTF8()))
      return failure("Unable to write temporary file",
                     stream.getStatus().failed()
                         ? stream.getStatus().getErrorMessage()
                         : "The write did not transfer all bytes");
    stream.flush();
    if (stream.getStatus().failed())
      return failure("Unable to flush temporary file",
                     stream.getStatus().getErrorMessage());
  }

  // JUCE 的 replaceFileIn 在首次保存路径中会先尝试删除目标，再移动临时文件。
  const auto pathOf = [](const juce::File &file) {
    return std::filesystem::u8path(file.getFullPathName().toRawUTF8());
  };
  std::error_code error;
  std::filesystem::rename(pathOf(temporary.getFile()), pathOf(target), error);
  if (error)
    return failure("Unable to replace file", juce::String(error.message()));

  return juce::Result::ok();
}

} // namespace midi
