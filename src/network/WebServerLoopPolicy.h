#pragma once

namespace web_server_loop_policy {

// Arduino WebServer::handleClient() delays for 1 ms when its HTTP listener is
// idle. Keep idle bursts bounded so the activity loop can continue servicing
// input, DNS and Wi-Fi health without sacrificing the larger transfer burst.
inline constexpr int IDLE_ITERATIONS = 12;

constexpr int iterations(const bool transferActive, const int transferIterations) {
  return transferActive ? transferIterations : IDLE_ITERATIONS;
}

constexpr bool skipMainLoopDelay(const bool serverRunning, const bool transferActive) {
  return serverRunning && transferActive;
}

}  // namespace web_server_loop_policy
