#pragma once

// Host stand-in: records the file-browser navigations ReaderUtils requests.

#include <string>
#include <vector>

class ActivityManager {
 public:
  std::vector<std::string> fileBrowserPaths;
  void goToFileBrowser(std::string path = {}) { fileBrowserPaths.push_back(std::move(path)); }
};
