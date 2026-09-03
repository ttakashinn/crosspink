#pragma once

// Prevents an input edge that opened/revealed a new surface from immediately
// activating that surface. The caller must provide one rendered/ready state
// and one clean frame with no relevant input held before events are accepted.
class InputReleaseGate {
 public:
  void reset() { armed = false; }

  bool acceptsInput(const bool surfaceReady, const bool relevantInputHeld) {
    if (!surfaceReady) return false;
    if (!armed) {
      if (!relevantInputHeld) armed = true;
      return false;
    }
    return true;
  }

 private:
  bool armed = false;
};
