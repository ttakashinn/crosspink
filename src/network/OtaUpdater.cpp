#include "OtaUpdater.h"

// clang-format off
// HttpDownloader.h pulls Arduino/SdFat, whose macros collide with lwip's
// ip4_addr.h unless seen first. Pin this order; clang-format would otherwise sort
// the local header last and break the build.
#include "HttpDownloader.h"
#include <Logging.h>
#include <FirmwareReleaseValidation.h>
#include <ReleaseJsonParser.h>
#include <esp_ota_ops.h>
#include <esp_wifi.h>
#include <mbedtls/sha256.h>
// clang-format on

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>

#include "FirmwareBoardTag.h"
#include "FirmwareFlasher.h"

namespace {
constexpr char latestReleaseUrl[] = "https://api.github.com/repos/ttakashinn/crosspoint-reader/releases/latest";
constexpr char trustedAssetPrefix[] = "https://github.com/ttakashinn/crosspoint-reader/releases/download/";
constexpr char OTA_TEMP_PATH[] = "/.crosspoint-ota.tmp";
constexpr size_t SHA256_HEX_LENGTH = 64;

bool isTrustedReleaseAssetUrl(const std::string& url) { return url.rfind(trustedAssetPrefix, 0) == 0; }

bool verifyFileSha256(const char* path, const std::string& expectedChecksum) {
  HalFile file;
  if (!Storage.openFileForRead("OTA", path, file)) return false;

  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  mbedtls_sha256_starts(&context, 0);
  uint8_t buffer[4096];
  size_t remaining = file.fileSize();
  bool readOk = true;
  while (remaining > 0) {
    const size_t wanted = std::min(remaining, sizeof(buffer));
    if (file.read(buffer, wanted) != static_cast<int>(wanted)) {
      readOk = false;
      break;
    }
    mbedtls_sha256_update(&context, buffer, wanted);
    remaining -= wanted;
  }
  file.close();

  uint8_t digest[32];
  mbedtls_sha256_finish(&context, digest);
  mbedtls_sha256_free(&context);
  if (!readOk || expectedChecksum.size() != SHA256_HEX_LENGTH) return false;

  char actual[SHA256_HEX_LENGTH + 1];
  for (size_t i = 0; i < sizeof(digest); ++i) snprintf(actual + i * 2, 3, "%02x", digest[i]);
  actual[SHA256_HEX_LENGTH] = '\0';
  return expectedChecksum == actual;
}
}  // namespace

OtaUpdater::OtaUpdaterError OtaUpdater::checkForUpdate() {
  LOG_DBG("OTA", "Checking for update (current: %s)", CROSSPOINT_VERSION);

  // A failed re-check must not leave a previously discovered release usable.
  updateAvailable = false;
  latestVersion.clear();
  otaUrl.clear();
  checksumUrl.clear();
  otaSize = 0;
  processedSize = 0;
  totalSize = 0;
  installPhase = InstallPhase::IDLE;

  // Stream the ~32KB release JSON straight into the parser as it arrives.
  // Buffering the whole body in a std::string would add a growing allocation
  // on top of the TLS session's heap during the fetch; with -fno-exceptions an
  // OOM there aborts. fetchUrl handles the verified-https GET, redirects, and
  // User-Agent (see HttpDownloader).
  ReleaseJsonParser releaseParser;
  // Each board updates from its own release asset: plain firmware.bin for the
  // combined C3 X4/X3 binary, firmware-<board>.bin otherwise.
  const bool isX4 = board_tag::boardNameLen() == 2 && memcmp(board_tag::boardName(), "x4", 2) == 0;
  char assetName[48] = "firmware.bin";
  if (!isX4) {
    snprintf(assetName, sizeof(assetName), "firmware-%.*s.bin", static_cast<int>(board_tag::boardNameLen()),
             board_tag::boardName());
  }
  releaseParser.setFirmwareAssetName(assetName);
  const bool ok = HttpDownloader::fetchUrl(
      latestReleaseUrl,
      [&releaseParser](const uint8_t* data, size_t len) {
        releaseParser.feed(reinterpret_cast<const char*>(data), len);
        return true;
      },
      "", "", HttpDownloader::TransportSecurity::VERIFIED_TLS);
  if (!ok) {
    LOG_ERR("OTA", "Release check fetch failed");
    return HTTP_ERROR;
  }
  if (!releaseParser.isComplete()) {
    LOG_ERR("OTA", "Release JSON is malformed or incomplete");
    return JSON_PARSE_ERROR;
  }

  LOG_DBG("OTA", "Parser results: tag=%s firmware=%s", releaseParser.foundTag() ? "yes" : "no",
          releaseParser.foundFirmware() ? "yes" : "no");

  if (!releaseParser.foundTag()) {
    LOG_ERR("OTA", "No tag_name in release JSON");
    return JSON_PARSE_ERROR;
  }

  if (!releaseParser.foundFirmware()) {
    LOG_INF("OTA", "No %s asset in latest release", assetName);
    return NO_UPDATE;
  }
  if (!releaseParser.foundChecksum()) {
    LOG_ERR("OTA", "No checksum asset found for %s", assetName);
    return CHECKSUM_ERROR;
  }

  latestVersion = releaseParser.getTagName();
  otaUrl = releaseParser.getFirmwareUrl();
  checksumUrl = releaseParser.getChecksumUrl();
  otaSize = releaseParser.getFirmwareSize();
  firmware_release::Version parsedVersion;
  if (!firmware_release::parseVersion(latestVersion.c_str(), parsedVersion)) {
    LOG_ERR("OTA", "Release tag has an invalid stable-version format: %s", latestVersion.c_str());
    return JSON_PARSE_ERROR;
  }
  if (!isTrustedReleaseAssetUrl(otaUrl) || !isTrustedReleaseAssetUrl(checksumUrl)) {
    LOG_ERR("OTA", "Release contains an untrusted asset URL");
    return JSON_PARSE_ERROR;
  }
  if (otaSize == 0 || otaSize > std::numeric_limits<size_t>::max() / 2) {
    LOG_ERR("OTA", "Release firmware has an invalid size: %u", static_cast<unsigned>(otaSize));
    return JSON_PARSE_ERROR;
  }
  totalSize = otaSize * 2;
  updateAvailable = true;

  LOG_DBG("OTA", "Found update: tag=%s size=%zu", latestVersion.c_str(), otaSize);
  LOG_DBG("OTA", "Firmware URL: %s", otaUrl.c_str());
  return OK;
}

bool OtaUpdater::isUpdateNewer() const {
  if (!updateAvailable || latestVersion.empty() || latestVersion == CROSSPOINT_VERSION) {
    return false;
  }

  firmware_release::Version current;
  firmware_release::Version latest;
  if (!firmware_release::parseVersion(CROSSPOINT_VERSION, current) ||
      !firmware_release::parseVersion(latestVersion.c_str(), latest)) {
    LOG_ERR("OTA", "Invalid version: current=%s latest=%s", CROSSPOINT_VERSION, latestVersion.c_str());
    return false;
  }

  if (latest.major != current.major) return latest.major > current.major;
  if (latest.minor != current.minor) return latest.minor > current.minor;
  if (latest.patch != current.patch) return latest.patch > current.patch;
  return latest.vanNhanSoRevision > current.vanNhanSoRevision;
}

const std::string& OtaUpdater::getLatestVersion() const { return latestVersion; }

OtaUpdater::OtaUpdaterError OtaUpdater::installUpdate(ProgressCallback onProgress, void* ctx) {
  if (!isUpdateNewer()) {
    return UPDATE_OLDER_ERROR;
  }

  const esp_partition_t* updatePartition = esp_ota_get_next_update_partition(nullptr);
  if (!updatePartition) {
    LOG_ERR("OTA", "No OTA partition available");
    return INTERNAL_UPDATE_ERROR;
  }
  if (otaSize == 0 || otaSize > updatePartition->size) {
    LOG_ERR("OTA", "Firmware size does not fit update partition: %u > %u", static_cast<unsigned>(otaSize),
            static_cast<unsigned>(updatePartition->size));
    return INVALID_IMAGE_ERROR;
  }

  std::string checksumSidecar;
  bool checksumTooLarge = false;
  const bool checksumFetched = HttpDownloader::fetchUrl(
      checksumUrl,
      [&checksumSidecar, &checksumTooLarge](const uint8_t* data, const size_t length) {
        constexpr size_t MAX_CHECKSUM_SIDECAR_SIZE = 256;
        if (checksumSidecar.size() + length > MAX_CHECKSUM_SIDECAR_SIZE) {
          checksumTooLarge = true;
          return false;
        }
        checksumSidecar.append(reinterpret_cast<const char*>(data), length);
        return true;
      },
      "", "", HttpDownloader::TransportSecurity::VERIFIED_TLS);
  if (!checksumFetched || checksumTooLarge) {
    LOG_ERR("OTA", "Could not download release checksum");
    return HTTP_ERROR;
  }
  std::string expectedChecksum;
  if (!firmware_release::parseSha256Sidecar(checksumSidecar, expectedChecksum)) {
    LOG_ERR("OTA", "Invalid firmware checksum contents");
    return CHECKSUM_ERROR;
  }

  Storage.remove(OTA_TEMP_PATH);
  esp_wifi_set_ps(WIFI_PS_NONE);
  installPhase = InstallPhase::DOWNLOADING;
  processedSize = 0;
  int lastReportedPct = -1;
  const auto downloadResult = HttpDownloader::downloadToFile(
      otaUrl, OTA_TEMP_PATH,
      [this, onProgress, ctx, &lastReportedPct](const size_t downloaded, const size_t) {
        processedSize = std::min(downloaded, otaSize);
        const size_t progressTotal = totalSize.load();
        const int pct = progressTotal > 0 ? static_cast<int>(processedSize.load() * 100 / progressTotal) : 0;
        if (onProgress && pct != lastReportedPct) {
          lastReportedPct = pct;
          onProgress(ctx);
        }
      },
      nullptr, "", "", nullptr, 60000, HttpDownloader::TransportSecurity::VERIFIED_TLS);
  esp_wifi_set_ps(WIFI_PS_MIN_MODEM);
  if (downloadResult != HttpDownloader::OK) {
    Storage.remove(OTA_TEMP_PATH);
    installPhase = InstallPhase::IDLE;
    return downloadResult == HttpDownloader::FILE_ERROR ? STORAGE_ERROR : HTTP_ERROR;
  }

  HalFile staged;
  if (!Storage.openFileForRead("OTA", OTA_TEMP_PATH, staged)) {
    Storage.remove(OTA_TEMP_PATH);
    installPhase = InstallPhase::IDLE;
    return STORAGE_ERROR;
  }
  const size_t stagedSize = staged.fileSize();
  staged.close();
  if (stagedSize != otaSize) {
    LOG_ERR("OTA", "Downloaded size mismatch: expected=%u actual=%u", static_cast<unsigned>(otaSize),
            static_cast<unsigned>(stagedSize));
    Storage.remove(OTA_TEMP_PATH);
    installPhase = InstallPhase::IDLE;
    return CHECKSUM_ERROR;
  }

  installPhase = InstallPhase::VERIFYING;
  if (onProgress) onProgress(ctx);
  if (!verifyFileSha256(OTA_TEMP_PATH, expectedChecksum)) {
    LOG_ERR("OTA", "Release SHA-256 verification failed");
    Storage.remove(OTA_TEMP_PATH);
    installPhase = InstallPhase::IDLE;
    return CHECKSUM_ERROR;
  }

  const auto validation = firmware_flash::validateImageFile(OTA_TEMP_PATH, updatePartition->size);
  if (validation != firmware_flash::Result::OK) {
    LOG_ERR("OTA", "Firmware image validation failed: %s", firmware_flash::resultName(validation));
    Storage.remove(OTA_TEMP_PATH);
    installPhase = InstallPhase::IDLE;
    return validation == firmware_flash::Result::BAD_CHIP || validation == firmware_flash::Result::WRONG_BOARD
               ? WRONG_DEVICE_ERROR
               : INVALID_IMAGE_ERROR;
  }

  struct FlashProgressContext {
    OtaUpdater* updater;
    ProgressCallback callback;
    void* callbackContext;
    int lastPercent;
  } progressContext{this, onProgress, ctx, lastReportedPct};
  const auto flashProgress = +[](const size_t written, const size_t, void* opaque) {
    auto* progress = static_cast<FlashProgressContext*>(opaque);
    progress->updater->processedSize = progress->updater->otaSize + written;
    const size_t progressTotal = progress->updater->totalSize.load();
    const int pct =
        progressTotal > 0 ? static_cast<int>(progress->updater->processedSize.load() * 100 / progressTotal) : 0;
    if (progress->callback && pct != progress->lastPercent) {
      progress->lastPercent = pct;
      progress->callback(progress->callbackContext);
    }
  };

  installPhase = InstallPhase::FLASHING;
  if (onProgress) onProgress(ctx);
  const auto flashResult = firmware_flash::flashFromSdPath(OTA_TEMP_PATH, flashProgress, &progressContext, true);
  Storage.remove(OTA_TEMP_PATH);
  installPhase = InstallPhase::IDLE;
  if (flashResult != firmware_flash::Result::OK) {
    LOG_ERR("OTA", "Firmware flash failed: %s", firmware_flash::resultName(flashResult));
    return flashResult == firmware_flash::Result::BAD_CHIP || flashResult == firmware_flash::Result::WRONG_BOARD
               ? WRONG_DEVICE_ERROR
               : INTERNAL_UPDATE_ERROR;
  }

  LOG_INF("OTA", "Update completed");
  return OK;
}
