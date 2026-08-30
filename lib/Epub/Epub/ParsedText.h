#pragma once

#include <EpdFontFamily.h>

#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "blocks/BlockStyle.h"
#include "blocks/TextBlock.h"

class GfxRenderer;
class Arena;
template <typename T>
class ArenaVector;

class ParsedText {
  // words/rubyTexts are std::deque, not std::vector: a paragraph can hold thousands
  // of tokens (CJK splits every character), and a vector grows by reallocating its
  // whole element array into one contiguous block (32 B/std::string -> 64-128 KB at
  // a few thousand tokens). On the ESP32-C3 that single large contiguous request
  // fails under a fragmented, BLE-resident heap and the throwing operator new
  // abort()s the firmware (fresh-open CJK crash). A deque grows in fixed ~512 B nodes
  // (largest contiguous alloc stays ~2 KB regardless of token count), so it never
  // triggers that. The per-token parallel arrays below stay vectors: 1 byte / 1 bit
  // each, they never approach the contiguous-block ceiling.
  std::deque<std::string> words;
  std::vector<EpdFontFamily::Style> wordStyles;
  // Boundary flags use all four combinations:
  //   continues=false, noSpace=false: ordinary breakable word gap
  //   continues=false, noSpace=true:  breakable zero-width, stretchable CJK/Korean gap
  //   continues=true,  noSpace=false: unbreakable attachment
  //   continues=true,  noSpace=true:  breakable zero-width, non-stretching attachment
  std::vector<bool> wordContinues;
  std::vector<bool> wordNoSpaceBefore;
  // Focus Reading emphasis: bytes [0, wordFocusBoundary) render bold, the rest at wordStyles.
  // 0 = none. An annotation rather than a token split, so the hyphenator and line breaker still
  // see whole words; TextBlock stores emphasis the same way, so extractLine passes it through.
  std::vector<uint8_t> wordFocusBoundary;
  // Temporary link identity that follows tokens through hyphenation and BiDi.
  // Zero is plain text; non-zero indexes linkTargets.
  std::vector<uint8_t> wordLinkIds;
  std::vector<std::string> linkTargets;
  // Zero-based visible Unicode-codepoint offsets in the spine body, stored as
  // uint16_t deltas from a shared base to keep this layout-only metadata small.
  // Pathological spans wider than uint16_t use sparse rebases; rendered
  // TextBlocks do not carry any of this metadata.
  struct VisibleOffsetRebase {
    size_t wordIndex;
    uint32_t base;
  };
  std::vector<uint16_t> wordVisibleOffsetDeltas;
  uint32_t visibleOffsetBase = 0;
  std::vector<VisibleOffsetRebase> visibleOffsetRebases;
  std::deque<std::string> rubyTexts;
  BlockStyle blockStyle;
  bool extraParagraphSpacing;
  bool hyphenationEnabled;
  bool focusReadingEnabled;
  uint8_t wordSpacing;
  bool isNaturalAlign;
  bool hasRtlWord;
  // A soft flush leaves the final line buffered in the same logical paragraph.
  // Subsequent passes must not reapply first-line indentation to that remainder.
  bool isContinuation_ = false;
  std::vector<std::string> reorderedWordsScratch;
  std::vector<EpdFontFamily::Style> reorderedStylesScratch;
  std::vector<uint16_t> reorderedWidthsScratch;
  std::vector<bool> reorderedContinuesScratch;
  std::vector<bool> reorderedNoSpaceBeforeScratch;
  std::vector<uint8_t> reorderedFocusBoundaryScratch;
  std::vector<std::string> lineWordsScratch;
  std::vector<EpdFontFamily::Style> lineStylesScratch;
  std::vector<int16_t> lineXPosScratch;
  std::vector<uint8_t> lineFocusBoundaryScratch;
  std::vector<uint16_t> lineFocusRunOffsetScratch;
  std::vector<uint16_t> visualOrderScratch;

  uint32_t visibleOffsetBaseAt(size_t wordIndex) const;
  uint32_t visibleOffsetAt(size_t wordIndex) const;
  void pushVisibleOffset(uint32_t offset);
  void insertVisibleOffset(size_t wordIndex, uint32_t offset);
  void eraseVisibleOffsetPrefix(size_t count);
  int calculateRubyExtraStartOffset(size_t wordIdx, size_t maxWordIdx, const GfxRenderer& renderer, int fontId) const;
  int calculateRubyExtraEndOffset(size_t lineStartIdx, size_t lineBreakIdx, const GfxRenderer& renderer,
                                  int fontId) const;
  int resolveFirstLineIndent(bool isFirstLine, const GfxRenderer& renderer, int fontId) const;
  bool computeLineBreaks(Arena& scratchArena, const GfxRenderer& renderer, int fontId, int pageWidth,
                         ArenaVector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                         std::vector<bool>& noSpaceBeforeVec, ArenaVector<size_t>& lineBreakIndices);
  bool computeHyphenatedLineBreaks(const GfxRenderer& renderer, int fontId, int pageWidth,
                                   ArenaVector<uint16_t>& wordWidths, std::vector<bool>& continuesVec,
                                   std::vector<bool>& noSpaceBeforeVec, ArenaVector<size_t>& lineBreakIndices);
  bool hyphenateWordAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                            ArenaVector<uint16_t>& wordWidths, bool allowFallbackBreaks);
  bool splitPathologicalTokenAtIndex(size_t wordIndex, int availableWidth, const GfxRenderer& renderer, int fontId,
                                     ArenaVector<uint16_t>& wordWidths);
  bool extractLine(size_t breakIndex, int pageWidth, const ArenaVector<uint16_t>& wordWidths,
                   const std::vector<bool>& continuesVec, const std::vector<bool>& noSpaceBeforeVec,
                   const ArenaVector<size_t>& lineBreakIndices,
                   const std::function<void(std::shared_ptr<TextBlock>, uint32_t)>& processLine,
                   const GfxRenderer& renderer, int fontId);
  bool calculateWordWidths(ArenaVector<uint16_t>& wordWidths, const GfxRenderer& renderer, int fontId);

 public:
  explicit ParsedText(const bool extraParagraphSpacing, const bool hyphenationEnabled = false,
                      const bool focusReadingEnabled = false, const BlockStyle& blockStyle = BlockStyle(),
                      const uint8_t wordSpacing = 0)
      : blockStyle(blockStyle),
        extraParagraphSpacing(extraParagraphSpacing),
        hyphenationEnabled(hyphenationEnabled),
        focusReadingEnabled(focusReadingEnabled),
        wordSpacing(wordSpacing > 2 ? 2 : wordSpacing),
        isNaturalAlign(false),
        hasRtlWord(false) {}
  ~ParsedText() = default;

  void addWord(std::string word, EpdFontFamily::Style fontStyle, bool underline = false, bool attachToPrevious = false,
               uint32_t visibleTextOffset = 0, bool breakWithoutSpaceBefore = false, uint8_t linkId = 0);
  uint8_t addLinkTarget(const char* href);
  bool linkTargetMatches(uint8_t linkId, const char* href) const;
  void setRubyForWordAt(size_t index, const std::string& ruby);
  void setRubyGroupAt(size_t startIndex, size_t count, const std::string& ruby);
  EpdFontFamily::Style getWordStyleAt(size_t index) const {
    return index < wordStyles.size() ? wordStyles[index] : EpdFontFamily::REGULAR;
  }
  std::string getRubyTextAt(size_t index) const { return index < rubyTexts.size() ? rubyTexts[index] : std::string(); }
  void ensureRubyCapacity();
  void setBlockStyle(const BlockStyle& blockStyle) { this->blockStyle = blockStyle; }
  BlockStyle& getBlockStyle() { return blockStyle; }
  size_t size() const { return words.size(); }
  bool isEmpty() const { return words.empty(); }
  bool layoutAndExtractLines(const GfxRenderer& renderer, int fontId, uint16_t viewportWidth,
                             const std::function<void(std::shared_ptr<TextBlock>, uint32_t)>& processLine,
                             bool includeLastLine = true);
};
