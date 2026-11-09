#pragma once

class BackgroundSettingsListener {
public:
  virtual ~BackgroundSettingsListener() = default;
  virtual void backgroundSettingsChanged() = 0;
  virtual void dialogMaterialChanged(bool backdropChanged) = 0;
};
