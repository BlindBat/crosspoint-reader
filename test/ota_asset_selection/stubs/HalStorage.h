#pragma once

// Minimal host stand-in: HttpDownloader.h includes <HalStorage.h> for the
// Arduino Stream type used in one fetchUrl overload signature. The OTA suite
// never touches storage.

class Stream {};
