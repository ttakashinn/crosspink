#include "VanNhanSoUpdateActivity.h"

#include <Bitmap.h>
#include <CrossPointSettings.h>
#include <CrossPointState.h>
#include <GfxRenderer.h>
#include <HalClock.h>
#include <HalStorage.h>
#include <I18n.h>
#include <Logging.h>
#include <Memory.h>
#include <WiFi.h>
#include <mbedtls/sha256.h>

#include <algorithm>
#include <cctype>
#include <cstring>

#include "MappedInputManager.h"
#include "SilentRestart.h"
#include "WifiCredentialStore.h"
#include "activities/network/WifiSelectionActivity.h"
#include "components/UITheme.h"
#include "fontIds.h"
#include "network/HttpDownloader.h"

#ifndef VANNHANSO_X3_URL
#define VANNHANSO_X3_URL "https://vannhanso.com/eink/xteink-x3/sleep/dashboard/today.bmp"
#endif

#ifndef VANNHANSO_X4_URL
#define VANNHANSO_X4_URL "https://vannhanso.com/eink/xteink-x4/sleep/dashboard/today.bmp"
#endif

namespace {
constexpr const char* TEMP_PATH = "/.vannhanso-sleep.tmp";
constexpr const char* CACHE_PATH = "/vannhanso-sleep.bmp";
constexpr const char* BACKUP_PATH = "/.vannhanso-sleep.bak";
constexpr const char* DATE_PATH = "/.crosspoint/vannhanso-date.txt";
constexpr const char* DATE_TEMP_PATH = "/.crosspoint/vannhanso-date.tmp";
constexpr int VIETNAM_UTC_OFFSET_HOURS = 7;
constexpr size_t SHA256_HEX_LENGTH = 64;

bool isLeapYear(const uint16_t year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

uint8_t daysInMonth(const uint16_t year, const uint8_t month) {
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) return 29;
  return DAYS[month - 1];
}

uint32_t vietnamDateKey(Rtc::DateTime dt) {
  if (dt.year < 2024 || dt.month < 1 || dt.month > 12 || dt.day < 1 || dt.day > daysInMonth(dt.year, dt.month) ||
      dt.hour > 23) {
    return 0;
  }

  if (dt.hour + VIETNAM_UTC_OFFSET_HOURS >= 24) {
    if (++dt.day > daysInMonth(dt.year, dt.month)) {
      dt.day = 1;
      if (++dt.month > 12) {
        dt.month = 1;
        ++dt.year;
      }
    }
  }
  return static_cast<uint32_t>(dt.year) * 10000U + static_cast<uint32_t>(dt.month) * 100U + dt.day;
}

bool isSha256Hex(const std::string& value) {
  if (value.size() != SHA256_HEX_LENGTH) return false;
  for (const char ch : value) {
    if (!std::isxdigit(static_cast<unsigned char>(ch))) return false;
  }
  return true;
}

std::string formatDateKey(const uint32_t dateKey) {
  if (dateKey == 0) return "--/--/----";
  char value[11];
  snprintf(value, sizeof(value), "%02lu/%02lu/%04lu", static_cast<unsigned long>(dateKey % 100),
           static_cast<unsigned long>((dateKey / 100) % 100), static_cast<unsigned long>(dateKey / 10000));
  return value;
}
}  // namespace

void VanNhanSoUpdateActivity::onEnter() {
  Activity::onEnter();
  resumeReaderAfterRestart = APP_STATE.lastSleepFromReader && !APP_STATE.openEpubPath.empty();

  if (automatic) {
    startAutomaticUpdate();
    return;
  }

  resolveCurrentDate(false);
  state = STATUS;
  requestUpdate();
}

void VanNhanSoUpdateActivity::onExit() {
  Activity::onExit();

  if (shouldTearDownWifiOnExit && WiFi.getMode() != WIFI_MODE_NULL) {
    WiFi.disconnect(false);
    delay(30);
    if (automatic && resumeReaderAfterRestart) {
      silentRestartToReader();
    } else {
      silentRestart();
    }
  }
}

void VanNhanSoUpdateActivity::beginManualUpdate() {
  shouldTearDownWifiOnExit = WiFi.status() != WL_CONNECTED;
  cancelDownload = false;

  if (WiFi.status() == WL_CONNECTED) {
    resolveCurrentDate(true);
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

  if (resolveCurrentDate(false) && isCurrentCache()) {
    LOG_INF("VNS", "Sleep screen already current for %lu", static_cast<unsigned long>(currentDateKey));
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
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true, true);
  delay(100);
  WiFi.setScanMethod(WIFI_ALL_CHANNEL_SCAN);
  WiFi.setSortMethod(WIFI_CONNECT_AP_BY_SIGNAL);
  WiFi.begin(credential->ssid.c_str(), credential->password.c_str());

  shouldTearDownWifiOnExit = true;
  connectionStartTime = millis();
  state = AUTO_CONNECTING;
  requestUpdate();
}

void VanNhanSoUpdateActivity::checkAutomaticConnection() {
  const wl_status_t status = WiFi.status();
  if (status == WL_CONNECTED) {
    if (resolveCurrentDate(true) && isCurrentCache()) {
      LOG_INF("VNS", "Sleep screen already current for %lu", static_cast<unsigned long>(currentDateKey));
      state = SKIPPED;
      finish();
      return;
    }

    recordAttempt();
    state = DOWNLOADING;
    requestUpdateAndWait();
    downloadSleepScreen();
    finish();
    return;
  }

  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL ||
      millis() - connectionStartTime > AUTO_CONNECTION_TIMEOUT_MS) {
    LOG_ERR("VNS", "Automatic update WiFi connection failed");
    fail(CrossPointState::VanNhanSoUpdateError::WIFI_TIMEOUT);
    finish();
  }
}

void VanNhanSoUpdateActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    state = STATUS;
    requestUpdate();
    return;
  }

  resolveCurrentDate(true);
  recordAttempt();
  state = DOWNLOADING;
  requestUpdate();
}

void VanNhanSoUpdateActivity::loop() {
  if (state == AUTO_CONNECTING) {
    int x = 0;
    int y = 0;
    if (mappedInput.wasAnyPressed() || mappedInput.wasScreenTapped(x, y)) {
      LOG_INF("VNS", "Automatic update cancelled by user input");
      finish();
      return;
    }
    checkAutomaticConnection();
    return;
  }

  if (state == DOWNLOADING) {
    requestUpdateAndWait();
    resolveCurrentDate(true);
    downloadSleepScreen();
    if (automatic) finish();
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (!automatic && mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
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

  if (halClock.isAvailable() && SETTINGS.clockHasBeenSynced) {
    haveDate = halClock.getDateTime(dateTime);
  }
  if (!haveDate) {
    haveDate = halClock.getSystemDateTime(dateTime);
  }

  if (!haveDate && allowNetworkSync && WiFi.status() == WL_CONNECTED) {
    if (halClock.isAvailable()) {
      if (halClock.syncFromNTP()) {
        SETTINGS.clockHasBeenSynced = 1;
        SETTINGS.saveToFile();
        haveDate = halClock.getDateTime(dateTime);
      }
    } else {
      haveDate = halClock.syncSystemTimeFromNTP(dateTime);
    }
  }

  currentDateKey = haveDate ? vietnamDateKey(dateTime) : 0;
  return currentDateKey != 0;
}

bool VanNhanSoUpdateActivity::isCurrentCache() const {
  if (currentDateKey == 0 || !Storage.exists(CACHE_PATH)) return false;
  uint32_t storedDateKey = 0;
  return readDateMarker(storedDateKey) && storedDateKey == currentDateKey;
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

bool VanNhanSoUpdateActivity::writeDateMarker(const uint32_t dateKey) const {
  char value[9];
  snprintf(value, sizeof(value), "%08lu", static_cast<unsigned long>(dateKey));

  Storage.remove(DATE_TEMP_PATH);
  HalFile file;
  if (!Storage.openFileForWrite("VNS", DATE_TEMP_PATH, file)) return false;
  if (file.write(reinterpret_cast<const uint8_t*>(value), 8) != 8) {
    file.close();
    Storage.remove(DATE_TEMP_PATH);
    return false;
  }
  file.close();

  Storage.remove(DATE_PATH);
  if (!Storage.rename(DATE_TEMP_PATH, DATE_PATH)) {
    Storage.remove(DATE_TEMP_PATH);
    return false;
  }
  return true;
}

void VanNhanSoUpdateActivity::downloadSleepScreen() {
  const bool isX3 = renderer.getScreenWidth() >= 528;
  const char* url = isX3 ? VANNHANSO_X3_URL : VANNHANSO_X4_URL;

  downloadedBytes = 0;
  totalBytes = 0;
  cancelDownload = false;
  HttpDownloader::ResponseInfo responseInfo;

  // HttpDownloader owns the only short-lived URL/path allocations. The image
  // body itself is streamed directly to SD and never buffered in heap.
  const auto result = HttpDownloader::downloadToFile(
      url, TEMP_PATH,
      [this](const size_t downloaded, const size_t total) {
        const int oldPercentage = totalBytes > 0 ? static_cast<int>(downloadedBytes * 100 / totalBytes) : -1;
        downloadedBytes = downloaded;
        totalBytes = total;
        const int newPercentage = total > 0 ? static_cast<int>(downloaded * 100 / total) : -1;
        if (newPercentage != oldPercentage) requestUpdate();

        // Automatic refresh must yield to the user. A transfer that is already
        // receiving data can be cancelled immediately; a stalled socket is
        // bounded by DOWNLOAD_TIMEOUT_MS below.
        if (automatic) {
          mappedInput.update();
          int x = 0;
          int y = 0;
          if (mappedInput.wasAnyPressed() || mappedInput.wasScreenTapped(x, y)) cancelDownload = true;
        }
      },
      &cancelDownload, "", "", &responseInfo, DOWNLOAD_TIMEOUT_MS);

  if (result != HttpDownloader::OK) {
    Storage.remove(TEMP_PATH);
    fail(CrossPointState::VanNhanSoUpdateError::DOWNLOAD);
    return;
  }

  state = VERIFYING;
  requestUpdateAndWait();
  if (!isSha256Hex(responseInfo.sha256)) {
    LOG_ERR("VNS", "Missing or invalid X-Content-SHA256 response header");
    Storage.remove(TEMP_PATH);
    fail(CrossPointState::VanNhanSoUpdateError::CHECKSUM_MISSING);
    return;
  }
  if (!validateChecksum(responseInfo.sha256)) {
    Storage.remove(TEMP_PATH);
    fail(CrossPointState::VanNhanSoUpdateError::CHECKSUM_MISMATCH);
    return;
  }
  if (!validateDownloadedFile()) {
    Storage.remove(TEMP_PATH);
    fail(CrossPointState::VanNhanSoUpdateError::INVALID_IMAGE);
    return;
  }

  state = INSTALLING;
  requestUpdateAndWait();
  if (!installDownloadedFile()) {
    Storage.remove(TEMP_PATH);
    fail(CrossPointState::VanNhanSoUpdateError::INSTALL);
    return;
  }

  if (currentDateKey != 0 && !writeDateMarker(currentDateKey)) {
    LOG_ERR("VNS", "Could not save update date marker");
  }

  recordSuccess();
  state = SUCCESS;
  requestUpdate();
}

void VanNhanSoUpdateActivity::recordAttempt() {
  APP_STATE.vanNhanSoUpdateResult = CrossPointState::VanNhanSoUpdateResult::IN_PROGRESS;
  APP_STATE.vanNhanSoUpdateError = CrossPointState::VanNhanSoUpdateError::NONE;
  APP_STATE.vanNhanSoLastAttemptDate = currentDateKey;
  APP_STATE.saveToFile();
}

void VanNhanSoUpdateActivity::recordSuccess() {
  APP_STATE.vanNhanSoUpdateResult = CrossPointState::VanNhanSoUpdateResult::SUCCESS;
  APP_STATE.vanNhanSoUpdateError = CrossPointState::VanNhanSoUpdateError::NONE;
  APP_STATE.vanNhanSoLastAttemptDate = currentDateKey;
  APP_STATE.vanNhanSoLastSuccessDate = currentDateKey;
  APP_STATE.saveToFile();
}

void VanNhanSoUpdateActivity::fail(const CrossPointState::VanNhanSoUpdateError error) {
  APP_STATE.vanNhanSoUpdateResult = CrossPointState::VanNhanSoUpdateResult::FAILED;
  APP_STATE.vanNhanSoUpdateError = error;
  APP_STATE.vanNhanSoLastAttemptDate = currentDateKey;
  APP_STATE.saveToFile();
  state = FAILED;
  requestUpdate();
}

bool VanNhanSoUpdateActivity::validateDownloadedFile() const {
  HalFile file;
  if (!Storage.openFileForRead("VNS", TEMP_PATH, file)) return false;

  Bitmap bitmap(file, true);
  const auto result = bitmap.parseHeaders();
  if (result != BmpReaderError::Ok) {
    LOG_ERR("VNS", "Downloaded BMP is invalid: %s", Bitmap::errorToString(result));
    return false;
  }

  if (!bitmap.is1Bit() || bitmap.getWidth() != renderer.getScreenWidth() ||
      bitmap.getHeight() != renderer.getScreenHeight()) {
    LOG_ERR("VNS", "Wrong BMP format or dimensions: %dx%d %ubpp", bitmap.getWidth(), bitmap.getHeight(),
            bitmap.getBpp());
    return false;
  }

  return true;
}

bool VanNhanSoUpdateActivity::validateChecksum(const std::string& expectedChecksum) const {
  HalFile file;
  if (!Storage.openFileForRead("VNS", TEMP_PATH, file)) return false;

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
    LOG_ERR("VNS", "Downloaded sleep-screen checksum mismatch");
  } else {
    LOG_INF("VNS", "Verified sleep-screen SHA-256: %s", actualChecksum);
  }
  return matches;
}

bool VanNhanSoUpdateActivity::installDownloadedFile() const {
  Storage.remove(BACKUP_PATH);
  const bool hadCache = Storage.exists(CACHE_PATH);

  if (hadCache && !Storage.rename(CACHE_PATH, BACKUP_PATH)) {
    LOG_ERR("VNS", "Could not back up the current sleep screen");
    return false;
  }

  if (!Storage.rename(TEMP_PATH, CACHE_PATH)) {
    LOG_ERR("VNS", "Could not install the downloaded sleep screen");
    if (hadCache) Storage.rename(BACKUP_PATH, CACHE_PATH);
    return false;
  }

  Storage.remove(BACKUP_PATH);
  return true;
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
    case CrossPointState::VanNhanSoUpdateError::NONE:
    default:
      return "";
  }
}

void VanNhanSoUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_VANNHANSO));

  const int midY = pageHeight / 2;
  uint32_t cachedDateKey = 0;
  readDateMarker(cachedDateKey);
  const std::string cachedDate = formatDateKey(cachedDateKey);
  char currentText[80];
  snprintf(currentText, sizeof(currentText), tr(STR_VANNHANSO_CURRENT_DATA), cachedDate.c_str());

  if (state == WIFI_SELECTION || state == AUTO_CONNECTING) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY, tr(STR_CONNECTING));
  } else if (state == DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY - 20, tr(STR_VANNHANSO_DOWNLOADING));
    if (totalBytes > 0) {
      char progress[16];
      snprintf(progress, sizeof(progress), "%u%%", static_cast<unsigned>(downloadedBytes * 100 / totalBytes));
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
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 5, errorText(APP_STATE.vanNhanSoUpdateError));
    renderer.drawCenteredText(UI_10_FONT_ID, midY + 40, currentText);
  } else {
    renderer.drawCenteredText(UI_12_FONT_ID, midY - 40, tr(STR_VANNHANSO_UPDATE_STATUS), true, EpdFontFamily::BOLD);
    renderer.drawCenteredText(UI_10_FONT_ID, midY, currentText);
    if (APP_STATE.vanNhanSoUpdateResult == CrossPointState::VanNhanSoUpdateResult::FAILED) {
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 38, errorText(APP_STATE.vanNhanSoUpdateError));
    } else if (cachedDateKey != 0 && cachedDateKey == currentDateKey) {
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 38, tr(STR_VANNHANSO_CACHE_CURRENT));
    } else if (cachedDateKey != 0) {
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 38, tr(STR_VANNHANSO_CACHE_OLD));
    } else {
      renderer.drawCenteredText(UI_10_FONT_ID, midY + 38, tr(STR_VANNHANSO_CACHE_EMPTY));
    }
  }

  if (!automatic && (state == STATUS || state == SUCCESS || state == FAILED || state == SKIPPED)) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), state == FAILED ? tr(STR_RETRY) : tr(STR_UPDATE), "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  } else if (state == WIFI_SELECTION || state == AUTO_CONNECTING || state == DOWNLOADING) {
    const auto labels = mappedInput.mapLabels(tr(STR_CANCEL), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
