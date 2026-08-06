#pragma once

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
