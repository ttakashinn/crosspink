#pragma once

#include <string>

#include "activities/UiListActivity.h"
#include "components/OptionPopup.h"

class VanNhanSoSettingsActivity final : public UiListActivity {
 public:
  explicit VanNhanSoSettingsActivity(GfxRenderer& renderer, MappedInputManager& mappedInput);

  void onEnter() override;
  void render(RenderLock&&) override;

 private:
  static constexpr int MAX_ITEM_COUNT = 7;

  OptionPopup optionPopup;
  std::string rowValues_[MAX_ITEM_COUNT];
  freeink::ui::ListItem rowItems_[MAX_ITEM_COUNT]{};

  int listCount() const override;
  void buildScreen(UiScreen& screen) override;
  void activateIndex(int index) override;
  bool handleCustomInput() override;
  const char* headerTitle() const override;

  StrId itemName(int index) const;
  std::string itemValue(int index) const;
  void handleSelection(int index);
  void syncPendingProfile();
};
