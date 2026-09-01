#pragma once

#include <cstdint>
#include <string>

namespace firmware_release {

enum class Edition : uint8_t {
  UPSTREAM,
  VAN_NHAN_SO,
  CROSSPINK,
};

struct Version {
  int major = 0;
  int minor = 0;
  int patch = 0;
  Edition edition = Edition::UPSTREAM;
  int editionRevision = 0;
  int editionPatch = 0;
};

// Accepts exactly X.Y.Z, X.Y.Z-vns.N, X.Y.Z-vns.N.M, or X.Y.Z-cp.N.M (with
// an optional leading v/V). CrossPink releases always carry both `cp`
// components so the edition version remains unambiguous, e.g. 1.6.0-cp.2.1.
// Unknown/trailing suffixes fail closed so development and malformed tags are
// never mistaken for a stable public update.
bool parseVersion(const char* value, Version& version);

// Compares every stable component in precedence order. Different editions are
// never comparable: OTA must not silently cross from VNS/upstream releases to
// CrossPink (or the reverse). Kept here instead of open-coding the fields in
// OtaUpdater so newly supported version components cannot be ignored.
bool isNewerVersion(const Version& candidate, const Version& current);

// Parses the sha256sum formats used by the release workflow:
//   <64 hex>
//   <64 hex>  firmware.bin
//   <64 hex> *firmware.bin
bool parseSha256Sidecar(const std::string& sidecar, std::string& checksum);

}  // namespace firmware_release
