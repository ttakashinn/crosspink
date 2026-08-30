#include "VanNhanSoUpdateActivity.h"

#include <ArduinoJson.h>
#include <CrossPointSettings.h>
#include <CrossPointState.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <Rtc.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <ctime>
#include <iterator>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "VanNhanSoUpdateHooks.h"
#include "WifiCredentialStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "features/vannhanso/VanNhanSoCache.h"
#include "features/vannhanso/VanNhanSoProfile.h"
#include "features/vannhanso/VanNhanSoUpdatePolicy.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"
#include "network/WifiConnectionDiagnostics.h"
#include "network/WifiConnectionPlatform.h"
#include "network/WifiConnectionPolicy.h"

#ifndef VANNHANSO_X3_MANIFEST_URL
#define VANNHANSO_X3_MANIFEST_URL "https://vannhanso.com/eink/v2/xteink-x3/manifest/today"
#endif

#ifndef VANNHANSO_X4_MANIFEST_URL
#define VANNHANSO_X4_MANIFEST_URL "https://vannhanso.com/eink/v2/xteink-x4/manifest/today"
#endif

namespace {
using vannhanso_cache::CACHE_PATH;
using vannhanso_cache::TEMP_PATH;
constexpr const char* DATE_PATH = "/.crosspoint/vannhanso-date.txt";
constexpr const char* PROFILE_PATH = "/.crosspoint/vannhanso-profile.txt";
constexpr const char* MANIFEST_TEMP_PATH = "/.crosspoint/vannhanso-manifest.tmp";
constexpr int VIETNAM_UTC_OFFSET_HOURS = 7;
constexpr size_t SHA256_HEX_LENGTH = 64;
constexpr size_t PROFILE_MAX_LENGTH = 128;
constexpr size_t REQUEST_URL_MAX_LENGTH = 384;
constexpr size_t MANIFEST_MAX_LENGTH = 2048;
// X3/X4 release builds already carry the low-memory wolfSSL transport used by
// normal HTTPS downloads. The ESP-IDF verified client requires a much larger
// contiguous handshake allocation and is failing before headers on real C3
// devices. The VNS URLs remain hard-coded HTTPS, asset hosts are allow-listed,
// and every image is length/schema/SHA-256 checked before an atomic install.
constexpr auto VNS_TRANSPORT = HttpDownloader::TransportSecurity::STANDARD;

#if defined(SIMULATOR)
bool hostSystemDateTime(Rtc::DateTime& out) {
  const std::time_t now = std::time(nullptr);
  std::tm utcTime{};
#if defined(_WIN32)
  if (gmtime_s(&utcTime, &now) != 0) return false;
#else
  if (!gmtime_r(&now, &utcTime)) return false;
#endif
  out.year = static_cast<uint16_t>(utcTime.tm_year + 1900);
  out.month = static_cast<uint8_t>(utcTime.tm_mon + 1);
  out.day = static_cast<uint8_t>(utcTime.tm_mday);
  out.hour = static_cast<uint8_t>(utcTime.tm_hour);
  out.minute = static_cast<uint8_t>(utcTime.tm_min);
  out.second = static_cast<uint8_t>(utcTime.tm_sec);
  out.weekday = static_cast<uint8_t>(utcTime.tm_wday);
  return true;
}
#endif

bool buildProfileSignature(char* output, const size_t outputSize) {
  // The legacy single-image marker must use the same canonical profile as the
  // manifest URL and per-profile cache. Keeping a second option table here
  // previously allowed a new weather location to silently fall back to Hanoi.
  return vannhanso_profile::buildQuery(output, outputSize);
}

bool isLeapYear(const uint16_t year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

uint8_t daysInMonth(const uint16_t year, const uint8_t month) {
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) return 29;
  return DAYS[month - 1];
}

bool vietnamDateTime(Rtc::DateTime dt, uint32_t& dateKey, uint16_t& minuteOfDay) {
  if (dt.year < 2024 || dt.month < 1 || dt.month > 12 || dt.day < 1 || dt.day > daysInMonth(dt.year, dt.month) ||
      dt.hour > 23 || dt.minute > 59) {
    return false;
  }

  uint8_t localHour = dt.hour + VIETNAM_UTC_OFFSET_HOURS;
  if (localHour >= 24) {
    localHour -= 24;
    if (++dt.day > daysInMonth(dt.year, dt.month)) {
      dt.day = 1;
      if (++dt.month > 12) {
        dt.month = 1;
        ++dt.year;
      }
    }
  }
  dateKey = static_cast<uint32_t>(dt.year) * 10000U + static_cast<uint32_t>(dt.month) * 100U + dt.day;
  minuteOfDay = static_cast<uint16_t>(localHour) * 60U + dt.minute;
  return true;
}

bool isSha256Hex(const std::string& value) {
  if (value.size() != SHA256_HEX_LENGTH) return false;
  for (const char ch : value) {
    if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
  }
  return true;
}

bool buildServerProfileSignature(char* output, const size_t outputSize) {
  if (!output || outputSize < 17) return false;
  char query[vannhanso_profile::QUERY_MAX_LENGTH];
  if (!vannhanso_profile::buildQuery(query, sizeof(query))) return false;

  uint8_t digest[32];
  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  mbedtls_sha256_starts(&context, /*is224=*/0);
  mbedtls_sha256_update(&context, reinterpret_cast<const unsigned char*>(query), strlen(query));
  mbedtls_sha256_finish(&context, digest);
  mbedtls_sha256_free(&context);
  for (size_t i = 0; i < 8; ++i) snprintf(output + i * 2, 3, "%02x", digest[i]);
  output[16] = '\0';
  return true;
}

bool isTrustedAssetUrl(const char* value) {
  if (!value) return false;
  static constexpr const char* ORIGIN_PREFIX = "https://vannhanso.com/";
  static constexpr const char* R2_PREFIX = "https://eink-assets.vannhanso.com/";
  static constexpr const char* WORKER_PREFIX = "https://vannhanso-eink-assets.kamikaze129.workers.dev/";
  return strncmp(value, ORIGIN_PREFIX, strlen(ORIGIN_PREFIX)) == 0 ||
         strncmp(value, R2_PREFIX, strlen(R2_PREFIX)) == 0 || strncmp(value, WORKER_PREFIX, strlen(WORKER_PREFIX)) == 0;
}

bool parseHttpDate(const std::string& value, Rtc::DateTime& dateTime) {
  char weekday[5] = {};
  char monthName[4] = {};
  char zone[4] = {};
  unsigned day = 0;
  unsigned year = 0;
  unsigned hour = 0;
  unsigned minute = 0;
  unsigned second = 0;
  char trailing = '\0';
  if (sscanf(value.c_str(), "%4s %u %3s %u %u:%u:%u %3s%c", weekday, &day, monthName, &year, &hour, &minute, &second,
             zone, &trailing) != 8 ||
      strlen(weekday) != 4 || weekday[3] != ',' || strcmp(zone, "GMT") != 0) {
    return false;
  }
  static constexpr const char* MONTHS[] = {"Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                           "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"};
  uint8_t month = 0;
  for (uint8_t i = 0; i < std::size(MONTHS); ++i) {
    if (strcmp(monthName, MONTHS[i]) == 0) {
      month = i + 1;
      break;
    }
  }
  if (year < 2024 || year > UINT16_MAX || month == 0 || day < 1 || day > daysInMonth(year, month) || hour > 23 ||
      minute > 59 || second > 60) {
    return false;
  }
  dateTime.year = static_cast<uint16_t>(year);
  dateTime.month = month;
  dateTime.day = static_cast<uint8_t>(day);
  dateTime.hour = static_cast<uint8_t>(hour);
  dateTime.minute = static_cast<uint8_t>(minute);
  dateTime.second = static_cast<uint8_t>(std::min(second, 59U));
  return true;
}

uint32_t parseIsoDateKey(const std::string& value) {
  if (value.size() != 10 || value[4] != '-' || value[7] != '-') return 0;
  uint32_t parts[3] = {};
  const uint8_t starts[] = {0, 5, 8};
  const uint8_t lengths[] = {4, 2, 2};
  for (uint8_t part = 0; part < 3; ++part) {
    for (uint8_t i = 0; i < lengths[part]; ++i) {
      const char ch = value[starts[part] + i];
      if (ch < '0' || ch > '9') return 0;
      parts[part] = parts[part] * 10U + static_cast<uint32_t>(ch - '0');
    }
  }
  if (parts[0] < 2024 || parts[1] < 1 || parts[1] > 12 || parts[2] < 1 || parts[2] > daysInMonth(parts[0], parts[1])) {
    return 0;
  }
  return parts[0] * 10000U + parts[1] * 100U + parts[2];
}

std::string formatDateKey(const uint32_t dateKey) {
  if (dateKey == 0) return "--/--/----";
  char value[11];
  snprintf(value, sizeof(value), "%02lu/%02lu/%04lu", static_cast<unsigned long>(dateKey % 100),
           static_cast<unsigned long>((dateKey / 100) % 100), static_cast<unsigned long>(dateKey / 10000));
  return value;
}

std::string formatDateTime(const uint32_t dateKey, const uint16_t minuteOfDay) {
  if (dateKey == 0) return "--/--/---- --:--";
  char value[18];
  if (minuteOfDay < 24U * 60U) {
    snprintf(value, sizeof(value), "%02lu/%02lu/%04lu %02u:%02u", static_cast<unsigned long>(dateKey % 100),
             static_cast<unsigned long>((dateKey / 100) % 100), static_cast<unsigned long>(dateKey / 10000),
             minuteOfDay / 60, minuteOfDay % 60);
  } else {
    snprintf(value, sizeof(value), "%02lu/%02lu/%04lu --:--", static_cast<unsigned long>(dateKey % 100),
             static_cast<unsigned long>((dateKey / 100) % 100), static_cast<unsigned long>(dateKey / 10000));
  }
  return value;
}

}  // namespace

void VanNhanSoUpdateActivity::onEnter() {
  Activity::onEnter();
  vannhanso_cache::recoverInterruptedInstall(renderer.getScreenWidth(), renderer.getScreenHeight());
  syncPendingProfile();

  if (automatic) {
    // An explicit sleep starts while the power button may still be held. Ignore
    // only that original hold; a later power press is new user activity and
    // must be able to cancel the pending sleep.
    ignoreTriggerPowerUntilRelease = sleepAfterUpdate && mappedInput.isPressed(MappedInputManager::Button::Power);
    startAutomaticUpdate();
    return;
  }

  resolveCurrentDate(false);
  state = STATUS;
  requestUpdate();
}

void VanNhanSoUpdateActivity::onExit() {
  Activity::onExit();
  wifi_connection_diagnostics::endAttempt();

  if (shouldTearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    WiFi.mode(WIFI_OFF);
    if (!automatic) {
      if (returnToVanNhanSoSettings) {
        silentRestartToVanNhanSoSettings();
      } else {
        silentRestart();
      }
    }
  }

  if (automatic && sleepAfterUpdate) vanNhanSoUpdateFinishedBeforeSleep(!sleepCancelledByUser);
}

void VanNhanSoUpdateActivity::beginManualUpdate() {
  shouldTearDownWifiOnExit = WiFi.status() != WL_CONNECTED;
  cancelDownload = false;

  if (WiFi.status() == WL_CONNECTED) {
    resolveCurrentDate(false);
    recordAttempt();
    state = DOWNLOADING;
    requestUpdate();
    return;
  }

  state = WIFI_SELECTION;
  WiFi.mode(WIFI_STA);
  auto wifiActivity = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifiActivity) {
    fail(CrossPointState::VanNhanSoUpdateError::NO_WIFI);
    return;
  }
  startActivityForResult(std::move(wifiActivity),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
}

void VanNhanSoUpdateActivity::startAutomaticUpdate() {
  state = WIFI_SELECTION;

  const bool haveCurrentDate = resolveCurrentDate(false);
  if (haveCurrentDate && vannhanso_update_policy::shouldSkipCurrentCache(
                             trigger, isCurrentCache(), currentDateKey, currentMinute,
                             APP_STATE.vanNhanSoLastSuccessDate, APP_STATE.vanNhanSoLastSuccessMinute)) {
    LOG_INF("VNS", "Sleep screen already current for %lu", static_cast<unsigned long>(currentDateKey));
    clearPendingProfile();
    state = SKIPPED;
    finish();
    return;
  }

  if (!haveCurrentDate && vannhanso_update_policy::shouldSkipAutomaticRetry(
                              currentProfileHash, APP_STATE.vanNhanSoFailureProfileHash,
                              APP_STATE.vanNhanSoUpdateResult == CrossPointState::VanNhanSoUpdateResult::FAILED,
                              APP_STATE.vanNhanSoAutoRetrySkipsRemaining)) {
    --APP_STATE.vanNhanSoAutoRetrySkipsRemaining;
    APP_STATE.saveToFile();
    LOG_INF("VNS", "Automatic update delayed for %u more trigger(s): clock unavailable",
            APP_STATE.vanNhanSoAutoRetrySkipsRemaining);
    state = SKIPPED;
    finish();
    return;
  }

  if (isBackoffActive()) {
    LOG_INF("VNS", "Automatic update delayed by retry backoff");
    state = SKIPPED;
    finish();
    return;
  }

  WIFI_STORE.loadFromFile();
  const std::string lastSsid = WIFI_STORE.getLastConnectedSsid();
  const auto credential = lastSsid.empty() ? std::nullopt : WIFI_STORE.findCredential(lastSsid);
  if (!credential) {
    LOG_INF("VNS", "Automatic update skipped: no saved WiFi credential");
    fail(CrossPointState::VanNhanSoUpdateError::NO_WIFI);
    finish();
    return;
  }

  WiFi.persistent(false);
  const bool stationModeReady = wifi_connection_platform::enterStationMode();
  if (!wifi_connection_platform::disconnectForRetry(1000)) {
    LOG_INF("VNS", "Previous STA disconnect did not settle within 1000ms; continuing");
  }
  delay(100);
  if (!wifi_connection_platform::disablePowerSave()) {
    LOG_INF("VNS", "Could not disable WiFi power save during association");
  }
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);

  connectionStartTime = millis();
  wifi_connection_diagnostics::beginAttempt();
  const wl_status_t beginStatus = WiFi.begin(credential->ssid.c_str(), credential->password.c_str());
  connectionBeginAccepted = wifi_connection_platform::beginAccepted(stationModeReady, beginStatus);
  lastConnectionStatusLogTime = 0;
  lastLoggedWifiStatus = -1;
  LOG_INF("VNS", "Automatic WiFi begin status=%d accepted=%d", static_cast<int>(beginStatus), connectionBeginAccepted);

  shouldTearDownWifiOnExit = true;
  state = AUTO_CONNECTING;
  if (pendingProfileRequired) requestUpdate();
}

void VanNhanSoUpdateActivity::checkAutomaticConnection() {
  const wl_status_t status = WiFi.status();
  const unsigned long now = millis();
  const unsigned long elapsed = now - connectionStartTime;
  if (lastLoggedWifiStatus != static_cast<int>(status) ||
      now - lastConnectionStatusLogTime >= CONNECTION_STATUS_LOG_INTERVAL_MS) {
    LOG_INF("VNS", "Automatic WiFi poll elapsed=%lums status=%d", elapsed, static_cast<int>(status));
    lastLoggedWifiStatus = static_cast<int>(status);
    lastConnectionStatusLogTime = now;
  }

  const auto outcome = wifi_connection_policy::evaluate(
      {wifi_connection_platform::policyStatus(status), wifi_connection_diagnostics::failureHint(),
       connectionBeginAccepted, static_cast<uint32_t>(elapsed), static_cast<uint32_t>(AUTO_CONNECTION_TIMEOUT_MS)});
  if (outcome == wifi_connection_policy::Outcome::CONNECTED) {
    wifi_connection_diagnostics::endAttempt();
    const bool haveCurrentDate = resolveCurrentDate(false);
    if (haveCurrentDate && vannhanso_update_policy::shouldSkipCurrentCache(
                               trigger, isCurrentCache(), currentDateKey, currentMinute,
                               APP_STATE.vanNhanSoLastSuccessDate, APP_STATE.vanNhanSoLastSuccessMinute)) {
      LOG_INF("VNS", "Sleep screen already current for %lu", static_cast<unsigned long>(currentDateKey));
      clearPendingProfile();
      state = SKIPPED;
      finish();
      return;
    }

    recordAttempt();
    state = DOWNLOADING;
    downloadSleepScreen();
    finish();
    return;
  }

  if (outcome != wifi_connection_policy::Outcome::PENDING) {
    wifi_connection_diagnostics::endAttempt();
    LOG_ERR("VNS", "Automatic update WiFi failed outcome=%d status=%d elapsed=%lums", static_cast<int>(outcome),
            static_cast<int>(status), elapsed);
    fail(outcome == wifi_connection_policy::Outcome::TIMED_OUT ? CrossPointState::VanNhanSoUpdateError::WIFI_TIMEOUT
                                                               : CrossPointState::VanNhanSoUpdateError::CONNECT);
    finish();
  }
}

bool VanNhanSoUpdateActivity::automaticCancellationRequested() {
  if (ignoreTriggerPowerUntilRelease && !mappedInput.isPressed(MappedInputManager::Button::Power)) {
    ignoreTriggerPowerUntilRelease = false;
  }

  int x = 0;
  int y = 0;
  const bool backPressed = mappedInput.wasPressed(MappedInputManager::Button::Back);
  const bool anyButtonPressed = mappedInput.wasAnyPressed();
  const bool powerButtonPressed = mappedInput.wasPressed(MappedInputManager::Button::Power);
  const bool screenTapped = mappedInput.wasScreenTapped(x, y);
  const bool shouldCancel = vannhanso_update_policy::shouldCancelAutomaticUpdate(
      trigger, pendingProfileRequired, backPressed, anyButtonPressed, powerButtonPressed, screenTapped,
      ignoreTriggerPowerUntilRelease);
  if (shouldCancel && sleepAfterUpdate) sleepCancelledByUser = true;
  return shouldCancel;
}

void VanNhanSoUpdateActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    state = STATUS;
    requestUpdate();
    return;
  }

  resolveCurrentDate(false);
  recordAttempt();
  state = DOWNLOADING;
  requestUpdate();
}

void VanNhanSoUpdateActivity::loop() {
  if (state == AUTO_CONNECTING) {
    if (automaticCancellationRequested()) {
      LOG_INF("VNS", "Automatic update cancelled by user input");
      if (pendingProfileRequired) recordCancelled();
      finish();
      return;
    }
    checkAutomaticConnection();
    return;
  }

  if (state == DOWNLOADING) {
    requestUpdateAndWait();
    // The manifest calendar_date and X-Calendar-Date response header are
    // authoritative for /manifest/today. Do not block the UI on an NTP round
    // trip before downloading it.
    resolveCurrentDate(false);
    downloadSleepScreen();
    if (automatic) finish();
    return;
  }

  int x = 0;
  int y = 0;
  // Idle/status screens follow the same release-edge convention as every
  // UiListActivity. Consuming the press here used to pop this screen, then
  // let the matching release pop VanNhanSoSettings as well.
  if (mappedInput.wasReleased(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (!automatic && mappedInput.wasReleased(MappedInputManager::Button::Confirm)) {
    beginManualUpdate();
    return;
  }
  if (mappedInput.wasScreenTapped(x, y)) {
    if (automatic) {
      finish();
    } else {
      beginManualUpdate();
    }
  }
}

bool VanNhanSoUpdateActivity::resolveCurrentDate(const bool allowNetworkSync) {
  Rtc::DateTime dateTime;
  bool haveDate = false;

#if defined(SIMULATOR)
  (void)allowNetworkSync;
  haveDate = hostSystemDateTime(dateTime);
#else
  // A merely plausible ESP system date is not authoritative after a complete
  // power loss. Only use clocks that have previously been synchronized; an
  // authenticated HTTPS Date response below re-establishes that state.
  if (SETTINGS.clockHasBeenSynced) {
    haveDate = halClock.isAvailable() ? halClock.getDateTime(dateTime) : halClock.getSystemDateTime(dateTime);
  }

  if (!haveDate && allowNetworkSync && WiFi.status() == WL_CONNECTED) {
    if (halClock.isAvailable()) {
      if (halClock.syncFromNTP()) {
        SETTINGS.clockHasBeenSynced = 1;
        SETTINGS.saveToFile();
        haveDate = halClock.getDateTime(dateTime);
      }
    } else {
      if (halClock.syncSystemTimeFromNTP(dateTime)) {
        SETTINGS.clockHasBeenSynced = 1;
        SETTINGS.saveToFile();
        haveDate = true;
      }
    }
  }
#endif

  currentDateKey = 0;
  currentMinute = UINT16_MAX;
  return haveDate && vietnamDateTime(dateTime, currentDateKey, currentMinute);
}

void VanNhanSoUpdateActivity::syncPendingProfile() {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  currentProfileHash = vannhanso_profile::identityHash(width, height);
  const uint32_t pendingHash = vannhanso_update_policy::pendingProfileHash(
      vannhanso_cache::hasCurrentProfileImage(width, height), currentProfileHash);
  pendingProfileRequired = pendingHash != 0;
  if (APP_STATE.vanNhanSoPendingProfileHash == pendingHash) return;

  APP_STATE.vanNhanSoPendingProfileHash = pendingHash;
  APP_STATE.saveToFile();
}

void VanNhanSoUpdateActivity::clearPendingProfile() {
  pendingProfileRequired = false;
  if (APP_STATE.vanNhanSoPendingProfileHash != currentProfileHash) return;

  APP_STATE.vanNhanSoPendingProfileHash = 0;
  APP_STATE.saveToFile();
}

bool VanNhanSoUpdateActivity::isCurrentCache() const {
  if (currentDateKey == 0) return false;

  char profilePath[vannhanso_profile::PATH_MAX_LENGTH];
  uint32_t storedDateKey = 0;
  if (vannhanso_profile::buildImagePath(renderer.getScreenWidth(), renderer.getScreenHeight(), profilePath,
                                        sizeof(profilePath)) &&
      vannhanso_cache::validateImage(profilePath, renderer.getScreenWidth(), renderer.getScreenHeight()) &&
      vannhanso_cache::readCurrentDate(renderer.getScreenWidth(), renderer.getScreenHeight(), storedDateKey)) {
    return storedDateKey == currentDateKey;
  }

  // One-time compatibility path for the single-image cache written by vns.4.
  if (!Storage.exists(CACHE_PATH) ||
      !vannhanso_cache::validateImage(CACHE_PATH, renderer.getScreenWidth(), renderer.getScreenHeight())) {
    return false;
  }
  char storedProfile[PROFILE_MAX_LENGTH];
  char currentProfile[PROFILE_MAX_LENGTH];
  return readDateMarker(storedDateKey) && storedDateKey == currentDateKey &&
         readProfileMarker(storedProfile, sizeof(storedProfile)) &&
         buildProfileSignature(currentProfile, sizeof(currentProfile)) && strcmp(storedProfile, currentProfile) == 0;
}

bool VanNhanSoUpdateActivity::isBackoffActive() const {
  return vannhanso_update_policy::isBackoffActive(
      currentProfileHash, APP_STATE.vanNhanSoFailureProfileHash,
      APP_STATE.vanNhanSoUpdateResult == CrossPointState::VanNhanSoUpdateResult::FAILED,
      APP_STATE.vanNhanSoConsecutiveFailures, currentDateKey, currentMinute, APP_STATE.vanNhanSoLastAttemptDate,
      APP_STATE.vanNhanSoLastAttemptMinute);
}

bool VanNhanSoUpdateActivity::readDateMarker(uint32_t& dateKey) const {
  HalFile file;
  if (!Storage.openFileForRead("VNS", DATE_PATH, file)) return false;

  char value[9] = {};
  if (file.read(value, 8) != 8) return false;
  uint32_t parsed = 0;
  for (uint8_t i = 0; i < 8; ++i) {
    if (value[i] < '0' || value[i] > '9') return false;
    parsed = parsed * 10U + static_cast<uint32_t>(value[i] - '0');
  }
  dateKey = parsed;
  return true;
}

bool VanNhanSoUpdateActivity::readProfileMarker(char* profile, const size_t profileSize) const {
  if (!profile || profileSize < 2) return false;
  HalFile file;
  if (!Storage.openFileForRead("VNS", PROFILE_PATH, file)) return false;
  const size_t length = file.fileSize();
  if (length == 0 || length >= profileSize) return false;
  if (file.read(reinterpret_cast<uint8_t*>(profile), length) != static_cast<int>(length)) return false;
  profile[length] = '\0';
  return true;
}

void VanNhanSoUpdateActivity::downloadSleepScreen() {
  const bool isX3 = renderer.getScreenWidth() >= 528;
  const char* baseUrl = isX3 ? VANNHANSO_X3_MANIFEST_URL : VANNHANSO_X4_MANIFEST_URL;
  const uint32_t localDateBeforeRequest = currentDateKey;
  char url[REQUEST_URL_MAX_LENGTH];
  if (!vannhanso_profile::buildManifestUrl(baseUrl, url, sizeof(url))) {
    fail(CrossPointState::VanNhanSoUpdateError::METADATA);
    return;
  }

  downloadedBytes = 0;
  totalBytes = 0;
  cancelDownload = false;
  const uint32_t timeoutMs = automatic ? AUTOMATIC_DOWNLOAD_TIMEOUT_MS : MANUAL_DOWNLOAD_TIMEOUT_MS;

  const auto pollCancellation = [this](const size_t, const size_t) {
    mappedInput.update();
    int x = 0;
    int y = 0;
    if (automatic) {
      cancelDownload = automaticCancellationRequested();
    } else if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
      cancelDownload = true;
    }
  };

  HttpDownloader::ResponseInfo manifestResponse;
  const auto manifestResult = HttpDownloader::downloadToFile(url, MANIFEST_TEMP_PATH, pollCancellation, &cancelDownload,
                                                             "", "", &manifestResponse, timeoutMs, VNS_TRANSPORT);
  if (manifestResult == HttpDownloader::ABORTED) {
    Storage.remove(MANIFEST_TEMP_PATH);
    recordCancelled(/*returnToStatus=*/!automatic);
    return;
  }
  if (cancelDownload) {
    // Some transports report a socket error while unwinding an explicitly
    // aborted request. User cancellation wins over that implementation detail:
    // it is not an HTTPS failure and the manual flow returns to its status page.
    Storage.remove(MANIFEST_TEMP_PATH);
    recordCancelled(/*returnToStatus=*/!automatic);
    return;
  }
  if (manifestResult != HttpDownloader::OK) {
    Storage.remove(MANIFEST_TEMP_PATH);
    failDownload(manifestResult, manifestResponse);
    return;
  }

  char manifestBody[MANIFEST_MAX_LENGTH + 1] = {};
  const size_t manifestLength =
      Storage.readFileToBuffer(MANIFEST_TEMP_PATH, manifestBody, sizeof(manifestBody), MANIFEST_MAX_LENGTH);
  Storage.remove(MANIFEST_TEMP_PATH);
  JsonDocument manifest;
  const DeserializationError jsonError = deserializeJson(manifest, manifestBody, manifestLength);
  const char* manifestDevice = manifest["device"] | "";
  const char* manifestProfile = manifest["profile"] | "";
  const char* assetUrl = manifest["asset_url"] | "";
  const char* checksum = manifest["sha256"] | "";
  const char* calendarDate = manifest["calendar_date"] | "";
  const int expectedWidth = manifest["width"] | 0;
  const int expectedHeight = manifest["height"] | 0;
  const int expectedBpp = manifest["bits_per_pixel"] | 0;
  const size_t expectedLength = manifest["content_length"] | static_cast<size_t>(0);
  const char* expectedDevice = isX3 ? "xteink-x3" : "xteink-x4";
  char expectedProfile[17];
  if (manifestLength == 0 || jsonError || (manifest["version"] | 0) != 2 ||
      strcmp(manifestDevice, expectedDevice) != 0 || expectedWidth != renderer.getScreenWidth() ||
      expectedHeight != renderer.getScreenHeight() || expectedBpp != 2 || expectedLength == 0 ||
      expectedLength > 1024U * 1024U || !isSha256Hex(checksum) || !isTrustedAssetUrl(assetUrl) ||
      strlen(assetUrl) >= REQUEST_URL_MAX_LENGTH ||
      !buildServerProfileSignature(expectedProfile, sizeof(expectedProfile)) ||
      strcmp(manifestProfile, expectedProfile) != 0) {
    LOG_ERR("VNS", "Invalid v2 manifest: %s", jsonError ? jsonError.c_str() : "schema mismatch");
    fail(CrossPointState::VanNhanSoUpdateError::METADATA);
    return;
  }

  const uint32_t responseDateKey = parseIsoDateKey(calendarDate);
  const uint32_t headerDateKey = parseIsoDateKey(manifestResponse.calendarDate);
  if (responseDateKey == 0 || (!manifestResponse.calendarDate.empty() && headerDateKey != responseDateKey)) {
    LOG_ERR("VNS", "Invalid or inconsistent manifest date: body=%lu header=%lu",
            static_cast<unsigned long>(responseDateKey), static_cast<unsigned long>(headerDateKey));
    fail(CrossPointState::VanNhanSoUpdateError::METADATA);
    return;
  }

  const std::string expectedChecksum(checksum);
  const std::string resolvedAssetUrl(assetUrl);
  const std::string serverDate = manifestResponse.serverDate;

  Rtc::DateTime serverDateTime;
  uint32_t serverVietnamDateKey = 0;
  uint16_t serverVietnamMinute = UINT16_MAX;
  const bool haveServerTime = parseHttpDate(serverDate, serverDateTime);
  if (haveServerTime && (!vietnamDateTime(serverDateTime, serverVietnamDateKey, serverVietnamMinute) ||
                         serverVietnamDateKey != responseDateKey)) {
    LOG_ERR("VNS", "Response Date and X-Calendar-Date headers disagree");
    fail(CrossPointState::VanNhanSoUpdateError::METADATA);
    return;
  }
  // The manifest is authoritative for the daily asset. It also lets X4
  // persist the data date after a full power loss, without waiting for NTP.
  if (localDateBeforeRequest != 0 && localDateBeforeRequest != responseDateKey) {
    LOG_INF("VNS", "Correcting local date from %lu to server date %lu",
            static_cast<unsigned long>(localDateBeforeRequest), static_cast<unsigned long>(responseDateKey));
  }
  currentDateKey = responseDateKey;
#if defined(SIMULATOR)
  if (haveServerTime) {
    currentMinute = serverVietnamMinute;
  } else {
    currentMinute = UINT16_MAX;
    LOG_ERR("VNS", "Missing or invalid HTTPS Date response header; daily cache timing may be unavailable");
  }
#else
  if (haveServerTime) {
    currentMinute = serverVietnamMinute;
    if (halClock.setDateTimeUtc(serverDateTime)) {
      if (!SETTINGS.clockHasBeenSynced) {
        SETTINGS.clockHasBeenSynced = 1;
        SETTINGS.saveToFile();
      }
    } else {
      LOG_ERR("VNS", "Could not persist the authenticated server time");
    }
  } else {
    currentMinute = UINT16_MAX;
    LOG_ERR("VNS", "Missing or invalid HTTPS Date response header; daily cache timing may be unavailable");
  }
#endif

  // "When entering sleep" means checking the manifest on every sleep, not
  // rewriting an identical immutable asset every time. Hash the active
  // profile image and avoid the second HTTP request when the server checksum
  // has not changed.
  char currentImagePath[vannhanso_profile::PATH_MAX_LENGTH];
  HalFile currentImage;
  const bool unchanged = vannhanso_profile::buildImagePath(renderer.getScreenWidth(), renderer.getScreenHeight(),
                                                           currentImagePath, sizeof(currentImagePath)) &&
                         Storage.openFileForRead("VNS", currentImagePath, currentImage) &&
                         currentImage.fileSize() == expectedLength;
  if (currentImage) currentImage.close();
  if (unchanged && validateChecksum(currentImagePath, expectedChecksum, /*logMismatch=*/false)) {
    uint32_t storedDateKey = 0;
    if ((!vannhanso_cache::readCurrentDate(renderer.getScreenWidth(), renderer.getScreenHeight(), storedDateKey) ||
         storedDateKey != currentDateKey) &&
        !vannhanso_cache::writeCurrentDate(renderer.getScreenWidth(), renderer.getScreenHeight(), currentDateKey)) {
      fail(CrossPointState::VanNhanSoUpdateError::METADATA);
      return;
    }
    LOG_INF("VNS", "Manifest checked; current profile image is unchanged");
    recordSuccess();
    state = SUCCESS;
    requestUpdate();
    return;
  }

  // The image body is streamed directly to SD and never buffered in heap.
  HttpDownloader::ResponseInfo responseInfo;
  const auto result = HttpDownloader::downloadToFile(
      resolvedAssetUrl, TEMP_PATH,
      [this](const size_t downloaded, const size_t total) {
        const size_t previousTotal = totalBytes.load();
        const size_t previousDownloaded = downloadedBytes.load();
        const int oldPercentage = previousTotal > 0 ? static_cast<int>(previousDownloaded * 100 / previousTotal) : -1;
        downloadedBytes = downloaded;
        totalBytes = total;
        const int newPercentage = total > 0 ? static_cast<int>(downloaded * 100 / total) : -1;
        if ((!automatic || pendingProfileRequired) && newPercentage != oldPercentage) requestUpdate(true);

        // Automatic refresh must yield to the user. A transfer that is already
        // receiving data can be cancelled immediately; a stalled socket is
        // bounded by the per-mode timeout below.
        mappedInput.update();
        int x = 0;
        int y = 0;
        if (automatic) {
          cancelDownload = automaticCancellationRequested();
        } else if (mappedInput.wasReleased(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) {
          cancelDownload = true;
        }
      },
      &cancelDownload, "", "", &responseInfo, timeoutMs, VNS_TRANSPORT);

  if (result == HttpDownloader::ABORTED) {
    Storage.remove(TEMP_PATH);
    recordCancelled(/*returnToStatus=*/!automatic);
    return;
  }
  if (cancelDownload) {
    Storage.remove(TEMP_PATH);
    recordCancelled(/*returnToStatus=*/!automatic);
    return;
  }
  if (result != HttpDownloader::OK) {
    Storage.remove(TEMP_PATH);
    failDownload(result, responseInfo);
    return;
  }

  HalFile downloadedFile;
  if (!Storage.openFileForRead("VNS", TEMP_PATH, downloadedFile) || downloadedFile.fileSize() != expectedLength) {
    if (downloadedFile) downloadedFile.close();
    Storage.remove(TEMP_PATH);
    fail(CrossPointState::VanNhanSoUpdateError::INCOMPLETE);
    return;
  }
  downloadedFile.close();

  state = VERIFYING;
  if (!automatic || pendingProfileRequired) requestUpdateAndWait();
  if (!validateChecksum(TEMP_PATH, expectedChecksum)) {
    Storage.remove(TEMP_PATH);
    fail(CrossPointState::VanNhanSoUpdateError::CHECKSUM_MISMATCH);
    return;
  }
  if (!vannhanso_cache::validateImage(TEMP_PATH, renderer.getScreenWidth(), renderer.getScreenHeight())) {
    Storage.remove(TEMP_PATH);
    fail(CrossPointState::VanNhanSoUpdateError::INVALID_IMAGE);
    return;
  }

  state = INSTALLING;
  if (!automatic || pendingProfileRequired) requestUpdateAndWait();
  if (!vannhanso_cache::installDownloadedImage(renderer.getScreenWidth(), renderer.getScreenHeight())) {
    Storage.remove(TEMP_PATH);
    fail(CrossPointState::VanNhanSoUpdateError::INSTALL);
    return;
  }

  bool metadataOk = true;
  if (currentDateKey != 0 &&
      !vannhanso_cache::writeCurrentDate(renderer.getScreenWidth(), renderer.getScreenHeight(), currentDateKey)) {
    LOG_ERR("VNS", "Could not save update date marker");
    metadataOk = false;
  }
  if (!metadataOk) {
    fail(CrossPointState::VanNhanSoUpdateError::METADATA);
    return;
  }

  recordSuccess();
  state = SUCCESS;
  requestUpdate();
}

void VanNhanSoUpdateActivity::recordAttempt() {
  RenderLock lock(*this);
  APP_STATE.vanNhanSoUpdateResult = CrossPointState::VanNhanSoUpdateResult::IN_PROGRESS;
  APP_STATE.vanNhanSoUpdateError = CrossPointState::VanNhanSoUpdateError::NONE;
  APP_STATE.vanNhanSoLastAttemptDate = currentDateKey;
  APP_STATE.vanNhanSoLastAttemptMinute = currentMinute;
  APP_STATE.vanNhanSoLastHttpStatus = 0;
  APP_STATE.saveToFile();
}

void VanNhanSoUpdateActivity::recordSuccess() {
  RenderLock lock(*this);
  APP_STATE.vanNhanSoUpdateResult = CrossPointState::VanNhanSoUpdateResult::SUCCESS;
  APP_STATE.vanNhanSoUpdateError = CrossPointState::VanNhanSoUpdateError::NONE;
  APP_STATE.vanNhanSoLastAttemptDate = currentDateKey;
  APP_STATE.vanNhanSoLastSuccessDate = currentDateKey;
  APP_STATE.vanNhanSoLastAttemptMinute = currentMinute;
  APP_STATE.vanNhanSoLastSuccessMinute = currentMinute;
  APP_STATE.vanNhanSoConsecutiveFailures = 0;
  APP_STATE.vanNhanSoLastHttpStatus = 0;
  APP_STATE.vanNhanSoFailureProfileHash = 0;
  APP_STATE.vanNhanSoAutoRetrySkipsRemaining = 0;
  if (APP_STATE.vanNhanSoPendingProfileHash == currentProfileHash) {
    APP_STATE.vanNhanSoPendingProfileHash = 0;
  }
  pendingProfileRequired = false;
  APP_STATE.saveToFile();
}

void VanNhanSoUpdateActivity::recordCancelled(const bool returnToStatus) {
  {
    RenderLock lock(*this);
    APP_STATE.vanNhanSoUpdateResult = CrossPointState::VanNhanSoUpdateResult::CANCELLED;
    APP_STATE.vanNhanSoUpdateError = CrossPointState::VanNhanSoUpdateError::NONE;
    APP_STATE.vanNhanSoLastAttemptDate = currentDateKey;
    APP_STATE.vanNhanSoLastAttemptMinute = currentMinute;
    APP_STATE.vanNhanSoAutoRetrySkipsRemaining = 0;
    APP_STATE.saveToFile();
    // A manual cancellation returns to the initial status page in the same
    // critical section. This prevents a queued render from briefly showing a
    // terminal cancellation/error screen while the transport unwinds.
    state = returnToStatus ? STATUS : CANCELLED;
  }
  requestUpdate();
}

void VanNhanSoUpdateActivity::fail(const CrossPointState::VanNhanSoUpdateError error) {
  {
    RenderLock lock(*this);
    if (APP_STATE.vanNhanSoFailureProfileHash != currentProfileHash) {
      APP_STATE.vanNhanSoConsecutiveFailures = 0;
    }
    APP_STATE.vanNhanSoUpdateResult = CrossPointState::VanNhanSoUpdateResult::FAILED;
    APP_STATE.vanNhanSoUpdateError = error;
    APP_STATE.vanNhanSoLastAttemptDate = currentDateKey;
    APP_STATE.vanNhanSoLastAttemptMinute = currentMinute;
    APP_STATE.vanNhanSoConsecutiveFailures = std::min<uint8_t>(APP_STATE.vanNhanSoConsecutiveFailures + 1, 4);
    APP_STATE.vanNhanSoFailureProfileHash = currentProfileHash;
    APP_STATE.vanNhanSoAutoRetrySkipsRemaining =
        automatic && currentDateKey == 0
            ? vannhanso_update_policy::automaticRetrySkipsAfterFailure(APP_STATE.vanNhanSoConsecutiveFailures)
            : 0;
    APP_STATE.saveToFile();
    state = FAILED;
  }
  requestUpdate();
}

void VanNhanSoUpdateActivity::failDownload(const HttpDownloader::DownloadError result,
                                           const HttpDownloader::ResponseInfo& responseInfo) {
  APP_STATE.vanNhanSoLastHttpStatus =
      responseInfo.statusCode > 0 && responseInfo.statusCode <= UINT16_MAX ? responseInfo.statusCode : 0;
  if (responseInfo.statusCode == 429) {
    fail(CrossPointState::VanNhanSoUpdateError::HTTP_RATE_LIMIT);
  } else if (responseInfo.statusCode >= 500) {
    fail(CrossPointState::VanNhanSoUpdateError::HTTP_SERVER);
  } else if (responseInfo.downloadedBytes > 0 && !responseInfo.complete) {
    fail(CrossPointState::VanNhanSoUpdateError::INCOMPLETE);
  } else if (responseInfo.transportError != 0) {
    fail(CrossPointState::VanNhanSoUpdateError::CONNECT);
  } else if (result == HttpDownloader::FILE_ERROR) {
    fail(CrossPointState::VanNhanSoUpdateError::INSTALL);
  } else {
    fail(CrossPointState::VanNhanSoUpdateError::DOWNLOAD);
  }
}

bool VanNhanSoUpdateActivity::validateChecksum(const char* path, const std::string& expectedChecksum,
                                               const bool logMismatch) const {
  HalFile file;
  if (!path || !Storage.openFileForRead("VNS", path, file)) return false;

  mbedtls_sha256_context context;
  mbedtls_sha256_init(&context);
  mbedtls_sha256_starts(&context, /*is224=*/0);

  uint8_t buffer[1024];
  size_t remaining = file.fileSize();
  bool readOk = true;
  while (remaining > 0) {
    const size_t wanted = std::min(remaining, sizeof(buffer));
    const int bytesRead = file.read(buffer, wanted);
    if (bytesRead != static_cast<int>(wanted)) {
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
  if (!readOk) return false;

  char actualChecksum[SHA256_HEX_LENGTH + 1];
  for (size_t i = 0; i < sizeof(digest); ++i) {
    snprintf(actualChecksum + i * 2, 3, "%02x", digest[i]);
  }
  actualChecksum[SHA256_HEX_LENGTH] = '\0';

  bool matches = true;
  for (size_t i = 0; i < SHA256_HEX_LENGTH; ++i) {
    const char expected = static_cast<char>(std::tolower(static_cast<unsigned char>(expectedChecksum[i])));
    matches = matches && actualChecksum[i] == expected;
  }
  if (!matches) {
    if (logMismatch) LOG_ERR("VNS", "Sleep-screen checksum mismatch");
  } else {
    LOG_INF("VNS", "Verified sleep-screen SHA-256: %s", actualChecksum);
  }
  return matches;
}

const char* VanNhanSoUpdateActivity::errorText(const CrossPointState::VanNhanSoUpdateError error) const {
  switch (error) {
    case CrossPointState::VanNhanSoUpdateError::NO_WIFI:
      return tr(STR_VANNHANSO_ERROR_NO_WIFI);
    case CrossPointState::VanNhanSoUpdateError::WIFI_TIMEOUT:
      return tr(STR_VANNHANSO_ERROR_WIFI_TIMEOUT);
    case CrossPointState::VanNhanSoUpdateError::DOWNLOAD:
      return tr(STR_VANNHANSO_ERROR_DOWNLOAD);
    case CrossPointState::VanNhanSoUpdateError::CHECKSUM_MISSING:
      return tr(STR_VANNHANSO_ERROR_CHECKSUM_MISSING);
    case CrossPointState::VanNhanSoUpdateError::CHECKSUM_MISMATCH:
      return tr(STR_VANNHANSO_ERROR_CHECKSUM_MISMATCH);
    case CrossPointState::VanNhanSoUpdateError::INVALID_IMAGE:
      return tr(STR_VANNHANSO_ERROR_INVALID_IMAGE);
    case CrossPointState::VanNhanSoUpdateError::INSTALL:
      return tr(STR_VANNHANSO_ERROR_INSTALL);
    case CrossPointState::VanNhanSoUpdateError::METADATA:
      return tr(STR_VANNHANSO_ERROR_METADATA);
    case CrossPointState::VanNhanSoUpdateError::CONNECT:
      return tr(STR_VANNHANSO_ERROR_CONNECT);
    case CrossPointState::VanNhanSoUpdateError::HTTP_RATE_LIMIT:
      return tr(STR_VANNHANSO_ERROR_RATE_LIMIT);
    case CrossPointState::VanNhanSoUpdateError::HTTP_SERVER:
      return tr(STR_VANNHANSO_ERROR_SERVER);
    case CrossPointState::VanNhanSoUpdateError::INCOMPLETE:
      return tr(STR_VANNHANSO_ERROR_INCOMPLETE);
    case CrossPointState::VanNhanSoUpdateError::NONE:
    default:
      return "";
  }
}

void VanNhanSoUpdateActivity::render(RenderLock&&) {
  // Routine automatic updates intentionally leave the reader/home frame
  // untouched. A missing profile is the exception: show bounded progress so
  // the wake button cannot silently cancel the only usable download.
  if (automatic && !pendingProfileRequired) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_VANNHANSO));

  const int midY = pageHeight / 2;
  uint32_t cachedDateKey = 0;
  if (!vannhanso_cache::readCurrentDate(renderer.getScreenWidth(), renderer.getScreenHeight(), cachedDateKey)) {
    readDateMarker(cachedDateKey);
  }
  const std::string cachedDate = formatDateKey(cachedDateKey);
  char currentText[80];
  if (pendingProfileRequired) {
    snprintf(currentText, sizeof(currentText), "%s", tr(STR_VANNHANSO_PROFILE_PENDING));
  } else {
    snprintf(currentText, sizeof(currentText), tr(STR_VANNHANSO_CURRENT_DATA), cachedDate.c_str());
  }

  const std::string lastAttempt =
      formatDateTime(APP_STATE.vanNhanSoLastAttemptDate, APP_STATE.vanNhanSoLastAttemptMinute);
  const std::string lastSuccess =
      formatDateTime(APP_STATE.vanNhanSoLastSuccessDate, APP_STATE.vanNhanSoLastSuccessMinute);
  char attemptText[80];
  char successText[80];
  snprintf(attemptText, sizeof(attemptText), tr(STR_VANNHANSO_LAST_ATTEMPT), lastAttempt.c_str());
  snprintf(successText, sizeof(successText), tr(STR_VANNHANSO_LAST_SUCCESS), lastSuccess.c_str());

  if (state == WIFI_SELECTION || state == AUTO_CONNECTING) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY, tr(STR_CONNECTING));
  } else if (state == DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY - 20, tr(STR_VANNHANSO_DOWNLOADING));
    if (totalBytes > 0) {
      char progress[16];
      snprintf(progress, sizeof(progress), "%u%%",
               static_cast<unsigned>(downloadedBytes.load() * 100 / totalBytes.load()));
      renderer.drawCenteredText(UI_12_FONT_ID, midY + 20, progress, true, EpdFontFamily::BOLD);
    }
  } else if (state == VERIFYING) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY, tr(STR_VANNHANSO_VERIFYING));
  } else if (state == INSTALLING) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY, tr(STR_VANNHANSO_INSTALLING));
  } else if (state == SUCCESS || state == SKIPPED) {
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 18, tr(STR_VANNHANSO_UPDATED), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 22, currentText);
  } else if (state == FAILED) {
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 30, tr(STR_VANNHANSO_UPDATE_FAILED), true, EpdFontFamily::BOLD);
    if (APP_STATE.vanNhanSoLastHttpStatus > 0) {
      char statusText[48];
      snprintf(statusText, sizeof(statusText), "%s (HTTP %u)", errorText(APP_STATE.vanNhanSoUpdateError),
               APP_STATE.vanNhanSoLastHttpStatus);
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 5, statusText);
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 5, errorText(APP_STATE.vanNhanSoUpdateError));
    }
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 40, currentText);
  } else if (state == CANCELLED) {
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 18, tr(STR_CANCEL), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 22, currentText);
  } else {
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 82, tr(STR_VANNHANSO_UPDATE_STATUS), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, midY - 42, currentText);
    renderer.drawCenteredText(UI_10_FONT_ID, midY - 8, attemptText);
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 26, successText);
    if (APP_STATE.vanNhanSoUpdateResult == CrossPointState::VanNhanSoUpdateResult::FAILED) {
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 60, tr(STR_VANNHANSO_LAST_RESULT_FAILED));
    } else if (APP_STATE.vanNhanSoUpdateResult == CrossPointState::VanNhanSoUpdateResult::SUCCESS) {
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 60, tr(STR_VANNHANSO_LAST_RESULT_OK));
    } else if (APP_STATE.vanNhanSoUpdateResult == CrossPointState::VanNhanSoUpdateResult::CANCELLED) {
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 60, tr(STR_CANCEL));
    } else if (cachedDateKey != 0 && cachedDateKey == currentDateKey) {
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 60, tr(STR_VANNHANSO_CACHE_CURRENT));
    } else if (cachedDateKey != 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 60, tr(STR_VANNHANSO_CACHE_OLD));
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 60, tr(STR_VANNHANSO_CACHE_EMPTY));
    }
  }

  if (!automatic &&
      (state == STATUS || state == SUCCESS || state == FAILED || state == CANCELLED || state == SKIPPED)) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), state == FAILED ? tr(STR_RETRY) : tr(STR_UPDATE), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == WIFI_SELECTION || state == AUTO_CONNECTING || state == DOWNLOADING) {
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
