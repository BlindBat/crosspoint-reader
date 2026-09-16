#pragma once

// Shared call log for the Epub/Fb2/Xtc/Txt stand-ins: BookCacheUtils tests
// assert which book class clearBookCache() dispatched to and with what path.

#include <string>
#include <vector>

struct BookStubCall {
  std::string kind;
  std::string path;
  std::string cacheDir;
};

inline std::vector<BookStubCall>& bookStubCalls() {
  static std::vector<BookStubCall> calls;
  return calls;
}
