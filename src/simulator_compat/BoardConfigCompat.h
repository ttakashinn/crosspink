#pragma once

// The render lab intentionally pins a simulator revision for deterministic
// screenshots. That revision predates the X4 Classic board enum/API added to
// the device SDK. None of the pinned simulator profiles model X4 Classic, so a
// false compatibility answer preserves their behavior while allowing current
// shared application code to compile. Remove this shim when the simulator pin
// is advanced to a revision that defines FREEINK_DEVICE_X4CLASSIC.
#if defined(__cplusplus) && __has_include(<BoardConfig.h>)
#include <BoardConfig.h>

#ifndef FREEINK_DEVICE_X4CLASSIC
namespace BoardConfig {
inline bool isX4Classic() { return false; }
}  // namespace BoardConfig
#endif
#endif
