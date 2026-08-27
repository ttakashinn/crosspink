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

bool isLeapYear(const uint16_t year) { return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0; }

uint8_t daysInMonth(const uint16_t year, const uint8_t month) {
  static constexpr uint8_t DAYS[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
  if (month == 2 && isLeapYear(year)) return 29;
  return DAYS[month - 1];
}

uint32_t vietnamDateKey(Rtc::DateTime dt) {
  if (dt.year < 2024 || dt.month < 1 || dt.month > 12 || dt.day < 1 ||
      dt.day > daysInMonth(dt.year, dt.month) || dt.hour > 23) {
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
}  // namespace

void VanNhanSoUpdateActivity::onEnter() {
  Activity::onEnter();
  resumeReaderAfterRestart = APP_STATE.lastSleepFromReader && !APP_STATE.openEpubPath.empty();

  if (automatic) {
    startAutomaticUpdate();
    return;
  }

  state = WIFI_SELECTION;
  shouldTearDownWifiOnExit = WiFi.status() != WL_CONNECTED;

  if (WiFi.status() == WL_CONNECTED) {
    state = DOWNLOADING;
    requestUpdate();
    return;
  }

  WiFi.mode(WIFI_STA);
  auto wifiActivity = makeUniqueNoThrow<WifiSelectionActivity>(renderer, mappedInput);
  if (!wifiActivity) {
    state = FAILED;
    requestUpdate();
    return;
  }
  startActivityForResult(std::move(wifiActivity),
                         [this](const ActivityResult& result) { onWifiSelectionComplete(!result.isCancelled); });
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
    state = SKIPPED;
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

    state = DOWNLOADING;
    requestUpdateAndWait();
    downloadSleepScreen();
    finish();
    return;
  }

  if (status == WL_CONNECT_FAILED || status == WL_NO_SSID_AVAIL ||
      millis() - connectionStartTime > AUTO_CONNECTION_TIMEOUT_MS) {
    LOG_ERR("VNS", "Automatic update WiFi connection failed");
    state = FAILED;
    finish();
  }
}

void VanNhanSoUpdateActivity::onWifiSelectionComplete(const bool connected) {
  if (!connected) {
    finish();
    return;
  }

  state = DOWNLOADING;
  requestUpdate();
}

void VanNhanSoUpdateActivity::loop() {
  if (state == AUTO_CONNECTING) {
    checkAutomaticConnection();
    return;
  }

  if (state == DOWNLOADING) {
    requestUpdateAndWait();
    resolveCurrentDate(true);
    downloadSleepScreen();
    return;
  }

  int x = 0;
  int y = 0;
  if (mappedInput.wasPressed(MappedInputManager::Button::Back) || mappedInput.wasScreenTapped(x, y)) finish();
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

  // HttpDownloader owns the only short-lived URL/path allocations. The image
  // body itself is streamed directly to SD and never buffered in heap.
  const auto result = HttpDownloader::downloadToFile(url, TEMP_PATH, nullptr);
  if (result != HttpDownloader::OK || !validateDownloadedFile() || !installDownloadedFile()) {
    Storage.remove(TEMP_PATH);
    state = FAILED;
    requestUpdate();
    return;
  }

  if (currentDateKey != 0 && !writeDateMarker(currentDateKey)) {
    LOG_ERR("VNS", "Could not save update date marker");
  }

  state = SUCCESS;
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

void VanNhanSoUpdateActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  const auto pageWidth = renderer.getScreenWidth();
  const auto pageHeight = renderer.getScreenHeight();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, pageWidth, metrics.headerHeight}, tr(STR_VANNHANSO));

  const int midY = pageHeight / 2;
  if (state == WIFI_SELECTION || state == AUTO_CONNECTING) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY, tr(STR_CONNECTING));
  } else if (state == DOWNLOADING) {
    renderer.drawCenteredText(UI_10_FONT_ID, midY, tr(STR_VANNHANSO_DOWNLOADING));
  } else if (state == SUCCESS || state == SKIPPED) {
    renderer.drawCenteredText(UI_12_FONT_ID, midY, tr(STR_VANNHANSO_UPDATED), true, EpdFontFamily::BOLD);
  } else {
    renderer.drawCenteredText(UI_12_FONT_ID, midY, tr(STR_VANNHANSO_UPDATE_FAILED), true,
                              EpdFontFamily::BOLD);
  }

  if (state == SUCCESS || state == FAILED || state == SKIPPED) {
    const auto labels = mappedInput.mapLabels(tr(STR_BACK), "", "", "");
    GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  }

  renderer.displayBuffer();
}
