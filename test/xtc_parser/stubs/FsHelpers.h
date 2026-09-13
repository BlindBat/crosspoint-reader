#pragma once

// XtcParser.cpp includes FsHelpers.h but uses nothing from it on the host;
// the real header drags in Arduino WString.h, so shadow it with an empty shim.
