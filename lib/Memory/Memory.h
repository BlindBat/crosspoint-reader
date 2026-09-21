#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>

// Nothrow versions of std::make_unique. Return nullptr on allocation failure
// instead of calling abort() (the default when exceptions are disabled on ESP32).
//
// The array overloads check the element count before the new-expression: a count whose
// byte size exceeds the implementation limit makes the new-expression itself throw
// std::bad_array_new_length on libstdc++, before the nothrow allocation function is ever
// reached — which under -fno-exceptions is the abort() these helpers exist to prevent.
//
// Single object:
//   auto obj = makeUniqueNoThrow<PNG>();
//   if (!obj) { LOG_ERR("TAG", "OOM"); return false; }
//
// Array:
//   auto buf = makeUniqueNoThrow<uint8_t[]>(size);
//   if (!buf) { LOG_ERR("TAG", "OOM"); return false; }
//   buf[0] = 0xFF;
//   someApi(buf.get(), size);
//

template <typename T, typename... Args>
  requires(!std::is_array_v<T>)
std::unique_ptr<T> makeUniqueNoThrow(Args&&... args) {
  return std::unique_ptr<T>(new (std::nothrow) T(std::forward<Args>(args)...));
}

template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrow(size_t count) {
  using Elem = std::remove_extent_t<T>;
  if (count > PTRDIFF_MAX / sizeof(Elem)) return nullptr;
  return std::unique_ptr<T>(new (std::nothrow) Elem[count]());
}

// Array without value-initialisation. Same nothrow contract as makeUniqueNoThrow
// (nullptr on allocation failure, never abort()), but the elements are only
// default-initialised, so for scalar types the contents are INDETERMINATE.
//
// Use it only for buffers the caller fills before reading them — a decoder
// scanline that is fully decoded each pass, a packed row that is memset() at the
// top of every row. Anything read before it is written (accumulators updated with
// +=, a "previous row" consumed on the first iteration) must keep makeUniqueNoThrow.
//
//   auto buf = makeUniqueNoThrowForOverwrite<uint8_t[]>(size);
//   if (!buf) { LOG_ERR("TAG", "OOM: %u bytes", size); return false; }
//   decodeInto(buf.get(), size);  // writes every byte before anything reads it
//
template <typename T>
  requires std::is_unbounded_array_v<T>
std::unique_ptr<T> makeUniqueNoThrowForOverwrite(size_t count) {
  using Elem = std::remove_extent_t<T>;
  if (count > PTRDIFF_MAX / sizeof(Elem)) return nullptr;
  return std::unique_ptr<T>(new (std::nothrow) Elem[count]);
}

// Helper struct to call a cleanup function on exit from any scope.
// Use with a lambda to avoid unnecessary allocations from std::function/std::bind:
// Example:
//   auto jpeg = makeUniqueNoThrow<JPEGDEC>();
//   ScopedCleanup cleanup{[&jpeg]{ jpeg->close(); }};
//
template <typename F>
struct [[nodiscard]] ScopedCleanup final {
  const F fn;
  explicit ScopedCleanup(F f) : fn{std::move(f)} {}
  ScopedCleanup(const ScopedCleanup&) = delete;
  ScopedCleanup& operator=(const ScopedCleanup&) = delete;
  ScopedCleanup(ScopedCleanup&&) = delete;
  ScopedCleanup& operator=(ScopedCleanup&&) = delete;
  ~ScopedCleanup() { fn(); }
};

template <typename F>
ScopedCleanup(F) -> ScopedCleanup<F>;
