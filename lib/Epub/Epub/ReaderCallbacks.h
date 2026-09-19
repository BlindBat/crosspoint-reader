#pragma once

#include <cstdint>
#include <memory>

// Page must be complete: the page-complete callbacks pass std::unique_ptr<Page>
// by value, whose destructor needs the full type.
#include "Page.h"

// Callbacks the section builders and their parsers hand to each other: a context
// pointer plus a plain function taking it, never std::function. Each distinct
// std::function signature costs flash, and its closure heap-allocates once it
// outgrows the inline buffer -- neither is affordable on a build that already
// runs against a ~380KB heap. `ctx` belongs to the caller and must outlive the
// object the callback is handed to.

// Fired once when a section is large enough to be worth a "building" popup.
struct BuildPopupFn {
  void (*fn)(void* ctx) = nullptr;
  void* ctx = nullptr;

  explicit operator bool() const { return fn != nullptr; }
  void operator()() const {
    if (fn != nullptr) fn(ctx);
  }
};

// Fired per laid-out EPUB page, carrying the page and its anchor indices.
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

// Fired per laid-out FB2 page. FB2 sections carry no anchor indices.
struct Fb2PageCompleteFn {
  void (*fn)(void* ctx, std::unique_ptr<Page> page) = nullptr;
  void* ctx = nullptr;

  explicit operator bool() const { return fn != nullptr; }
  void operator()(std::unique_ptr<Page> page) const {
    if (fn != nullptr) fn(ctx, std::move(page));
  }
};
