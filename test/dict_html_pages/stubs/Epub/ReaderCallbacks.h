#pragma once

// Host stub for lib/Epub/Epub/ReaderCallbacks.h, mirroring the two callback
// shapes DictHtmlPages.cpp uses. Bound to this suite's stub Page (see Page.h),
// which is why it is not included from the real header.

#include <cstddef>
#include <cstdint>
#include <memory>

#include "Page.h"

struct BuildPopupFn {
  void (*fn)(void* ctx) = nullptr;
  void* ctx = nullptr;

  explicit operator bool() const { return fn != nullptr; }
  void operator()() const {
    if (fn != nullptr) fn(ctx);
  }
};

struct EpubPageCompleteFn {
  void (*fn)(void* ctx, std::unique_ptr<Page> page, uint16_t paragraphIndex, uint16_t listItemIndex,
             uint32_t visibleTextOffset) = nullptr;
  void* ctx = nullptr;

  explicit operator bool() const { return fn != nullptr; }
  void operator()(std::unique_ptr<Page> page, const uint16_t paragraphIndex, const uint16_t listItemIndex,
                  const uint32_t visibleTextOffset) const {
    if (fn != nullptr) fn(ctx, std::move(page), paragraphIndex, listItemIndex, visibleTextOffset);
  }
};
