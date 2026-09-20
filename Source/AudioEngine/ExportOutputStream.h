#pragma once
#include <juce_core/juce_core.h>

// Ownership: 编码器拥有适配流，文件流和完成结果归导出任务持有，覆盖编码器析构。
// Reason: 编码器析构仍可能写文件尾/文件头，必须在销毁后检查这些写入的结果。
class ExportOutputStream final : public juce::OutputStream {
public:
  ExportOutputStream(juce::FileOutputStream &fileStream, juce::Result &result)
      : file(fileStream), status(result) {}
  int64_t getPosition() override { return file.getPosition(); }
  bool setPosition(int64_t position) override {
    const bool ok = file.setPosition(position);
    if (!ok)
      recordFailure(L"无法更新音频文件头的位置。");
    return ok;
  }
  bool write(const void *data, size_t bytes) override {
    const bool ok = file.write(data, bytes);
    if (!ok)
      recordFailure(L"音频文件写入未完成。");
    return ok;
  }
  void flush() override {
    file.flush();
    if (file.getStatus().failed())
      recordFailure(file.getStatus().getErrorMessage());
  }
private:
  void recordFailure(const juce::String &message) {
    if (status.wasOk())
      status = juce::Result::fail(file.getStatus().failed()
                                      ? file.getStatus().getErrorMessage() : message);
  }
  juce::FileOutputStream &file;
  juce::Result &status;
};
