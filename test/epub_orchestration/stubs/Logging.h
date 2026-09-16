#pragma once

// Host test stub: the firmware logging macros are silenced, but their arguments
// are still consumed so variables that only feed a LOG_DBG do not warn.
template <typename... Ts>
inline void crosspointHostLogSink(const Ts&...) {}

#define LOG_DBG(...) crosspointHostLogSink(__VA_ARGS__)
#define LOG_ERR(...) crosspointHostLogSink(__VA_ARGS__)
#define LOG_INF(...) crosspointHostLogSink(__VA_ARGS__)
#define LOG_WRN(...) crosspointHostLogSink(__VA_ARGS__)
