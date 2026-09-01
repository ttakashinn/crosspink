#include "VanNhanSoSettingsActivity.h"

#include <CrossPointSettings.h>
#include <CrossPointState.h>
#include <GfxRenderer.h>
#include <I18n.h>
#include <Memory.h>

#include <algorithm>
#include <iterator>
#include <utility>

#include "MappedInputManager.h"
#include "VanNhanSoUpdateActivity.h"
#include "components/UITheme.h"
#include "features/vannhanso/VanNhanSoCache.h"
#include "features/vannhanso/VanNhanSoProfile.h"
#include "features/vannhanso/VanNhanSoUpdatePolicy.h"

namespace fui = freeink::ui;

namespace {
constexpr int BASE_ITEM_COUNT = 3;
constexpr int FULL_ITEM_COUNT = 6;

const StrId layoutNames[] = {StrId::STR_VANNHANSO_LAYOUT_MINIMAL, StrId::STR_VANNHANSO_LAYOUT_FULL};
const StrId fontNames[] = {StrId::STR_VANNHANSO_FONT_STANDARD, StrId::STR_VANNHANSO_FONT_LARGE};
const StrId vocabularyNames[] = {StrId::STR_VANNHANSO_VOCAB_B1, StrId::STR_VANNHANSO_VOCAB_B2,
                                 StrId::STR_VANNHANSO_VOCAB_C1, StrId::STR_VANNHANSO_VOCAB_C2,
                                 StrId::STR_VANNHANSO_VOCAB_MIXED};
const StrId weatherNames[] = {StrId::STR_VANNHANSO_WEATHER_HANOI,   StrId::STR_VANNHANSO_WEATHER_HOCHIMINH,
                              StrId::STR_VANNHANSO_WEATHER_DANANG,  StrId::STR_VANNHANSO_WEATHER_HAIPHONG,
                              StrId::STR_VANNHANSO_WEATHER_CANTHO,  StrId::STR_VANNHANSO_WEATHER_HUE,
                              StrId::STR_VANNHANSO_WEATHER_DONGNAI, StrId::STR_VANNHANSO_WEATHER_NAMDINH};
static_assert(std::size(weatherNames) == CrossPointSettings::VANNHANSO_WEATHER_LOCATION_COUNT);
}  // namespace

VanNhanSoSettingsActivity::VanNhanSoSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput,
                                                     const bool returnToSettingsOnBack)
    : UiListActivity("VanNhanSoSettings", renderer, mappedInput), returnToSettingsOnBack(returnToSettingsOnBack) {}

void VanNhanSoSettingsActivity::onEnter() {
  UiListActivity::onEnter();
  syncPendingProfile();

  for (int i = 0; i < MAX_ITEM_COUNT; ++i) {
    rowItems_[i].label = I18N.get(itemName(i));
    rowItems_[i].actionValue = static_cast<int16_t>(i);
  }
}

int VanNhanSoSettingsActivity::listCount() const {
  return SETTINGS.vanNhanSoLayout == CrossPointSettings::VANNHANSO_LAYOUT_FULL ? FULL_ITEM_COUNT : BASE_ITEM_COUNT;
}

const char* VanNhanSoSettingsActivity::headerTitle() const { return tr(STR_VANNHANSO); }

StrId VanNhanSoSettingsActivity::itemName(const int index) const {
  static constexpr StrId names[FULL_ITEM_COUNT] = {
      StrId::STR_VANNHANSO_REFRESH,          StrId::STR_VANNHANSO_LAYOUT,           StrId::STR_VANNHANSO_FONT_SIZE,
      StrId::STR_VANNHANSO_VOCABULARY_LEVEL, StrId::STR_VANNHANSO_WEATHER_LOCATION, StrId::STR_VANNHANSO_FINANCE};
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
      return I18N.get(layoutNames[std::min<uint8_t>(SETTINGS.vanNhanSoLayout, std::size(layoutNames) - 1)]);
    case 2:
      return I18N.get(fontNames[std::min<uint8_t>(SETTINGS.vanNhanSoFontSize, std::size(fontNames) - 1)]);
    case 3:
      return I18N.get(
          vocabularyNames[std::min<uint8_t>(SETTINGS.vanNhanSoVocabularyLevel, std::size(vocabularyNames) - 1)]);
    case 4:
      return I18N.get(weatherNames[std::min<uint8_t>(SETTINGS.vanNhanSoWeatherLocation, std::size(weatherNames) - 1)]);
    case 5:
      return SETTINGS.vanNhanSoFinance ? tr(STR_STATE_ON) : tr(STR_STATE_OFF);
    default:
      return "";
  }
}

void VanNhanSoSettingsActivity::handleSelection(const int index) {
  switch (index) {
    case 0: {
      auto activity = makeUniqueNoThrow<VanNhanSoUpdateActivity>(renderer, mappedInput,
                                                                 vannhanso_update_policy::UpdateTrigger::MANUAL,
                                                                 /*returnToVanNhanSoSettings=*/true);
      if (activity) startActivityForResult(std::move(activity), [this](const ActivityResult&) { requestUpdate(); });
      return;
    }
    case 1:
      optionPopup.show(StrId::STR_VANNHANSO_LAYOUT, layoutNames, std::size(layoutNames), SETTINGS.vanNhanSoLayout,
                       [this](const int index) {
                         SETTINGS.vanNhanSoLayout = index;
                         SETTINGS.saveToFile();
                         syncPendingProfile();
                         moveSelectionTo(std::min(nav.selected, listCount() - 1));
                       });
      return;
    case 2:
      optionPopup.show(StrId::STR_VANNHANSO_FONT_SIZE, fontNames, std::size(fontNames), SETTINGS.vanNhanSoFontSize,
                       [this](const int index) {
                         SETTINGS.vanNhanSoFontSize = index;
                         SETTINGS.saveToFile();
                         syncPendingProfile();
                       });
      return;
    case 3:
      optionPopup.show(StrId::STR_VANNHANSO_VOCABULARY_LEVEL, vocabularyNames, std::size(vocabularyNames),
                       SETTINGS.vanNhanSoVocabularyLevel, [this](const int index) {
                         SETTINGS.vanNhanSoVocabularyLevel = index;
                         SETTINGS.saveToFile();
                         syncPendingProfile();
                       });
      return;
    case 4:
      optionPopup.show(StrId::STR_VANNHANSO_WEATHER_LOCATION, weatherNames, std::size(weatherNames),
                       SETTINGS.vanNhanSoWeatherLocation, [this](const int index) {
                         SETTINGS.vanNhanSoWeatherLocation = index;
                         SETTINGS.saveToFile();
                         syncPendingProfile();
                       });
      return;
    case 5:
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

bool VanNhanSoSettingsActivity::handleCustomInput() {
  return optionPopup.handleInput(mappedInput, [this] { requestUpdate(); });
}

void VanNhanSoSettingsActivity::onBackButton() {
  if (returnToSettingsOnBack) {
    activityManager.goToSettings();
  } else {
    finish();
  }
}

void VanNhanSoSettingsActivity::activateIndex(const int index) {
  if (optionPopup.isActive()) return;
  nav.selected = index;
  app.clearTapFlash();
  handleSelection(index);
  requestUpdate();
}

void VanNhanSoSettingsActivity::buildScreen(UiScreen& screen) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  screen.setContentMargin(fui::Insets{static_cast<int16_t>(metrics.topPadding + metrics.headerHeight), 0, 0, 0});
  screen.spacer(static_cast<int16_t>(metrics.verticalSpacing));

  const int count = listCount();
  for (int i = 0; i < count; ++i) {
    rowValues_[i] = itemValue(i);
    rowItems_[i].value = rowValues_[i].empty() ? nullptr : rowValues_[i].c_str();
  }

  fui::ListProps props;
  props.items = rowItems_;
  props.count = static_cast<uint16_t>(count);
  props.action = ACTION_ROW;
  props.inputMask = fui::InputTouch;
  props.valueInset = 8;
  props.labelText = screen.theme().bodyText;
  props.labelText.maxLines = 2;
  syncListViewport(screen, props);
  screen.list(props);
}

void VanNhanSoSettingsActivity::render(RenderLock&& lock) {
  if (optionPopup.processRender(renderer, mappedInput)) return;
  UiListActivity::render(std::move(lock));
}
