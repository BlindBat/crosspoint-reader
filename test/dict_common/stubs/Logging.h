#pragma once

// Host test stub for the firmware logging macros. Unlike the ((void)0) stubs
// used by suites whose production code never logs from otherwise-unused
// variables, this one EVALUATES its arguments through a no-op variadic sink:
// Dictionary.cpp has locals (e.g. buildSidecar's startMs) that are only read
// inside LOG_* calls, and discarding the arguments unevaluated would trip
// -Wunused-variable / -Wunused-but-set-variable under -Wall -Wextra.

template <typename... Args>
inline void dictLogSink(const char*, Args&&...) {}

#define LOG_DBG(...) dictLogSink(__VA_ARGS__)
#define LOG_INF(...) dictLogSink(__VA_ARGS__)
#define LOG_WRN(...) dictLogSink(__VA_ARGS__)
#define LOG_ERR(...) dictLogSink(__VA_ARGS__)
