#pragma once

#define LOG_DBG(...) ((void)0)
#define LOG_INF(...) ((void)0)
#define LOG_ERR(...) ((void)0)

// The suite compiles OtaUpdater.cpp with -DCROSSPOINT_VERSION=g_testCurrentVersion
// so isUpdateNewer() can be exercised against a runtime-settable current
// version. This header is included by OtaUpdater.cpp before the first use, so
// the declaration lives here.
extern const char* g_testCurrentVersion;
