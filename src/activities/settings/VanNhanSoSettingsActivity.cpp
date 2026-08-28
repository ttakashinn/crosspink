#include "VanNhanSoSettingsActivity.h"

#include <CrossPointSettings.h>
#include <CrossPointState.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <iterator>

#include "MappedInputManager.h"
#include "VanNhanSoUpdateActivity.h"
#include "components/UITheme.h"
#include "features/vannhanso/VanNhanSoCache.h"
#include "features/vannhanso/VanNhanSoProfile.h"
#include "features/vannhanso/VanNhanSoUpdatePolicy.h"

namespace {
constexpr int BASE_ITEM_COUNT = 4;
constexpr int FULL_ITEM_COUNT = 7;

const StrId updateModeNames[] = {StrId::STR_VANNHANSO_UPDATE_FIRST_BOOT, StrId::STR_VANNHANSO_UPDATE_ON_SLEEP,
                                 StrId::STR_VANNHANSO_UPDATE_MANUAL};
const StrId layoutNames[] = {StrId::STR_VANNHANSO_LAYOUT_MINIMAL, StrId::STR_VANNHANSO_LAYOUT_FULL};
const StrId fontNames[] = {StrId::STR_VANNHANSO_FONT_STANDARD, StrId::STR_VANNHANSO_FONT_LARGE};
const StrId vocabularyNames[] = {StrId::STR_VANNHANSO_VOCAB_B1, StrId::STR_VANNHANSO_VOCAB_B2,
                                 StrId::STR_VANNHANSO_VOCAB_C1, StrId::STR_VANNHANSO_VOCAB_C2,
                                 StrId::STR_VANNHANSO_VOCAB_MIXED};
const StrId weatherNames[] = {StrId::STR_VANNHANSO_WEATHER_HANOI,   StrId::STR_VANNHANSO_WEATHER_HOCHIMINH,
                              StrId::STR_VANNHANSO_WEATHER_DANANG,  StrId::STR_VANNHANSO_WEATHER_HAIPHONG,
                              StrId::STR_VANNHANSO_WEATHER_CANTHO,  StrId::STR_VANNHANSO_WEATHER_HUE,
                              StrId::STR_VANNHANSO_WEATHER_DONGNAI};
}  // namespace

void VanNhanSoSettingsActivity::onEnter() {
  Activity::onEnter();
  selectedIndex = 0;
  syncPendingProfile();
  requestUpdate();
}

int VanNhanSoSettingsActivity::itemCount() const {
  return SETTINGS.vanNhanSoLayout == CrossPointSettings::VANNHANSO_LAYOUT_FULL ? FULL_ITEM_COUNT : BASE_ITEM_COUNT;
}

StrId VanNhanSoSettingsActivity::itemName(const int index) const {
  static constexpr StrId names[FULL_ITEM_COUNT] = {
      StrId::STR_VANNHANSO_REFRESH,          StrId::STR_VANNHANSO_UPDATE_MODE,      StrId::STR_VANNHANSO_LAYOUT,
      StrId::STR_VANNHANSO_FONT_SIZE,        StrId::STR_VANNHANSO_VOCABULARY_LEVEL, StrId::STR_VANNHANSO_WEATHER_LOCATION,
      StrId::STR_VANNHANSO_FINANCE};
  return names[index >= 0 && index < FULL_ITEM_COUNT ? index : 0];
}

std::string VanNhanSoSettingsActivity::itemValue(const int index) const {
  switch (index) {
    case 0: {
      uint32_t dateKey;
      if (vannhanso_cache::hasCurrentProfileImage(renderer.getScreenWidth(), renderer.getScreenHeight()) &&
          vannhanso_cache::readCurrentDate(renderer.getScreenWidth(), renderer.getScreenHeight(), dateKey)) {
        char date[11];
        snprintf(date, sizeof(date), "%02lu/%02lu/%04lu", static_cast<unsigned long>(dateKey % 100),
                 static_cast<unsigned long>((dateKey / 100) % 100), static_cast<unsigned long>(dateKey / 10000));
        return date;
      }
      return APP_STATE.vanNhanSoPendingProfileHash ==
                     vannhanso_profile::identityHash(renderer.getScreenWidth(), renderer.getScreenHeight())
                 ? tr(STR_VANNHANSO_PROFILE_PENDING)
                 : tr(STR_NOT_SET);
    }
    case 1:
      return I18N.get(updateModeNames[std::min<uint8_t>(SETTINGS.vanNhanSoUpdateMode, std::size(updateModeNames) - 1)]);
    case 2:
      return I18N.get(layoutNames[std::min<uint8_t>(SETTINGS.vanNhanSoLayout, std::size(layoutNames) - 1)]);
    case 3:
      return I18N.get(fontNames[std::min<uint8_t>(SETTINGS.vanNhanSoFontSize, std::size(fontNames) - 1)]);
    case 4:
      return I18N.get(vocabularyNames[
          std::min<uint8_t>(SETTINGS.vanNhanSoVocabularyLevel, std::size(vocabularyNames) - 1)]);
    case 5:
      return I18N.get(weatherNames[
          std::min<uint8_t>(SETTINGS.vanNhanSoWeatherLocation, std::size(weatherNames) - 1)]);
    case 6:
      return SETTINGS.vanNhanSoFinance ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    default:
      return "";
  }
}

void VanNhanSoSettingsActivity::handleSelection() {
  switch (selectedIndex) {
    case 0: {
      auto activity = makeUniqueNoThrow<VanNhanSoUpdateActivity>(renderer, mappedInput);
      if (activity) startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
      return;
    }
    case 1:
      optionPopup.show(StrId::STR_VANNHANSO_UPDATE_MODE, updateModeNames, std::size(updateModeNames),
                       SETTINGS.vanNhanSoUpdateMode, [](const int index) {
                         SETTINGS.vanNhanSoUpdateMode = index;
                         SETTINGS.saveToFile();
                       });
      return;
    case 2:
      optionPopup.show(StrId::STR_VANNHANSO_LAYOUT, layoutNames, std::size(layoutNames), SETTINGS.vanNhanSoLayout,
                       [this](const int index) {
                         SETTINGS.vanNhanSoLayout = index;
                         SETTINGS.saveToFile();
                         syncPendingProfile();
                         selectedIndex = std::min(selectedIndex, itemCount() - 1);
                       });
      return;
    case 3:
      optionPopup.show(StrId::STR_VANNHANSO_FONT_SIZE, fontNames, std::size(fontNames), SETTINGS.vanNhanSoFontSize,
                       [this](const int index) {
                         SETTINGS.vanNhanSoFontSize = index;
                         SETTINGS.saveToFile();
                         syncPendingProfile();
                       });
      return;
    case 4:
      optionPopup.show(StrId::STR_VANNHANSO_VOCABULARY_LEVEL, vocabularyNames, std::size(vocabularyNames),
                       SETTINGS.vanNhanSoVocabularyLevel, [this](const int index) {
                         SETTINGS.vanNhanSoVocabularyLevel = index;
                         SETTINGS.saveToFile();
                         syncPendingProfile();
                       });
      return;
    case 5:
      optionPopup.show(StrId::STR_VANNHANSO_WEATHER_LOCATION, weatherNames, std::size(weatherNames),
                       SETTINGS.vanNhanSoWeatherLocation, [this](const int index) {
                         SETTINGS.vanNhanSoWeatherLocation = index;
                         SETTINGS.saveToFile();
                         syncPendingProfile();
                       });
      return;
    case 6:
      SETTINGS.vanNhanSoFinance = !SETTINGS.vanNhanSoFinance;
      SETTINGS.saveToFile();
      syncPendingProfile();
      return;
    default:
      return;
  }
}

void VanNhanSoSettingsActivity::syncPendingProfile() {
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  const uint32_t profileHash = vannhanso_profile::identityHash(width, height);
  const uint32_t pendingHash =
      vannhanso_update_policy::pendingProfileHash(vannhanso_cache::hasCurrentProfileImage(width, height), profileHash);
  if (APP_STATE.vanNhanSoPendingProfileHash == pendingHash) return;

  APP_STATE.vanNhanSoPendingProfileHash = pendingHash;
  APP_STATE.saveToFile();
}

void VanNhanSoSettingsActivity::loop() {
  if (optionPopup.handleInput(mappedInput, [this] { requestUpdate(); })) return;

  const auto& metrics = UITheme::getInstance().getMetrics();
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight =
      renderer.getScreenHeight() - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  const int count = itemCount();
  switch (handleListTouch(selectedIndex, count, contentTop, contentHeight, false)) {
    case ListTouchResult::Activated:
      handleSelection();
      requestUpdate();
      return;
    case ListTouchResult::Consumed:
      return;
    case ListTouchResult::None:
      break;
  }

  if (mappedInput.wasPressed(MappedInputManager::Button::Back)) {
    finish();
    return;
  }
  if (mappedInput.wasPressed(MappedInputManager::Button::Confirm)) {
    handleSelection();
    requestUpdate();
    return;
  }
  buttonNavigator.onNext([this] {
    selectedIndex = ButtonNavigator::nextIndex(selectedIndex, itemCount());
    requestUpdate();
  });
  buttonNavigator.onPrevious([this] {
    selectedIndex = ButtonNavigator::previousIndex(selectedIndex, itemCount());
    requestUpdate();
  });
}

void VanNhanSoSettingsActivity::render(RenderLock&&) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  renderer.clearScreen();
  const auto& metrics = UITheme::getInstance().getMetrics();
  const int width = renderer.getScreenWidth();
  const int height = renderer.getScreenHeight();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, width, metrics.headerHeight}, tr(STR_VANNHANSO));
  const int contentTop = metrics.topPadding + metrics.headerHeight + metrics.verticalSpacing;
  const int contentHeight = height - contentTop - metrics.buttonHintsHeight - metrics.verticalSpacing * 2;
  GUI.drawList(renderer, Rect{0, contentTop, width, contentHeight}, itemCount(), selectedIndex,
               [this](const int index) { return std::string(I18N.get(itemName(index))); }, nullptr, nullptr,
               [this](const int index) { return itemValue(index); }, true);
  const auto labels = mappedInput.mapLabels(tr(STR_BACK), tr(STR_SELECT), tr(STR_DIR_UP), tr(STR_DIR_DOWN));
  GUI.drawButtonHints(renderer, labels.btn1, labels.btn2, labels.btn3, labels.btn4);
  renderer.displayBuffer();
}
