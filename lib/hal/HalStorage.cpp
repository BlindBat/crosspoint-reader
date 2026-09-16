#include "HalStorage.h"

#include <FS.h>  // need to be included before SdFat.h for compatibility with FS.h's File class
#include <Logging.h>
#include <Memory.h>
#include <SDCardManager.h>
#if FREEINK_CAP_USB_MSC
#include <UsbMassStorage.h>
#endif

#include <cassert>

#define SDCard SDCardManager::getInstance()

namespace {
#if FREEINK_CAP_USB_MSC
freeink::UsbMassStorage usbMassStorage;
#endif
}  // namespace

HalStorage HalStorage::instance;

HalStorage::HalStorage() {
  // Recursive so the same task can re-enter StorageLock without self-deadlock.
  // openFileForRead/Write take the lock and then assign to a HalFile&
  // out-param; if that out-param already held an Impl, its destructor takes
  // the lock again to close the prior FsFile under serialization (see
  // HalFile::Impl::~Impl below). Priority inheritance still applies to
  // recursive mutexes.
  storageMutex = xSemaphoreCreateRecursiveMutex();
  assert(storageMutex != nullptr);
}

// begin() and ready() are only called from setup, no need to acquire mutex for them

bool HalStorage::begin() { return SDCard.begin(); }

bool HalStorage::ready() const { return SDCard.ready(); }

// For the rest of the methods, we acquire the mutex to ensure thread safety

class HalStorage::StorageLock {
 public:
  StorageLock() { xSemaphoreTakeRecursive(HalStorage::getInstance().storageMutex, portMAX_DELAY); }
  ~StorageLock() { xSemaphoreGiveRecursive(HalStorage::getInstance().storageMutex); }
};

void HalStorage::prepareForDeepSleep() {
  StorageLock lock;
  SDCard.shutdown();
}

#if FREEINK_CAP_USB_MSC && !FREEINK_SD_SDMMC
#error "USB Drive requires an SDMMC-backed storage profile"
#endif

bool HalStorage::beginUsbDrive() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  auto* const blockDevice = SDCard.detachFilesystemForRawAccess();
  if (!blockDevice) {
    LOG_ERR("USB", "USB Drive requires a mounted SDMMC filesystem");
    return false;
  }

  if (!usbMassStorage.begin(blockDevice)) {
    LOG_ERR("USB", "USB Drive MSC initialization failed");
    if (!SDCard.begin()) {
      LOG_ERR("USB", "Unable to remount SD card after USB Drive startup failure");
    }
    return false;
  }
  return true;
#else
  return false;
#endif
}

bool HalStorage::disconnectUsbDriveHost() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  return usbMassStorage.disconnectHost();
#else
  return false;
#endif
}

void HalStorage::endUsbDrive() {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  usbMassStorage.end();
#endif
}

UsbDriveState HalStorage::usbDriveState() const {
#if FREEINK_CAP_USB_MSC
  StorageLock lock;
  switch (usbMassStorage.state()) {
    case freeink::UsbMassStorageState::WaitingForHost:
      return UsbDriveState::WaitingForHost;
    case freeink::UsbMassStorageState::Connected:
    case freeink::UsbMassStorageState::Accessed:
      return UsbDriveState::Connected;
    case freeink::UsbMassStorageState::Ejected:
      return UsbDriveState::Ejected;
    case freeink::UsbMassStorageState::Disconnected:
      return UsbDriveState::Disconnected;
    case freeink::UsbMassStorageState::IoError:
      return UsbDriveState::IoError;
    case freeink::UsbMassStorageState::Idle:
      break;
  }
#endif
  return UsbDriveState::Unsupported;
}

#define HAL_STORAGE_WRAPPED_CALL(method, ...) \
  HalStorage::StorageLock lock;               \
  return SDCard.method(__VA_ARGS__);

std::vector<String> HalStorage::listFiles(const char* path, int maxFiles) {
  HAL_STORAGE_WRAPPED_CALL(listFiles, path, maxFiles);
}

String HalStorage::readFile(const char* path) { HAL_STORAGE_WRAPPED_CALL(readFile, path); }

bool HalStorage::readFileToStream(const char* path, Print& out, size_t chunkSize) {
  HAL_STORAGE_WRAPPED_CALL(readFileToStream, path, out, chunkSize);
}

size_t HalStorage::readFileToBuffer(const char* path, char* buffer, size_t bufferSize, size_t maxBytes) {
  HAL_STORAGE_WRAPPED_CALL(readFileToBuffer, path, buffer, bufferSize, maxBytes);
}

size_t HalStorage::fileSize(const char* path) {
  StorageLock lock;
  FsFile file;
  if (!SDCard.openFileForRead("STORAGE", path, file)) {
    return 0;
  }
  const size_t bytes = file.isDirectory() ? 0 : file.fileSize();
  file.close();
  return bytes;
}

bool HalStorage::writeFile(const char* path, const String& content) {
  HAL_STORAGE_WRAPPED_CALL(writeFile, path, content);
}

bool HalStorage::ensureDirectoryExists(const char* path) { HAL_STORAGE_WRAPPED_CALL(ensureDirectoryExists, path); }

class HalFile::Impl {
 public:
  Impl(FsFile&& fsFile) : file(std::move(fsFile)) {}
  // SdFat is not thread-safe; FsFile::close() touches SD/SPI and must run
  // under StorageLock or it races SdSpiCard::m_spiActive across tasks and
  // trips FreeRTOS's xTaskPriorityDisinherit assert. The FsFile member
  // destructor (DESTRUCTOR_CLOSES_FILE=1) will close() again after the lock
  // releases, but close() on an already-closed FsFile is a no-op. See SdFat
  // issue #518 and the HAL note in CLAUDE.md.
  ~Impl() {
    HalStorage::StorageLock lock;
    file.close();
  }
  FsFile file;
};

HalFile::HalFile() = default;
HalFile::HalFile(std::unique_ptr<Impl> impl) : impl(std::move(impl)) {}
HalFile::~HalFile() = default;
HalFile::HalFile(HalFile&&) = default;
HalFile& HalFile::operator=(HalFile&&) = default;

HalFile HalStorage::open(const char* path, const oflag_t oflag) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  auto impl = makeUniqueNoThrow<HalFile::Impl>(SDCard.open(path, oflag));
  if (!impl) {
    LOG_ERR("STORAGE", "OOM: HalFile::Impl");
    return HalFile();
  }
  return HalFile(std::move(impl));
}

bool HalStorage::mkdir(const char* path, const bool pFlag) { HAL_STORAGE_WRAPPED_CALL(mkdir, path, pFlag); }

bool HalStorage::exists(const char* path) { HAL_STORAGE_WRAPPED_CALL(exists, path); }

bool HalStorage::remove(const char* path) { HAL_STORAGE_WRAPPED_CALL(remove, path); }
bool HalStorage::rename(const char* oldPath, const char* newPath) {
  HAL_STORAGE_WRAPPED_CALL(rename, oldPath, newPath);
}

bool HalStorage::rmdir(const char* path) { HAL_STORAGE_WRAPPED_CALL(rmdir, path); }

bool HalStorage::openFileForRead(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile;
  bool ok = SDCard.openFileForRead(moduleName, path, fsFile);
  auto impl = makeUniqueNoThrow<HalFile::Impl>(std::move(fsFile));
  if (!impl) {
    LOG_ERR("STORAGE", "OOM: HalFile::Impl");
    file = HalFile();
    return false;
  }
  file = HalFile(std::move(impl));
  return ok;
}

bool HalStorage::openFileForRead(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForRead(const char* moduleName, const String& path, HalFile& file) {
  return openFileForRead(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const char* path, HalFile& file) {
  StorageLock lock;  // ensure thread safety for the duration of this function
  FsFile fsFile;
  bool ok = SDCard.openFileForWrite(moduleName, path, fsFile);
  auto impl = makeUniqueNoThrow<HalFile::Impl>(std::move(fsFile));
  if (!impl) {
    LOG_ERR("STORAGE", "OOM: HalFile::Impl");
    file = HalFile();
    return false;
  }
  file = HalFile(std::move(impl));
  return ok;
}

bool HalStorage::openFileForWrite(const char* moduleName, const std::string& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::openFileForWrite(const char* moduleName, const String& path, HalFile& file) {
  return openFileForWrite(moduleName, path.c_str(), file);
}

bool HalStorage::removeDir(const char* path) { HAL_STORAGE_WRAPPED_CALL(removeDir, path); }

// HalFile implementation
// Allow doing file operations while ensuring thread safety via HalStorage's mutex.
// Please keep the list below in sync with the HalFile.h header

// A default-constructed or moved-from handle has no Impl; every accessor then
// logs and returns `failValue` instead of dereferencing (no abort on device).
#define HAL_FILE_GUARD(method, failValue)                             \
  if (impl == nullptr) {                                              \
    LOG_ERR("STORAGE", "HalFile::" #method " on an unopened handle"); \
    return failValue;                                                 \
  }

#define HAL_FILE_WRAPPED_CALL(method, failValue, ...) \
  HAL_FILE_GUARD(method, failValue)                   \
  HalStorage::StorageLock lock;                       \
  return impl->file.method(__VA_ARGS__);

#define HAL_FILE_FORWARD_CALL(method, failValue, ...) \
  HAL_FILE_GUARD(method, failValue)                   \
  return impl->file.method(__VA_ARGS__);

void HalFile::flush() {
  if (impl == nullptr) return;
  HalStorage::StorageLock lock;
  impl->file.flush();
}
size_t HalFile::getName(char* name, size_t len) {
  if (name != nullptr && len > 0) name[0] = '\0';
  HAL_FILE_WRAPPED_CALL(getName, 0, name, len);
}
size_t HalFile::size() { HAL_FILE_FORWARD_CALL(size, 0, ); }              // already thread-safe, no need to wrap
size_t HalFile::fileSize() { HAL_FILE_FORWARD_CALL(fileSize, 0, ); }      // already thread-safe, no need to wrap
uint64_t HalFile::fileSize64() { HAL_FILE_FORWARD_CALL(fileSize, 0, ); }  // already thread-safe, no need to wrap
bool HalFile::seek(size_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, false, pos); }
bool HalFile::seek64(uint64_t pos) { HAL_FILE_WRAPPED_CALL(seekSet, false, pos); }
bool HalFile::seekCur(int64_t offset) { HAL_FILE_WRAPPED_CALL(seekCur, false, offset); }
bool HalFile::seekSet(size_t offset) { HAL_FILE_WRAPPED_CALL(seekSet, false, offset); }
int HalFile::available() const { HAL_FILE_WRAPPED_CALL(available, 0, ); }
size_t HalFile::position() const { HAL_FILE_WRAPPED_CALL(position, 0, ); }
int HalFile::read(void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(read, -1, buf, count); }
int HalFile::read() { HAL_FILE_WRAPPED_CALL(read, -1, ); }
size_t HalFile::write(const uint8_t* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, 0, buf, count); }
size_t HalFile::write(const void* buf, size_t count) { HAL_FILE_WRAPPED_CALL(write, 0, buf, count); }
size_t HalFile::write(uint8_t b) { HAL_FILE_WRAPPED_CALL(write, 0, b); }
bool HalFile::rename(const char* newPath) { HAL_FILE_WRAPPED_CALL(rename, false, newPath); }
bool HalFile::isDirectory() const {
  HAL_FILE_FORWARD_CALL(isDirectory, false, );
}  // already thread-safe, no need to wrap
void HalFile::rewindDirectory() {
  if (impl == nullptr) return;
  HalStorage::StorageLock lock;
  impl->file.rewindDirectory();
}
bool HalFile::close() { HAL_FILE_WRAPPED_CALL(close, false, ); }
HalFile HalFile::openNextFile() {
  HAL_FILE_GUARD(openNextFile, HalFile())
  HalStorage::StorageLock lock;
  auto next = makeUniqueNoThrow<Impl>(impl->file.openNextFile());
  if (!next) {
    LOG_ERR("STORAGE", "OOM: HalFile::Impl");
    return HalFile();
  }
  return HalFile(std::move(next));
}
bool HalFile::isOpen() const { return impl != nullptr && impl->file.isOpen(); }  // already thread-safe, no need to wrap
HalFile::operator bool() const { return isOpen(); }
