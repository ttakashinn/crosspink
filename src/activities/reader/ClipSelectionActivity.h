#pragma once

#include <Epub/Page.h>

#include <memory>
#include <vector>

#include "activities/Activity.h"
#include "clippings/ClippingPageTools.h"

class ClipSelectionActivity final : public Activity {
 public:
  struct PageProvider {
    void* context = nullptr;
    std::unique_ptr<Page> (*loadPage)(void* context, uint16_t pageIndex) = nullptr;
    uint16_t pageCount = 0;
  };

  ClipSelectionActivity(GfxRenderer& renderer, MappedInputManager& mappedInput, std::unique_ptr<Page> page,
                        int marginLeft, int marginTop, const std::vector<ClippingCodec::Record>& records,
                        uint16_t spineIndex, uint16_t pageIndex, uint32_t pageVisibleOffset, PageProvider pageProvider,
                        int fontId, int lineHeight)
      : Activity("ClipSelection", renderer, mappedInput),
        page_(std::move(page)),
        marginLeft_(marginLeft),
        marginTop_(marginTop),
        fontId_(fontId),
        lineHeight_(lineHeight),
        records_(&records),
        pageIndex_(pageIndex),
        pageVisibleOffset_(pageVisibleOffset),
        pageProvider_(pageProvider) {
    partialSelection_.spineIndex = spineIndex;
  }

  void onEnter() override;
  void loop() override;
  void render(RenderLock&&) override;

 private:
  int closestInRow(uint16_t row, int centerX) const;
  int wordAt(int x, int y) const;
  void moveVertical(int direction);
  void confirmSelection();
  void drawSelection() const;
  bool appendCurrentSegment(ClippingSelectionResult& selection) const;
  bool moveToPage(uint16_t pageIndex, int restoredAnchor = -1, int restoredCursor = 0);
  void advancePage();
  void retreatPage();
  uint32_t matchingClippingId(const ClippingSelectionResult& selection) const;

  std::unique_ptr<Page> page_;
  int marginLeft_ = 0;
  int marginTop_ = 0;
  int fontId_ = 0;
  int lineHeight_ = 0;
  std::vector<ClippingPageTools::WordRef> words_;
  const std::vector<ClippingCodec::Record>* records_ = nullptr;
  uint16_t pageIndex_ = 0;
  uint32_t pageVisibleOffset_ = 0;
  PageProvider pageProvider_;
  ClippingSelectionResult partialSelection_;
  uint16_t rowCount_ = 0;
  int cursor_ = 0;
  int anchor_ = -1;
  bool selectionTooLong_ = false;
  bool selectionUnavailable_ = false;
  bool wordsTruncated_ = false;
};
