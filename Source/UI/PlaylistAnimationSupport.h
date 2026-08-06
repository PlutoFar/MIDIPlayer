#pragma once

#include <algorithm>
#include <cmath>

inline int getCurrentTrackIndexAfterRemoval(int currentTrackIndex,
                                            int removedRow) {
  if (removedRow == currentTrackIndex)
    return -1;
  if (removedRow >= 0 && removedRow < currentTrackIndex)
    return currentTrackIndex - 1;
  return currentTrackIndex;
}

class PlaylistAnimationState {
public:
  void startInsert(int firstRow, int rowCount) {
    insertFirstRow = firstRow;
    insertRowCount = std::max(0, rowCount);
    insertProgress = insertRowCount > 0 ? 0.0f : 1.0f;
  }

  void startRemove(int row) {
    removingRow = row;
    removeProgress = row >= 0 ? 0.0f : 1.0f;
  }

  void startReorder(int fromRow, int toRow, float height) {
    reorderFromRow = fromRow;
    reorderToRow = toRow;
    rowHeight = height;
    reorderProgress = fromRow != toRow ? 0.0f : 1.0f;
  }

  void clearReorder() {
    reorderFromRow = -1;
    reorderToRow = -1;
    reorderProgress = 1.0f;
  }

  void startShiftAfterRemoval(int firstShiftedRow, float height) {
    removalShiftFirstRow = firstShiftedRow;
    rowHeight = height;
    removalShiftProgress = firstShiftedRow >= 0 ? 0.0f : 1.0f;
  }

  void startCurrentTrackTransition(int previousRow, int currentRow) {
    previousCurrentRow = previousRow;
    currentRowIndex = currentRow;
    currentTransitionProgress =
        previousRow == currentRow ? 1.0f : 0.0f;
  }

  void setCurrentTrackImmediate(int currentRow) {
    previousCurrentRow = -1;
    currentRowIndex = currentRow;
    currentTransitionProgress = 1.0f;
  }

  bool tick() {
    insertProgress = advance(insertProgress, 0.12f);
    removeProgress = advance(removeProgress, 0.14f);
    reorderProgress = advance(reorderProgress, 0.11f);
    removalShiftProgress = advance(removalShiftProgress, 0.12f);
    currentTransitionProgress =
        advance(currentTransitionProgress, 0.14f);
    return isAnimating();
  }

  bool isAnimating() const {
    return insertProgress < 1.0f || removeProgress < 1.0f ||
           reorderProgress < 1.0f || removalShiftProgress < 1.0f ||
           currentTransitionProgress < 1.0f;
  }

  bool isRemoveComplete() const {
    return removingRow >= 0 && removeProgress >= 1.0f;
  }

  int getRemovingRow() const { return removingRow; }

  void clearRemoval() {
    removingRow = -1;
    removeProgress = 1.0f;
  }

  float getRowAlpha(int row) const {
    float alpha = 1.0f;
    if (row >= insertFirstRow &&
        row < insertFirstRow + insertRowCount)
      alpha *= easeOutCubic(insertProgress);
    if (row == removingRow)
      alpha *= 1.0f - easeInCubic(removeProgress);
    return std::clamp(alpha, 0.0f, 1.0f);
  }

  float getRowScale(int row) const {
    if (row >= insertFirstRow &&
        row < insertFirstRow + insertRowCount)
      return 0.92f + 0.08f * easeOutCubic(insertProgress);
    if (row == removingRow)
      return 1.0f - 0.16f * easeInCubic(removeProgress);
    return 1.0f;
  }

  float getRowOffset(int row) const {
    float offset = 0.0f;
    if (removalShiftProgress < 1.0f &&
        row >= removalShiftFirstRow)
      offset += rowHeight *
                (1.0f - easeOutCubic(removalShiftProgress));

    if (reorderProgress >= 1.0f)
      return offset;

    const float remaining = 1.0f - easeOutCubic(reorderProgress);
    if (row == reorderToRow)
      offset += (float)(reorderFromRow - reorderToRow) * rowHeight *
                remaining;
    else if (reorderFromRow < reorderToRow && row >= reorderFromRow &&
             row < reorderToRow)
      offset += rowHeight * remaining;
    else if (reorderFromRow > reorderToRow && row > reorderToRow &&
             row <= reorderFromRow)
      offset -= rowHeight * remaining;

    return offset;
  }

  float getCurrentHighlight(int row) const {
    const float eased = easeOutCubic(currentTransitionProgress);
    if (row == currentRowIndex)
      return eased;
    if (row == previousCurrentRow)
      return 1.0f - eased;
    return 0.0f;
  }

private:
  static float advance(float value, float step) {
    return value < 1.0f ? std::min(1.0f, value + step) : 1.0f;
  }

  static float easeOutCubic(float value) {
    const float inverse = 1.0f - value;
    return 1.0f - inverse * inverse * inverse;
  }

  static float easeInCubic(float value) {
    return value * value * value;
  }

  int insertFirstRow = -1;
  int insertRowCount = 0;
  float insertProgress = 1.0f;

  int removingRow = -1;
  float removeProgress = 1.0f;

  int reorderFromRow = -1;
  int reorderToRow = -1;
  float rowHeight = 0.0f;
  float reorderProgress = 1.0f;

  int removalShiftFirstRow = -1;
  float removalShiftProgress = 1.0f;

  int previousCurrentRow = -1;
  int currentRowIndex = -1;
  float currentTransitionProgress = 1.0f;
};
