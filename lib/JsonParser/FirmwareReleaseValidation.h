#pragma once

#include <string>

namespace firmware_release {

struct Version {
  int major = 0;
  int minor = 0;
  int patch = 0;
  int vanNhanSoRevision = 0;
};

// Accepts exactly X.Y.Z or X.Y.Z-vns.N (with an optional leading v/V).
// Unknown/trailing suffixes fail closed so development and malformed tags are
// never mistaken for a stable public update.
bool parseVersion(const char* value, Version& version);

// Parses the sha256sum formats used by the release workflow:
//   <64 hex>
//   <64 hex>  firmware.bin
//   <64 hex> *firmware.bin
bool parseSha256Sidecar(const std::string& sidecar, std::string& checksum);

}  // namespace firmware_release
