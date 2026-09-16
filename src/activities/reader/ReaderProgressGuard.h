#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

// Writes a reader's progress.bin, skipping the write when the position has not
// moved since the last successful save.
//
// renderBook() runs on every repaint, not only on a page turn: a popup
// dismissal, a settings change or a status-bar redraw re-renders the same page.
// Each save is a temp-file write plus a remove and a rename on the SD card
// (ProgressFile::writeAtomic), all under storageMutex.
//
// Same guard EpubReaderActivity keeps inline around its saveProgress() call
// (lastSavedSpineIndex / lastSavedPage / lastSavedPageCount); the TXT, XTC and
// FB2 readers share this one.
class ReaderProgressGuard {
  // -1 means "nothing written yet", so the first save always runs.
  int lastSavedSectionIndex = -1;
  int lastSavedPage = -1;
  int lastSavedPageCount = -1;

  bool moved(int sectionIndex, int page, int pageCount) const;

 public:
  // Writes len bytes to <cachePath>/progress.bin unless the position is
  // unchanged. Returns false only when a needed write failed, so a caller can
  // log it; a skipped write reports success because the bytes are already
  // there. A failed write leaves the guard untouched, so the next repaint
  // retries.
  bool save(const std::string& cachePath, int sectionIndex, int page, int pageCount, const uint8_t* data, size_t len);

  // Single-section readers (TXT, XTC) store only a page number.
  bool save(const std::string& cachePath, const int page, const uint8_t* data, const size_t len) {
    return save(cachePath, 0, page, 0, data, len);
  }

  // The position progress.bin was just read at: the first render must not
  // write it straight back.
  void markSaved(int sectionIndex, int page, int pageCount);
  void markSaved(const int page) { markSaved(0, page, 0); }

  // After progress.bin was deleted (cache clear): the next save must run.
  void forget() { markSaved(-1, -1, -1); }
};
