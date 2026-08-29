#include "BackupReadingStatsActivity.h"

#include <GfxRenderer.h>
#include <I18n.h>

#include "MappedInputManager.h"
#include "activities/reader/ReadingStatsBackup.h"
#include "components/UITheme.h"
#include "fontIds.h"

void BackupReadingStatsActivity::onEnter() {
  Activity::onEnter();
  const char* options[] = {tr(STR_CANCEL), tr(STR_CONFIRM)};
  popup.show(tr(STR_BACKUP_STATS), options, 2, 0, [this](const int index) {
    if (index == 1)
      runBackup();
    else
      finish();
  });
  requestUpdate(true);
}

void BackupReadingStatsActivity::runBackup() {
  state = ReadingStatsBackup::create(fileName, sizeof(fileName)) ? State::SUCCESS : State::FAILED;
  requestUpdate(true);
}

void BackupReadingStatsActivity::loop() {
  if (state == State::CONFIRM) {
    if (popup.handleInput(mappedInput, [this] { requestUpdate(); })) return;
    finish();
    return;
  }
  int x = 0;
  int y = 0;
  if (mappedInput.wasReleased(MappedInputManager::Button::Back) ||
      mappedInput.wasReleased(MappedInputManager::Button::Confirm) || mappedInput.wasScreenTapped(x, y)) {
    finish();
  }
}

void BackupReadingStatsActivity::render(RenderLock&&) {
  const auto& metrics = UITheme::getInstance().getMetrics();
  renderer.clearScreen();
  GUI.drawHeader(renderer, Rect{0, metrics.topPadding, renderer.getScreenWidth(), metrics.headerHeight},
                 tr(STR_BACKUP_STATS));

  if (state == State::CONFIRM) {
    const auto lines = renderer.wrappedText(UI_10_FONT_ID, tr(STR_BACKUP_STATS_CONFIRM),
                                            renderer.getScreenWidth() - metrics.contentSidePadding * 4, 3);
    int y = metrics.topPadding + metrics.headerHeight + 70;
    const int lineHeight = renderer.getLineHeight(UI_10_FONT_ID);
    for (const auto& line : lines) {
      renderer.drawCenteredText(UI_10_FONT_ID, y, line.c_str());
      y += lineHeight;
    }
    if (popup.processRender(renderer, mappedInput)) return;
  } else {
    const bool success = state == State::SUCCESS;
    renderer.drawCenteredText(UI_10_FONT_ID, renderer.getScreenHeight() / 2 - 30,
                              I18N.get(success ? StrId::STR_BACKUP_STATS_DONE : StrId::STR_BACKUP_STATS_FAILED), true,
                              EpdFontFamily::BOLD);
    if (success) renderer.drawCenteredText(SMALL_FONT_ID, renderer.getScreenHeight() / 2 + 10, fileName);
    const auto hints = mappedInput.mapLabels(tr(STR_BACK), tr(STR_BACK), "", "");
    GUI.drawButtonHints(renderer, hints.btn1, hints.btn2, hints.btn3, hints.btn4);
  }
  renderer.displayBuffer();
}
