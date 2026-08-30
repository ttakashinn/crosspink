#pragma once

#include "WifiConnectionPolicy.h"

namespace wifi_connection_diagnostics {

// The event callback is installed once and kept for the firmware lifetime.
// Removing a callback while the single-core ESP32-C3 event task is iterating
// its callback vector can invalidate that iteration, so activities only mark
// the diagnostic window active/inactive.
void beginAttempt();
void endAttempt();
wifi_connection_policy::FailureHint failureHint();

}  // namespace wifi_connection_diagnostics
