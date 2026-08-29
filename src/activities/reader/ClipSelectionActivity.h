#pragma once

#include <Epub/Page.h>

#include <memory>
#include <optional>
#include <vector>

#include "activities/Activity.h"
#include "clippings/ClippingPageTools.h"

class ClipSelectionActivity final : public Activity {
 public:
  ClipSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Page> page,
                        int marginLeft, int marginTop, const std::vector<ClippingCodec::Record>& records,
                        uint16_t spineIndex, uint16_t pageIndex, uint32_t pageVisibleOffset,
                        std::optional<uint32_t> nextPageVisibleOffset, int fontId, int lineHeight)
      : Activity("ClipSelection", renderer, mappedInput),
        page_(std::move(page)),
        marginLeft_(marginLeft),
        marginTop_(marginTop),
        fontId_(fontId),
        lineHeight_(lineHeight),
        records_(&records),
        spineIndex_(spineIndex),
        pageIndex_(pageIndex),
        pageVisibleOffset_(pageVisibleOffset),
        nextPageVisibleOffset_(nextPageVisibleOffset) {}

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
  const std::vector<ClippingCodec::Record>* records_ = nullptr;
  std::array<ClippingPageTools::ResolvedClipping, ClippingPageTools::MAX_RESOLVED_CLIPPINGS> resolved_{};
  size_t resolvedCount_ = 0;
  uint16_t spineIndex_ = 0;
  uint16_t pageIndex_ = 0;
  uint32_t pageVisibleOffset_ = 0;
  std::optional<uint32_t> nextPageVisibleOffset_;
  uint16_t rowCount_ = 0;
  int cursor_ = 0;
  int anchor_ = -1;
  bool selectionTooLong_ = false;
};
