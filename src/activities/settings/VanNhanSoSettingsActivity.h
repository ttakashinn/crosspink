#pragma once

#include "activities/Activity.h"
#include "components/OptionPopup.h"
#include "util/ButtonNavigator.h"

class VanNhanSoSettingsActivity final : public Activity {
 public:
  explicit VanNhanSoSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput)
      : Activity("VanNhanSoSettings", renderer, mappedInput) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  ButtonNavigator buttonNavigator;
  OptionPopup optionPopup;
  int selectedIndex = 0;

  int itemCount() const;
  StrId itemName(int index) const;
  std::string itemValue(int index) const;
  void handleSelection();
};
