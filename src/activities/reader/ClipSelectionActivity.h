#pragma once

#include <Epub/Page.h>

#include <memory>
#include <vector>

#include "activities/Activity.h"
#include "clippings/ClippingPageTools.h"

class ClipSelectionActivity final : public Activity {
 public:
  ClipSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Page> page,
                        int marginLeft, int marginTop)
      : Activity("ClipSelection", renderer, mappedInput),
        page_(std::move(page)),
        marginLeft_(marginLeft),
        marginTop_(marginTop) {}

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int closestInRow(uint16_t row, int centerX) const;
  int wordAt(int x, int y) const;
  void moveVertical(int direction);
  void confirmSelection();
  void drawSelection() const;

  std::unique_ptr<Page> page_;
  int marginLeft_ = 0;
  int marginTop_ = 0;
  int fontId_ = 0;
  int lineHeight_ = 0;
  std::vector<ClippingPageTools::WordRef> words_;
  uint16_t rowCount_ = 0;
  int cursor_ = 0;
  int anchor_ = -1;
  bool selectionTooLong_ = false;
};
