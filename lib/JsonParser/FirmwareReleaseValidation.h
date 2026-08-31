#pragma once

#include <string>

namespace firmware_release {

struct Version {
  int major = 0;
  int minor = 0;
  int patch = 0;
  int vanNhanSoRevision = 0;
  int vanNhanSoPatch = 0;
};

// Accepts exactly X.Y.Z, X.Y.Z-vns.N or X.Y.Z-vns.N.M (with an optional
// leading v/V). The second VNS component is needed for maintenance releases
// such as vns.7.1; omitted components compare as zero.
// Unknown/trailing suffixes fail closed so development and malformed tags are
// never mistaken for a stable public update.
bool parseVersion(const char* value, Version& version);

// Compares every stable component in precedence order. Kept here instead of
// open-coding the fields in OtaUpdater so newly supported version components
// cannot be accepted by the parser but accidentally ignored by update checks.
bool isNewerVersion(const Version& candidate, const Version& current);

// Parses the sha256sum formats used by the release workflow:
//   <64 hex>
//   <64 hex>  firmware.bin
//   <64 hex> *firmware.bin
bool parseSha256Sidecar(const std::string& sidecar, std::string& checksum);

}  // namespace firmware_release
