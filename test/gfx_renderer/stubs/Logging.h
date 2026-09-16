#pragma once

// No-op logging stub. A variadic sink (rather than `((void)0)`) keeps the
// production code's log arguments "used" so -Wall -Wextra stays quiet about
// parameters that only feed logging.
inline void logStubSink(const char*, ...) {}

#define LOG_DBG(...) logStubSink(__VA_ARGS__)
#define LOG_INF(...) logStubSink(__VA_ARGS__)
#define LOG_ERR(...) logStubSink(__VA_ARGS__)
