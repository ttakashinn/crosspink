#include "FullScreenMessageActivity.h"

#include <GfxRenderer.h>

#include "components/UIScale.h"
#include "components/UITheme.h"

void FullScreenMessageActivity::onEnter() {
  Activity::onEnter();

  const int fontId = uiScaleSpec().bodyFontId;
  renderer.clearScreen();
  UITheme::drawCenteredWrappedText(renderer, Rect{20, 0, renderer.getScreenWidth() - 40, renderer.getScreenHeight()},
                                   fontId, text.c_str(), 5, true, style);
  renderer.displayBuffer(refreshMode);
}
