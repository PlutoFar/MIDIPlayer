#pragma once

#include "../AudioEngine/AudioDeviceState.h"
#include "State.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace midi {

/**
    Responsibilities: 应用状态、播放命令和插件任务调度；界面只持有快照及命令入口。
    Ownership: 独占音频引擎、设备服务、插件目录和播放列表；界面不得延长内部引用寿命。
    Concurrency: 构造、析构及界面命令在 JUCE 消息线程执行；`scan`、`runExport`
    可在调用方工作线程同步执行。调用方必须在析构前结束这两类外部任务。
    Ordering: 析构取消并等待内部插件任务，再释放设备回调和音频引擎。
    Failures: 业务拒绝通过返回值和错误查询接口报告；异步命令的受理结果与执行结果分开。
*/
class Core {
public:
  // Trust Boundary: `runExport` 在改变播放状态前校验绝对目标路径、曲目索引、编码参数和尾音范围。
  // 采样率单位为整数 Hz；参数默认值仅用于初始化导出界面，不替代无效请求中的值。
  struct ExportRequest {
    int trackIndex = -1;
    std::wstring targetPath;
    std::wstring formatName = L"WAV";
    double sampleRate = 96000.0;
    int bitDepth = 24;
    bool useFloatingPoint = false;
    bool autoTail = true;
    double fixedTailSeconds = 3.0;
    std::wstring title;
    int qualityIndex = 0;
  };

  // Side effect: 读取配置和插件缓存并尝试打开音频设备；设备失败记录在 `state().audio`。
  Core();
  ~Core();

  Core(const Core &) = delete;
  Core &operator=(const Core &) = delete;

  // Postconditions: 返回脱离内部对象寿命的值快照；调用方不得跨多次查询假设状态未变。
  // Concurrency: 状态读取可与核心命令并发；`state` 不复制插件、曲目数组。
  AppState state() const;
  PlaylistState playlistState() const;

  // Preconditions: 在消息线程周期调用；拖动进度条时传入 `uiSuppressTrackAdvance=true`。
  // Postconditions: 同步 MIDI 采样率、推进已结束曲目、停止崩溃插件的播放，按需发起设备重配置。
  void tick(bool uiSuppressTrackAdvance);

  // Concurrency: 同步扫描在调用线程执行，取消回调也在该线程调用。
  // Postconditions: 扫描及缓存替换均成功后发布目录；失败、取消或任务冲突返回 `false`。
  bool scan(std::function<bool()> shouldCancel = {});
  // Postconditions: 返回当前目录副本；目录变更后由界面按需重新读取。
  std::vector<PluginInfo> plugins() const;
  // Preconditions: 消息线程调用；`loadAsync` 的 `id` 来自 `plugins()`，回调自行约束界面对象寿命。
  // Postconditions: `true` 仅表示受理；存活至完成时在消息线程调用 `completion`，参数为执行结果。
  // Ownership: 上次使用的插件属于界面偏好；调用方在成功后保存，并独立处理保存失败。
  // Failures: 线程创建或受理失败返回 `false` 且不回调；任务异常作为失败结果交付，析构取消后不交付回调。
  bool loadAsync(const PluginId &id, std::function<void(bool)> completion);
  bool unloadAsync(std::function<void(bool)> completion);
  bool editorAsync(std::function<void(bool)> completion);
  // Ordering: 插件任务执行中记录关闭请求，待该任务结束后处理；重复请求合并。
  void closeEditor();
  // Postconditions: 下面的插件查询返回独立值；`pluginError` 是进程诊断，`lastPluginError` 包含业务拒绝。
  bool hasPluginLoaded() const;
  std::wstring loadedPluginName() const;
  bool workerCrashed() const;
  std::wstring pluginError() const;
  std::wstring lastPluginError() const;
  // Preconditions: 消息线程调用；只为崩溃进程尝试受理后台回收任务。
  void terminateCrashedWorker();
  // Concurrency: 只置取消标记并唤醒命令等待；不在调用线程销毁进程或共享内存。
  void cancelPendingPluginOperation();

  // Preconditions: 消息线程传入本机 MIDI 路径；完整解析在后台执行，完成回调在消息线程交付。
  // Postconditions: 返回受理结果；成功解析后更新曲目/列表并按插件状态安排播放。
  // Failures: 解析失败保留原曲目，诊断通过 `state().transport.lastError` 查询。
  bool openMidi(const std::wstring &path, std::function<void(bool)> completion = {});
  // Ordering: 无插件时在释放核心锁后调用 `onPluginMissing`；该回调只负责发起加载交互。
  bool openMidiFromShell(const std::wstring &path,
                         std::function<void()> onPluginMissing,
                         std::function<void(bool)> completion = {});
  // Postconditions: 仅替换 MIDI 序列及文件信息；不修改列表索引、不安排自动播放。
  bool loadMidiFile(const std::wstring &path);
  // Preconditions: 播放需要已加载插件及有效序列，切曲需要有效目标列表项；导出或插件音频变更期间不启动播放。
  // Ordering: 暂停、停止及显式播放使旧延迟播放请求失效；`pause` 保留位置，`stop` 回到开头。
  void play();
  void pause();
  void togglePlay();
  void stop();
  void next();
  void prev();
  void playTrackAt(int index);
  // Postconditions: `ratio` 限制在 [0,1] 后转换为采样位置；音频线程在后续块应用最新请求。
  // Failures: 无有效时长或正在导出时保留播放位置。
  void seek(double ratio);
  // Preconditions: `value` 是线性音频增益；调用方负责滑块值的音量曲线换算和设置持久化。
  // Postconditions: 增益限制在 [0,1]；导出期间保留原增益。
  void volume(float value);
  // Postconditions: 索引为当前列表项或 -1；采样率为实时设备配置的 Hz 值。
  int currentTrackIndex() const;
  double sampleRate() const;

  // Preconditions: 文件必须存在且扩展名为 .mid/.midi；索引使用当前列表顺序。
  // Postconditions: 接受的变更同步维护当前索引；批量添加返回新增数量并排除重复项。
  // Failures: 导出期间拒绝修改；无效文件、索引或重复项返回 `false`/0，查询未命中返回 -1。
  bool addToPlaylist(const std::wstring &path);
  int addFilesToPlaylist(const std::vector<std::wstring> &paths);
  bool removeTrack(int index);
  bool moveTrack(int fromIndex, int toIndex);
  bool refreshTrack(int index);
  int findTrackIndex(const std::wstring &path) const;
  bool clearPlaylist();
  // Postconditions: 持久化编号 1..4 对应 `PlaybackMode`；其他编号按当前实现归一为连续播放。
  void setPlayMode(int mode);
  std::wstring trackFileAt(int index) const;
  // Postconditions: 成功保存/加载后更新列表路径；加载只发布完整校验后的列表。
  // Failures: 返回 `false` 并通过 `lastPlaylistError` 提供文件错误；导出期间直接拒绝。
  bool saveList(const std::wstring &path);
  bool loadList(const std::wstring &path);
  std::wstring lastPlaylistError() const;
  // Side effect: 仅设置会话中的列表路径；不读写文件，导出期间保持原值。
  void setCurrentPlaylistFile(const std::wstring &path);
  std::wstring currentPlaylistFile() const;

  // Postconditions: 可用性为最近发布状态；首次运行和设备恢复标记仅描述本次启动。
  bool hasAudioDevice() const;
  bool isFirstRunAudio() const;
  bool wasDeviceRestoredWithFallback() const;
  // Preconditions: 消息线程调用；`rescan=true` 会同步枚举设备。快照不转移设备对象所有权。
  AudioDeviceState audioDeviceState(bool rescan = false);
  // Postconditions: 空字符串表示配置及保存成功；失败返回诊断，设备可能已切换但保存失败。
  // Concurrency: 插件任务、导出或其他设备配置期间拒绝修改；原生控制面板可运行嵌套消息循环。
  juce::String setAudioDriver(const juce::String &name);
  juce::String
  configureAudioDevice(const juce::AudioDeviceManager::AudioDeviceSetup &setup);
  juce::String showAudioControlPanel();
  // Side effect: 向当前设备播放测试音；插件任务或导出期间忽略请求。
  void playTestSound();
  // Ownership: 监听器归调用方所有；须在消息线程注册，并在监听器或 `Core` 析构前移除。
  void addAudioDeviceListener(juce::ChangeListener *listener);
  void removeAudioDeviceListener(juce::ChangeListener *listener);

  // Preconditions: 调用方保证 `Core` 和回调存活至同步调用返回；回调不得直接操作界面控件。
  // Ordering: 独占导出，保存播放位置，渲染临时文件，恢复实时配置及播放状态。
  // Postconditions: 回调在调用线程执行；成功状态要求渲染和恢复均成功。
  // Failures: `Failed` 的诊断由 `lastExportError` 读取；恢复失败时目标音频可能已写入。
  enum class ExportResult { Succeeded, Cancelled, Failed };
  ExportResult runExport(const ExportRequest &request,
                         std::function<void(float)> onProgress = {},
                         std::function<bool()> shouldCancel = {});
  bool isExportActive() const;
  std::wstring lastExportError() const;

private:
  struct Impl;
  std::unique_ptr<Impl> impl;
};

} // namespace midi
