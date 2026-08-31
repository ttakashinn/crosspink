#include "FirmwareReleaseValidation.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <limits>

namespace firmware_release {

namespace {
constexpr size_t SHA256_HEX_LENGTH = 64;

bool parseNonNegativeInt(const char*& cursor, int& value) {
  if (!cursor || !std::isdigit(static_cast<unsigned char>(*cursor))) return false;
  int parsed = 0;
  do {
    const int digit = *cursor - '0';
    if (parsed > (std::numeric_limits<int>::max() - digit) / 10) return false;
    parsed = parsed * 10 + digit;
    ++cursor;
  } while (std::isdigit(static_cast<unsigned char>(*cursor)));
  value = parsed;
  return true;
}
}  // namespace

bool parseVersion(const char* value, Version& version) {
  version = {};
  if (!value || !*value) return false;
  if (*value == 'v' || *value == 'V') ++value;

  if (!parseNonNegativeInt(value, version.major) || *value++ != '.' || !parseNonNegativeInt(value, version.minor) ||
      *value++ != '.' || !parseNonNegativeInt(value, version.patch)) {
    return false;
  }

  constexpr char VNS_SUFFIX[] = "-vns.";
  if (*value == '\0') return true;
  if (strncmp(value, VNS_SUFFIX, sizeof(VNS_SUFFIX) - 1) != 0) return false;
  value += sizeof(VNS_SUFFIX) - 1;

  if (!parseNonNegativeInt(value, version.vanNhanSoRevision)) return false;
  if (*value == '\0') return true;
  if (*value++ != '.') return false;
  return parseNonNegativeInt(value, version.vanNhanSoPatch) && *value == '\0';
}

bool isNewerVersion(const Version& candidate, const Version& current) {
  if (candidate.major != current.major) return candidate.major > current.major;
  if (candidate.minor != current.minor) return candidate.minor > current.minor;
  if (candidate.patch != current.patch) return candidate.patch > current.patch;
  if (candidate.vanNhanSoRevision != current.vanNhanSoRevision) {
    return candidate.vanNhanSoRevision > current.vanNhanSoRevision;
  }
  return candidate.vanNhanSoPatch > current.vanNhanSoPatch;
}

bool parseSha256Sidecar(const std::string& sidecar, std::string& checksum) {
  checksum.clear();
  size_t start = 0;
  while (start < sidecar.size() && std::isspace(static_cast<unsigned char>(sidecar[start]))) ++start;
  if (sidecar.size() - start < SHA256_HEX_LENGTH) return false;
  for (size_t i = 0; i < SHA256_HEX_LENGTH; ++i) {
    if (!std::isxdigit(static_cast<unsigned char>(sidecar[start + i]))) return false;
  }
  checksum.assign(sidecar, start, SHA256_HEX_LENGTH);
  std::transform(checksum.begin(), checksum.end(), checksum.begin(),
                 [](const unsigned char ch) { return static_cast<char>(std::tolower(ch)); });

  size_t rest = start + SHA256_HEX_LENGTH;
  if (rest == sidecar.size()) return true;
  if (!std::isspace(static_cast<unsigned char>(sidecar[rest]))) return false;
  while (rest < sidecar.size() && std::isspace(static_cast<unsigned char>(sidecar[rest]))) ++rest;
  if (rest == sidecar.size()) return true;
  if (rest < sidecar.size() && sidecar[rest] == '*') ++rest;
  const size_t nameStart = rest;
  while (rest < sidecar.size() && !std::isspace(static_cast<unsigned char>(sidecar[rest]))) ++rest;
  if (nameStart < rest && sidecar.compare(nameStart, rest - nameStart, "firmware.bin") != 0) return false;
  while (rest < sidecar.size() && std::isspace(static_cast<unsigned char>(sidecar[rest]))) ++rest;
  return rest == sidecar.size();
}

}  // namespace firmware_release
