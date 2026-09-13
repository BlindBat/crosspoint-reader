// Link stub for src/util/DictionaryRegistry.cpp.
//
// The real registry walks the SD card with HalFile directory iteration
// (openNextFile/isDirectory), which the stdio-backed HalFile stub does not
// implement. Dictionary::open() only needs resolveBasePath(), so this stub
// reimplements just that over POSIX dirent, with the same observable contract:
// resolve "<folder>" to "/dictionaries/<folder>/<stem>" when the folder holds
// exactly one .idx stem, and fail otherwise (missing folder, no .idx, or an
// ambiguous folder with several stems). Dictionary::open() re-validates the
// .dict/.dict.dz presence itself, so the registry's data-file check is not
// duplicated here.

#include <HalStorage.h>
#include <dirent.h>

#include <cstring>
#include <string>

#include "src/util/DictionaryRegistry.h"

namespace DictionaryRegistry {

void discover(std::vector<DictionaryEntry>& out) { out.clear(); }

bool resolveBasePath(const char* folderName, std::string& basePathOut) {
  if (!folderName || !*folderName) return false;
  const std::string virtualDir = std::string("/dictionaries/") + folderName;

  DIR* dir = ::opendir(dictstub::mapPath(virtualDir).c_str());
  if (!dir) return false;

  std::string stem;
  bool ambiguous = false;
  while (dirent* entry = ::readdir(dir)) {
    const char* name = entry->d_name;
    const size_t len = std::strlen(name);
    if (len <= 4 || std::strcmp(name + len - 4, ".idx") != 0) continue;
    std::string candidate(name, len - 4);
    if (!stem.empty() && stem != candidate) {
      ambiguous = true;
      break;
    }
    stem = std::move(candidate);
  }
  ::closedir(dir);

  if (stem.empty() || ambiguous) return false;
  basePathOut = virtualDir + "/" + stem;
  return true;
}

}  // namespace DictionaryRegistry
