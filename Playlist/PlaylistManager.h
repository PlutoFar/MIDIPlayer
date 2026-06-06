#pragma once

#include <juce_data_structures/juce_data_structures.h>
#include <juce_gui_basics/juce_gui_basics.h>

class PlaylistManager {
public:
  struct Track {
    juce::File file;
    juce::String name;
    double durationSeconds = 0.0;
    bool missing = false;
  };

  struct ChangeLog {
    int added = 0;
    int removed = 0;
    bool reordered = false;

    void reset();
    bool hasChanges() const;
  };

  enum class PlaybackMode {
    Sequential = 1,
    LoopList = 2,
    LoopSingle = 3,
    Shuffle = 4
  };

  bool contains(const juce::File &file) const;
  bool addFile(const juce::File &file, bool allowDuplicates = false);
  bool removeTrack(int index);
  bool moveTrack(int fromIndex, int toIndex);
  void clear();

  const juce::Array<Track> &getTracks() const;
  const Track *getTrack(int index) const;
  int findTrackIndex(const juce::File &file) const;
  bool refreshTrack(int index);

  int size() const;
  bool isEmpty() const;

  bool save(const juce::File &file) const;
  bool load(const juce::File &file);

  void setPlaybackMode(PlaybackMode mode);
  PlaybackMode getPlaybackMode() const;
  int getNextIndex(int currentIndex) const;
  int getPreviousIndex(int currentIndex) const;

  const ChangeLog &getChangeLog() const;
  bool hasChanges() const;
  bool hasMissingFiles() const;
  juce::String getChangeSummary() const;

private:
  juce::Array<Track> tracks;
  PlaybackMode currentMode = PlaybackMode::Sequential;
  mutable ChangeLog changeLog;
};
