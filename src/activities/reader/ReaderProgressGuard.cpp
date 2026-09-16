#include "ReaderProgressGuard.h"

#include "ProgressFile.h"

bool ReaderProgressGuard::moved(const int sectionIndex, const int page, const int pageCount) const {
  return sectionIndex != lastSavedSectionIndex || page != lastSavedPage || pageCount != lastSavedPageCount;
}

void ReaderProgressGuard::markSaved(const int sectionIndex, const int page, const int pageCount) {
  lastSavedSectionIndex = sectionIndex;
  lastSavedPage = page;
  lastSavedPageCount = pageCount;
}

bool ReaderProgressGuard::save(const std::string& cachePath, const int sectionIndex, const int page,
                               const int pageCount, const uint8_t* data, const size_t len) {
  if (!moved(sectionIndex, page, pageCount)) {
    return true;
  }
  if (!ProgressFile::writeAtomic(cachePath, data, len)) {
    return false;
  }
  markSaved(sectionIndex, page, pageCount);
  return true;
}
