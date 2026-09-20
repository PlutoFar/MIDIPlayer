#pragma once

// Contract: 消息线程独占提示状态；弹出菜单打开期间优先展示该菜单提示，悬停不能覆盖。
// Postconditions: 关闭菜单只释放菜单优先权，后续悬停事件更新活动提示。

enum class ExportHintTarget {
  none,
  track,
  preset,
  format,
  sampleRate,
  bitDepth,
  quality,
  tailMode,
  tailDuration,
  exportAction,
  cancelAction
};

class ExportHintState {
public:
  void setHoveredTarget(ExportHintTarget target) {
    if (popupTarget == ExportHintTarget::none)
      activeTarget = target;
  }

  void clearHoveredTarget(ExportHintTarget target) {
    if (popupTarget == ExportHintTarget::none && activeTarget == target)
      activeTarget = ExportHintTarget::none;
  }

  void setPopupTarget(ExportHintTarget target, bool isOpen) {
    if (isOpen) {
      popupTarget = target;
      activeTarget = target;
    } else if (popupTarget == target) {
      popupTarget = ExportHintTarget::none;
    }
  }

  ExportHintTarget getActiveTarget() const { return activeTarget; }
  ExportHintTarget getPopupTarget() const { return popupTarget; }

private:
  ExportHintTarget activeTarget = ExportHintTarget::none;
  ExportHintTarget popupTarget = ExportHintTarget::none;
};
