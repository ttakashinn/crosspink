#include "ChapterHtmlSlimParser.h"

#include <FontCacheManager.h>
#include <FsHelpers.h>
#include <GfxRenderer.h>
#include <HalStorage.h>
#include <Logging.h>
#include <Memory.h>
#include <Utf8.h>
#include <XmlParserUtils.h>
#include <expat.h>

#include <algorithm>
#include <array>
#include <iterator>
#include <limits>
#include <new>

#include "../../../../src/fontIds.h"
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
#include "render_lab/RenderLab.h"
#endif
#include "Epub.h"
#include "Epub/Page.h"
#include "Epub/VisibleTextUtils.h"
#include "Epub/converters/ImageDecoderFactory.h"
#include "Epub/converters/ImageDimsProbe.h"
#include "Epub/converters/ImageToFramebufferDecoder.h"
#include "Epub/htmlEntities.h"

// Minimum file size (in bytes) to show indexing popup - smaller chapters don't benefit from it
constexpr size_t MIN_SIZE_FOR_POPUP = 10 * 1024;  // 10KB
constexpr size_t PARSE_BUFFER_SIZE = 1024;
// Stop before entering throwing STL growth or shared_ptr control-block
// This number comes from PR #73
// If we have > 750 words buffered up, perform the layout and consume out all but the last line
// There should be enough here to build out 1-2 full pages and doing this will free up a lot of
// memory.
// Spotted when reading Intermezzo, there are some really long text blocks in there.
constexpr size_t TEXT_BLOCK_SOFT_FLUSH_WORDS = 750;

// When CSS is enabled, flush earlier to save RAM. 320 is still more than enough to build a CJK
// page at font size 14
constexpr size_t TEXT_BLOCK_SOFT_FLUSH_WORDS_WITH_CSS = 320;

// Hard cap on the number of anchor IDs recorded per chapter. Legitimate navigation
// anchors (TOC entries, footnotes, cross-references) rarely exceed a few hundred per
// chapter. A runaway count usually means a converter injected machine-generated IDs on
// every text fragment (e.g. Kobo KePub spans). The cap prevents unbounded heap growth
// on resource-constrained devices (~380KB heap). TOC anchors bypass this cap.
constexpr size_t MAX_ANCHORS_PER_CHAPTER = 1024;

// Reuse serializable PageLine/PageHorizontalRule elements for a small grid.
constexpr int16_t TABLE_CELL_HORIZONTAL_PADDING = 4;
constexpr int16_t TABLE_ROW_SEPARATOR_GAP = 4;
constexpr uint8_t TABLE_ROW_SEPARATOR_THICKNESS = 1;
constexpr int16_t TABLE_MIN_CELL_WIDTH_LINE_HEIGHTS = 3;
constexpr int16_t MIN_TEXT_WIDTH_LINE_HEIGHTS = 6;

constexpr const char* HEADER_TAGS[] = {"h1", "h2", "h3", "h4", "h5", "h6"};
// HTML5 sectioning/list elements are block containers in EPUB 3. Treating them
// as generic inline tags drops their margins, padding, alignment and page-break
// declarations, which is especially visible in modern Vietnamese EPUBs whose
// chapters are commonly wrapped in <section> or <article>.
constexpr const char* BLOCK_TAGS[] = {"p",    "li",    "div", "br",     "blockquote", "section", "article",
                                      "main", "aside", "nav", "figure", "figcaption", "ul",      "ol",
                                      "dl",   "dt",    "dd",  "pre",    "address"};
constexpr const char* BOLD_TAGS[] = {"b", "strong"};
constexpr const char* ITALIC_TAGS[] = {"i", "em"};
constexpr const char* UNDERLINE_TAGS[] = {"u", "ins"};
constexpr const char* LINETHROUGH_TAGS[] = {"del", "s", "strike"};
constexpr const char* IMAGE_TAGS[] = {"img", "image"};
bool isWhitespace(const char c) { return c == ' ' || c == '\r' || c == '\n' || c == '\t'; }

std::string trimAndNormalize(const std::string& str) {
  if (str.empty()) return "";
  size_t start = 0;
  while (start < str.size() && isWhitespace(str[start])) {
    start++;
  }
  if (start == str.size()) return "";
  size_t end = str.size() - 1;
  while (end > start && isWhitespace(str[end])) {
    end--;
  }
  std::string result;
  result.reserve(end - start + 1);
  bool inSpace = false;
  for (size_t i = start; i <= end; i++) {
    if (isWhitespace(str[i])) {
      if (!inSpace) {
        result.push_back(' ');
        inSpace = true;
      }
    } else {
      result.push_back(str[i]);
      inSpace = false;
    }
  }
  return result;
}

bool matches(const char* tag_name, const char* const* possible_tags, size_t count) {
  for (size_t i = 0; i < count; i++) {
    if (strcmp(tag_name, possible_tags[i]) == 0) {
      return true;
    }
  }
  return false;
}

BlockStyle::HorizontalLayout resolveTextHorizontalLayout(const BlockStyle& style, const uint16_t viewportWidth,
                                                         const int lineHeight) {
  // Keep an author-inset text column at least half a viewport wide and roughly
  // six em at larger font sizes. This still honors ordinary blockquotes/lists,
  // but deeply nested margins cannot degrade Vietnamese prose into one-word lines.
  const int minimumReadableWidth = std::max<int>({1, viewportWidth / 2, lineHeight * MIN_TEXT_WIDTH_LINE_HEIGHTS});
  return style.resolveHorizontalLayout(viewportWidth,
                                       static_cast<uint16_t>(std::min<int>(viewportWidth, minimumReadableWidth)));
}

bool isNonVisibleTextTag(const char* name) { return VisibleTextUtils::isNonVisibleElement(name); }

const char* getAttribute(const XML_Char** atts, const char* attrName) {
  if (!atts) return nullptr;
  for (int i = 0; atts[i]; i += 2) {
    if (strcmp(atts[i], attrName) == 0) return atts[i + 1];
  }
  return nullptr;
}

uint16_t parseTableSpan(const char* value) {
  if (!value || value[0] == '\0') return 1;

  uint32_t span = 0;
  for (const char* current = value; *current != '\0'; ++current) {
    if (*current < '0' || *current > '9') return 1;
    const uint32_t digit = static_cast<uint32_t>(*current - '0');
    if (span > (UINT16_MAX - digit) / 10) return UINT16_MAX;
    span = span * 10 + digit;
  }
  return span == 0 ? UINT16_MAX : static_cast<uint16_t>(span);
}

// Returns true if the HTML element is a purely inline, non-navigable wrapper.
// IDs on these elements are never meaningful navigation targets in epub content.
// Reading-system converters (Kobo KePub, Calibre, etc.) frequently inject thousands
// of such IDs for progress tracking or internal bookkeeping, and recording each one
// as a navigation anchor exhausts the heap on memory-constrained devices.
// Block-level, sectioning, and structural elements are always considered navigable.
bool isNonNavigableInlineElement(const char* name) { return strcmp(name, "span") == 0; }

bool isInternalEpubLink(const char* href) {
  if (!href || href[0] == '\0') return false;
  if (strncmp(href, "http://", 7) == 0 || strncmp(href, "https://", 8) == 0) return false;
  if (strncmp(href, "mailto:", 7) == 0) return false;
  if (strncmp(href, "ftp://", 6) == 0) return false;
  if (strncmp(href, "tel:", 4) == 0) return false;
  if (strncmp(href, "javascript:", 11) == 0) return false;
  return true;
}

bool isHeaderOrBlock(const char* name) {
  return matches(name, HEADER_TAGS, std::size(HEADER_TAGS)) || matches(name, BLOCK_TAGS, std::size(BLOCK_TAGS));
}

bool isTableStructuralTag(const char* name) {
  return strcmp(name, "table") == 0 || strcmp(name, "tr") == 0 || strcmp(name, "td") == 0 || strcmp(name, "th") == 0;
}

void ChapterHtmlSlimParser::applyDirectionToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (css.hasDirection()) {
    entry.hasDirection = true;
    entry.direction = css.direction;
  }
}

EpdFontFamily::Style ChapterHtmlSlimParser::fontStyleForTextDecoration(const CssTextDecoration decoration) {
  EpdFontFamily::Style style = EpdFontFamily::REGULAR;
  if ((decoration & CssTextDecoration::Underline) != CssTextDecoration::None) {
    style = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::UNDERLINE);
  }
  if ((decoration & CssTextDecoration::LineThrough) != CssTextDecoration::None) {
    style = static_cast<EpdFontFamily::Style>(style | EpdFontFamily::STRIKETHROUGH);
  }
  return style;
}

void ChapterHtmlSlimParser::applyTextDecorationToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (css.hasTextDecoration()) {
    entry.hasTextDecoration = true;
    entry.textDecoration = css.textDecoration;
  }
}

void ChapterHtmlSlimParser::applySmallCapsToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (css.hasFontVariantCaps()) {
    entry.hasSmallCaps = true;
    entry.smallCaps = css.fontVariantCaps == CssFontVariantCaps::SmallCaps;
  }
}

void ChapterHtmlSlimParser::applyVerticalAlignToEntry(StyleStackEntry& entry, const CssStyle& css) {
  if (!css.hasVerticalAlign()) return;
  if (css.verticalAlign == CssVerticalAlign::Super) {
    entry.hasSup = true;
    entry.sup = true;
  } else if (css.verticalAlign == CssVerticalAlign::Sub) {
    entry.hasSub = true;
    entry.sub = true;
  }
}

void ChapterHtmlSlimParser::pushTableTextStyleEntry(const CssStyle& cssStyle) {
  if (!cssStyle.hasFontWeight() && !cssStyle.hasFontStyle() && !cssStyle.hasFontVariantCaps() &&
      !cssStyle.hasTextDecoration() && !cssStyle.hasDirection() && !cssStyle.hasTextAlign()) {
    return;
  }

  StyleStackEntry entry;
  entry.depth = depth;
  if (cssStyle.hasFontWeight()) {
    entry.hasBold = true;
    entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
  }
  if (cssStyle.hasFontStyle()) {
    entry.hasItalic = true;
    entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
  }
  applySmallCapsToEntry(entry, cssStyle);
  applyTextDecorationToEntry(entry, cssStyle);
  applyDirectionToEntry(entry, cssStyle);
  // A table/cell/row direction can be inherited by the cell CSS, but only the
  // cell entry owns a paragraph. Keep table wrappers from rewriting unrelated
  // flow paragraphs while preserving the cell's base direction.
  entry.setsParagraphDirection = insideTableCell && cssStyle.hasDirection();
  if (cssStyle.hasTextAlign()) {
    entry.hasTextAlign = true;
    entry.textAlign = cssStyle.textAlign;
  }
  if (!pushInlineStyle(entry)) return;
  updateEffectiveInlineStyle();
}

bool ChapterHtmlSlimParser::pushInlineStyle(const StyleStackEntry& entry) {
  if (inlineStyleStack.push_back(entry)) return true;
  markLowMemoryFailure("inline style arena");
  return false;
}

void ChapterHtmlSlimParser::pushDecorationStyleEntry(const CssTextDecoration defaultDecoration,
                                                     const CssStyle& cssStyle) {
  StyleStackEntry entry;
  entry.depth = depth;
  entry.hasTextDecoration = true;
  entry.textDecoration = cssStyle.hasTextDecoration() ? cssStyle.textDecoration : defaultDecoration;
  if (cssStyle.hasFontWeight()) {
    entry.hasBold = true;
    entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
  }
  if (cssStyle.hasFontStyle()) {
    entry.hasItalic = true;
    entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
  }
  applySmallCapsToEntry(entry, cssStyle);
  applyDirectionToEntry(entry, cssStyle);
  applyVerticalAlignToEntry(entry, cssStyle);
  if (!pushInlineStyle(entry)) return;
  updateEffectiveInlineStyle();
}

// Update effective bold/italic/decorations based on block style and inline style stack
void ChapterHtmlSlimParser::updateEffectiveInlineStyle() {
  // Start with block-level styles
  effectiveFontWeightDefined = currentCssStyle.hasFontWeight();
  effectiveBold = effectiveFontWeightDefined && currentCssStyle.fontWeight == CssFontWeight::Bold;
  effectiveFontStyleDefined = currentCssStyle.hasFontStyle();
  effectiveItalic = effectiveFontStyleDefined && currentCssStyle.fontStyle == CssFontStyle::Italic;
  effectiveSmallCaps =
      currentCssStyle.hasFontVariantCaps() && currentCssStyle.fontVariantCaps == CssFontVariantCaps::SmallCaps;
  effectiveTextDecoration =
      currentCssStyle.hasTextDecoration() ? currentCssStyle.textDecoration : CssTextDecoration::None;
  bool paragraphDirectionDefined = false;
  bool paragraphIsRtl = false;
  if (!blockStyleStack.empty()) {
    const auto& blockStyle = blockStyleStack.back();
    paragraphDirectionDefined = blockStyle.directionDefined;
    paragraphIsRtl = blockStyle.isRtl;
  }
  effectiveDirectionDefined = paragraphDirectionDefined;
  effectiveDirection = paragraphIsRtl ? CssTextDirection::Rtl : CssTextDirection::Ltr;
  effectiveTextAlignDefined = currentCssStyle.hasTextAlign();
  effectiveTextAlign = currentCssStyle.textAlign;
  effectiveSup = false;
  effectiveSub = false;

  // Apply inline style stack in order
  for (const auto& entry : inlineStyleStack) {
    if (entry.hasBold) {
      effectiveFontWeightDefined = true;
      effectiveBold = entry.bold;
    }
    if (entry.hasItalic) {
      effectiveFontStyleDefined = true;
      effectiveItalic = entry.italic;
    }
    if (entry.hasSmallCaps) {
      effectiveSmallCaps = entry.smallCaps;
    }
    // CSS line decorations propagate through descendants; child entries add
    // their own lines but cannot cancel an ancestor's already active line.
    if (entry.hasTextDecoration) {
      effectiveTextDecoration = effectiveTextDecoration | entry.textDecoration;
    }
    if (entry.hasDirection) {
      effectiveDirectionDefined = true;
      effectiveDirection = entry.direction;
      if (entry.setsParagraphDirection) {
        paragraphDirectionDefined = true;
        paragraphIsRtl = entry.direction == CssTextDirection::Rtl;
      }
    }
    if (entry.hasTextAlign) {
      effectiveTextAlignDefined = true;
      effectiveTextAlign = entry.textAlign;
    }
    if (entry.hasSup) {
      effectiveSup = entry.sup;
      if (entry.sup) effectiveSub = false;
    }
    if (entry.hasSub) {
      effectiveSub = entry.sub;
      if (entry.sub) effectiveSup = false;
    }
  }

  // Keep only the paragraph base direction in the empty block. Inline direction
  // still styles its own text, but must not realign/reorder the whole paragraph.
  if (currentTextBlock && currentTextBlock->isEmpty()) {
    auto& style = currentTextBlock->getBlockStyle();
    style.directionDefined = paragraphDirectionDefined;
    style.isRtl = paragraphIsRtl;
  }
}

CssParser::DescendantMask ChapterHtmlSlimParser::activeCssAncestorMask() const {
  if (!cssParser || depth <= 0) return 0;
  const size_t index = std::min(static_cast<size_t>(depth - 1), MAX_CSS_ANCESTOR_DEPTH - 1);
  return cssAncestorMasks[index];
}

void ChapterHtmlSlimParser::markLowMemoryFailure(const char* stage) {
  if (failure_ != Failure::LowMemory) {
    LOG_ERR("EHP", "Low memory during %s (%u free, %u max alloc); aborting section build", stage,
            static_cast<unsigned>(ESP.getFreeHeap()), static_cast<unsigned>(ESP.getMaxAllocHeap()));
  }
  failure_ = Failure::LowMemory;
  if (xmlParser_) XML_StopParser(xmlParser_, XML_FALSE);
}

bool ChapterHtmlSlimParser::shouldAbortForLowMemory(const char* stage) {
  if (failure_ == Failure::LowMemory) return true;
  if (epubLayoutHeapSufficient(renderMode, ESP.getFreeHeap(), ESP.getMaxAllocHeap())) {
    return false;
  }
  if (!attemptedLowMemoryFontCacheRelease_) {
    attemptedLowMemoryFontCacheRelease_ = true;
    if (auto* fontCache = renderer.getFontCacheManager()) {
      fontCache->releaseSdFontCaches();
      if (epubLayoutHeapSufficient(renderMode, ESP.getFreeHeap(), ESP.getMaxAllocHeap())) {
        LOG_DBG("EHP", "Released SD font caches before %s", stage);
        return false;
      }
    }
  }
  markLowMemoryFailure(stage);
  return true;
}

void ChapterHtmlSlimParser::trackCssAncestor(const std::string_view tagName, const std::string_view classAttr) {
  if (!cssParser || depth < 0 || static_cast<size_t>(depth) >= MAX_CSS_ANCESTOR_DEPTH) return;
  cssAncestorMasks[static_cast<size_t>(depth)] =
      activeCssAncestorMask() | cssParser->matchingAncestorMask(tagName, classAttr);
}

void ChapterHtmlSlimParser::forcePageBreak() {
  if (partWordBufferIndex > 0) flushPartWordBuffer();
  if (currentTextBlock && !currentTextBlock->isEmpty()) makePages();
  if (!currentPage || currentPage->elements.empty()) return;

  setCurrentPageVisibleOffset(visibleTextOffset);
  completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, currentPageVisibleOffset);
  completedPageCount++;
  currentPage.reset();
  currentPageNextY = 0;
  currentPageVisibleOffsetSet = false;
}

void ChapterHtmlSlimParser::registerPageBreakAfter(const int elementDepth) {
  if (pageBreakAfterCount >= pageBreakAfterDepths.size()) {
    LOG_DBG("EHP", "Page-break-after stack full at depth %d", elementDepth);
    return;
  }
  pageBreakAfterDepths[pageBreakAfterCount++] = elementDepth;
}

bool ChapterHtmlSlimParser::consumePageBreakAfter(const int elementDepth) {
  for (size_t i = pageBreakAfterCount; i > 0; --i) {
    if (pageBreakAfterDepths[i - 1] != elementDepth) continue;
    for (size_t tail = i; tail < pageBreakAfterCount; ++tail) {
      pageBreakAfterDepths[tail - 1] = pageBreakAfterDepths[tail];
    }
    pageBreakAfterCount--;
    return true;
  }
  return false;
}

void ChapterHtmlSlimParser::flushPendingAnchor() {
  if (pendingAnchorId.empty()) return;
  if (shouldAbortForLowMemory("anchor flush")) return;

  // If the pending anchor is a TOC chapter boundary, force a page break after the previous
  // block is flushed so the chapter starts on a fresh page.
  if (std::find(tocAnchors.begin(), tocAnchors.end(), pendingAnchorId) != tocAnchors.end()) {
    if (currentPage && !currentPage->elements.empty()) {
      completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, currentPageVisibleOffset);
      completedPageCount++;
      currentPage = makeUniqueNoThrow<Page>();
      if (!currentPage) {
        markLowMemoryFailure("TOC page break");
        return;
      }
      currentPageNextY = 0;
      currentPageVisibleOffsetSet = false;
      currentPage->setPublisherPageLabel(activePublisherPageLabel);
    }
  }

  // Record deferred anchor after previous block is flushed (and any TOC page break)
  anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
  pendingAnchorId.clear();
}

void ChapterHtmlSlimParser::setCurrentPageVisibleOffset(const uint32_t offset) {
  if (currentPageVisibleOffsetSet) return;
  // The first page always begins at the start of the body, even when the XHTML
  // contains leading formatting whitespace before its first rendered word.
  currentPageVisibleOffset = completedPageCount == 0 ? 0 : offset;
  currentPageVisibleOffsetSet = true;
}

void ChapterHtmlSlimParser::addPublisherPageMarker(const uint32_t offset, const char* label) {
  if (!label || label[0] == '\0' || publisherPageMarkerCount >= MAX_PUBLISHER_PAGE_MARKERS) return;
  for (size_t i = 0; i < publisherPageMarkerCount; ++i) {
    if (publisherPageMarkers[i].visibleOffset == offset) return;
  }
  auto& marker = publisherPageMarkers[publisherPageMarkerCount++];
  marker.visibleOffset = offset;
  const int bounded = std::min<int>(strlen(label), sizeof(marker.label) - 1);
  const int safeLength = utf8SafeTruncateBuffer(label, bounded);
  memcpy(marker.label, label, static_cast<size_t>(safeLength));
  marker.label[safeLength] = '\0';
}

void ChapterHtmlSlimParser::applyPublisherPageLabel(Page& page, const uint32_t offset) {
  while (nextPublisherPageMarker < publisherPageMarkerCount &&
         publisherPageMarkers[nextPublisherPageMarker].visibleOffset <= offset) {
    strncpy(activePublisherPageLabel, publisherPageMarkers[nextPublisherPageMarker].label,
            sizeof(activePublisherPageLabel) - 1);
    activePublisherPageLabel[sizeof(activePublisherPageLabel) - 1] = '\0';
    ++nextPublisherPageMarker;
  }
  page.setPublisherPageLabel(activePublisherPageLabel);
}

// flush the contents of partWordBuffer to currentTextBlock
void ChapterHtmlSlimParser::flushPartWordBuffer() {
  if (!currentTextBlock) {
    partWordBufferIndex = 0;
    nextWordContinues = false;
    nextWordBreakWithoutSpace = false;
    return;
  }
  const bool isBold = effectiveBold;
  const bool isItalic = effectiveItalic;

  // Combine style flags using bitwise OR
  EpdFontFamily::Style fontStyle = EpdFontFamily::REGULAR;
  if (isBold) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::BOLD);
  }
  if (isItalic) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::ITALIC);
  }
  fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | fontStyleForTextDecoration(effectiveTextDecoration));
  if (effectiveSup) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SUP);
  } else if (effectiveSub) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SUB);
  }
  if (effectiveSmallCaps) {
    fontStyle = static_cast<EpdFontFamily::Style>(fontStyle | EpdFontFamily::SMALL_CAPS);
  }

  // flush the buffer
  partWordBuffer[partWordBufferIndex] = '\0';
  const size_t wordBytes = static_cast<size_t>(partWordBufferIndex);
  if (insideTableCell && !tableRowStacked && tableCellTextBytes + wordBytes > MAX_GRID_TABLE_CELL_BYTES) {
    fallbackTableRowToStacked();
  }

  uint8_t linkId = 0;
  if (insideFootnoteLink && currentFootnote.href[0] != '\0') {
    if (!currentTextBlock->linkTargetMatches(currentFootnoteLinkId, currentFootnote.href)) {
      currentFootnoteLinkId = currentTextBlock->addLinkTarget(currentFootnote.href);
    }
    linkId = currentFootnoteLinkId;
  }
  currentTextBlock->addWord(partWordBuffer, fontStyle, false, nextWordContinues, partWordVisibleOffset,
                            nextWordBreakWithoutSpace, linkId);
  if (insideTableCell && !tableRowStacked) {
    tableCellTextBytes += wordBytes;
    if (currentTextBlock->size() > MAX_GRID_TABLE_CELL_WORDS) {
      fallbackTableRowToStacked();
    }
  }
  partWordBufferIndex = 0;
  nextWordContinues = false;
  nextWordBreakWithoutSpace = false;
  listItemBulletOnly = false;
}

// start a new text block if needed
void ChapterHtmlSlimParser::startNewTextBlock(const BlockStyle& blockStyle) {
  if (shouldAbortForLowMemory("text block creation")) return;
  nextWordContinues = false;  // New block = new paragraph, no continuation
  nextWordBreakWithoutSpace = false;
  if (currentTextBlock) {
    // already have a text block running and it is empty - just reuse it
    if (currentTextBlock->isEmpty()) {
      // The stack accumulates horizontal margins and text properties from ancestors.
      // Vertical margins are per-element and not inherited through the stack, but
      // container elements deposit their vertical margins on the empty block when they
      // open. Merge those into the new style so the first child in a container inherits
      // the container's vertical spacing.
      const auto style = currentTextBlock->getBlockStyle();
      BlockStyle incoming = blockStyle;
      if (style.fromBrElement) {
        // The empty block was created by a <br> section separator. Inject a full line of
        // blank space before the following paragraph so the scene/section break is visible.
        // This only fires when the <br> block stayed empty (i.e. no inline text was added).
        const int16_t lineHeight = static_cast<int16_t>(renderer.getLineHeight(fontId, lineCompression));
        incoming.marginTop = static_cast<int16_t>(incoming.marginTop + lineHeight);
      }

      currentTextBlock->setBlockStyle(style.getCombinedBlockStyle(incoming, BlockStyle::CombineAxis::Vertical));

      flushPendingAnchor();
      return;
    }

    // <li> added a bullet as the first word, making the block non-empty. When a nested
    // block-level child (<p>, <div>, etc.) opens, reuse the block instead of flushing
    // the bullet to its own line. The bullet stays inline with the child's text.
    if (listItemBulletOnly) {
      const auto style = currentTextBlock->getBlockStyle();
      currentTextBlock->setBlockStyle(style.getCombinedBlockStyle(blockStyle, BlockStyle::CombineAxis::Vertical));
      listItemBulletOnly = false;
      flushPendingAnchor();
      return;
    }

    makePages();
    if (failure_ == Failure::LowMemory) return;
  }
  // If the pending anchor is a TOC chapter boundary, force a page break after the previous
  // block is flushed so the chapter starts on a fresh page.
  flushPendingAnchor();
  if (failure_ == Failure::LowMemory) return;
  currentTextBlock = makeUniqueNoThrow<ParsedText>(extraParagraphSpacing, hyphenationEnabled, focusReadingEnabled,
                                                   blockStyle, wordSpacing);
  if (!currentTextBlock) {
    markLowMemoryFailure("text block allocation");
    return;
  }
  wordsExtractedInBlock = 0;
  listItemBulletOnly = false;
}

void ChapterHtmlSlimParser::emitHorizontalRule(const BlockStyle& blockStyle) {
  if (shouldAbortForLowMemory("horizontal rule")) return;
  if (partWordBufferIndex > 0) {
    flushPartWordBuffer();
  }

  if (currentTextBlock) {
    const BlockStyle parentBlockStyle = currentTextBlock->getBlockStyle();
    startNewTextBlock(parentBlockStyle);
    if (failure_ == Failure::LowMemory) return;
  }

  if (!currentPage) {
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      markLowMemoryFailure("horizontal-rule page");
      return;
    }
    currentPageNextY = 0;
    currentPage->setPublisherPageLabel(activePublisherPageLabel);
  }

  const int16_t lineHeight = static_cast<int16_t>(renderer.getLineHeight(fontId, lineCompression));
  const int16_t defaultVerticalSpacing = static_cast<int16_t>(lineHeight / 2);
  const int16_t topSpacing =
      static_cast<int16_t>((blockStyle.marginTop > 0 ? blockStyle.marginTop : defaultVerticalSpacing) +
                           (blockStyle.paddingTop > 0 ? blockStyle.paddingTop : 0));
  const int16_t bottomSpacing =
      static_cast<int16_t>((blockStyle.marginBottom > 0 ? blockStyle.marginBottom : defaultVerticalSpacing) +
                           (blockStyle.paddingBottom > 0 ? blockStyle.paddingBottom : 0));
  constexpr uint8_t ruleThickness = 2;
  const auto horizontalLayout = blockStyle.resolveHorizontalLayout(viewportWidth);
  const int16_t availableWidth = static_cast<int16_t>(horizontalLayout.contentWidth);
  const int16_t width = std::max<int16_t>(1, static_cast<int16_t>(availableWidth / 4));
  const int16_t xPos = static_cast<int16_t>(horizontalLayout.xOffset + ((availableWidth - width) / 2));
  const int16_t totalHeight = static_cast<int16_t>(topSpacing + ruleThickness + bottomSpacing);

  if (!currentPage->elements.empty() && currentPageNextY + totalHeight > viewportHeight) {
    setCurrentPageVisibleOffset(visibleTextOffset);
    completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, currentPageVisibleOffset);
    completedPageCount++;
    currentPage.reset(new (std::nothrow) Page());
    if (!currentPage) {
      markLowMemoryFailure("horizontal-rule page break");
      return;
    }
    currentPageNextY = 0;
    currentPageVisibleOffsetSet = false;
    currentPage->setPublisherPageLabel(activePublisherPageLabel);
  }

  currentPageNextY += topSpacing;

  auto pageRule = std::shared_ptr<PageHorizontalRule>(
      new (std::nothrow) PageHorizontalRule(width, ruleThickness, xPos, currentPageNextY));
  if (!pageRule) {
    markLowMemoryFailure("horizontal-rule element");
    return;
  }
  applyPublisherPageLabel(*currentPage, visibleTextOffset);
  currentPage->elements.push_back(pageRule);
  setCurrentPageVisibleOffset(visibleTextOffset);
  currentPageNextY = static_cast<int16_t>(currentPageNextY + ruleThickness + bottomSpacing);

  if (!pendingAnchorId.empty()) {
    anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
    pendingAnchorId.clear();
  }
}

void ChapterHtmlSlimParser::fallbackTableRowToStacked() {
  if (tableRowStacked) {
    return;
  }

  auto activeCell = std::move(currentTextBlock);
  tableRowStacked = true;

  for (auto& cell : tableRowCells) {
    currentTextBlock = std::move(cell);
    wordsExtractedInBlock = 0;
    if (currentTextBlock && !currentTextBlock->isEmpty()) {
      makePages();
    }
  }
  tableRowCells.clear();
  currentTextBlock = std::move(activeCell);
  wordsExtractedInBlock = 0;
}

void ChapterHtmlSlimParser::closeTableCell() {
  if (!insideTableCell) {
    return;
  }
  insideTableCell = false;

  if (!currentTextBlock) {
    return;
  }

  if (!tableRowStacked &&
      (tableRowCells.size() >= MAX_GRID_TABLE_COLUMNS || currentTextBlock->size() > MAX_GRID_TABLE_CELL_WORDS)) {
    fallbackTableRowToStacked();
  }

  if (tableRowStacked) {
    wordsExtractedInBlock = 0;
    if (!currentTextBlock->isEmpty()) {
      makePages();
    }
    currentTextBlock.reset();
    return;
  }

  tableRowCells.push_back(std::move(currentTextBlock));
}

void ChapterHtmlSlimParser::addTableRowSeparator() {
  if (shouldAbortForLowMemory("table row separator")) return;
  if (!currentPage || currentPage->elements.empty() || viewportWidth == 0 ||
      currentPageNextY + TABLE_ROW_SEPARATOR_GAP > viewportHeight) {
    return;
  }

  auto separator = std::shared_ptr<PageHorizontalRule>(
      new (std::nothrow) PageHorizontalRule(viewportWidth, TABLE_ROW_SEPARATOR_THICKNESS, 0, currentPageNextY + 1));
  if (!separator) {
    markLowMemoryFailure("table row separator allocation");
    return;
  }
  if (currentPage->elements.capacity() == currentPage->elements.size()) {
    currentPage->elements.reserve(currentPage->elements.size() + 1);
  }
  currentPage->elements.push_back(std::move(separator));
  currentPageNextY += TABLE_ROW_SEPARATOR_GAP;
}

void ChapterHtmlSlimParser::finishTableRow() {
  if (shouldAbortForLowMemory("table row layout")) return;
  closeTableCell();

  if (tableRowCells.empty()) {
    if (tableRowStacked) {
      addTableRowSeparator();
    }
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
    render_lab::recordTableRowFinished(false, 0, 0, static_cast<uint16_t>(completedPageCount));
#endif
    tableRowStacked = false;
    return;
  }

  const int16_t lineHeight =
      std::max<int16_t>(1, static_cast<int16_t>(renderer.getLineHeight(fontId) * lineCompression));
  const size_t columnCount = tableRowCells.size();
  const uint16_t cellWidth = static_cast<uint16_t>(viewportWidth / columnCount);

  // Keep enough width for a few glyphs while allowing ordinary three-column
  // tables to remain tabular at the default font size in portrait.
  if (columnCount < 2 || cellWidth <= TABLE_CELL_HORIZONTAL_PADDING * 2 ||
      cellWidth < lineHeight * TABLE_MIN_CELL_WIDTH_LINE_HEIGHTS) {
    fallbackTableRowToStacked();
    addTableRowSeparator();
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
    render_lab::recordTableRowFinished(false, 0, 0, static_cast<uint16_t>(completedPageCount));
#endif
    tableRowStacked = false;
    return;
  }

  const uint16_t textWidth = static_cast<uint16_t>(cellWidth - TABLE_CELL_HORIZONTAL_PADDING * 2);
  for (auto& lines : tableCellLines) {
    lines.clear();
  }
  tableLineVisibleOffsets.clear();
  if (tableLineVisibleOffsets.capacity() < MAX_GRID_TABLE_CELL_WORDS * 2) {
    tableLineVisibleOffsets.reserve(MAX_GRID_TABLE_CELL_WORDS * 2);
  }
  size_t maxLineCount = 0;
  size_t wrappedCellCount = 0;
  const bool rowRtl = tableRowRtl;

  for (size_t column = 0; column < columnCount; ++column) {
    auto& lines = tableCellLines[column];
    // Two wrapped lines per buffered word avoids normal vector growth (max 64).
    if (lines.capacity() < MAX_GRID_TABLE_CELL_WORDS * 2) {
      lines.reserve(MAX_GRID_TABLE_CELL_WORDS * 2);
    }
    if (!tableRowCells[column]->layoutAndExtractLines(
            renderer, fontId, textWidth, [this, &lines](const std::shared_ptr<TextBlock>& line, const uint32_t offset) {
              const size_t lineIndex = lines.size();
              lines.push_back(line);
              if (tableLineVisibleOffsets.size() <= lineIndex) {
                tableLineVisibleOffsets.resize(lineIndex + 1, UINT32_MAX);
              }
              tableLineVisibleOffsets[lineIndex] = std::min(tableLineVisibleOffsets[lineIndex], offset);
            })) {
      markLowMemoryFailure("table cell text layout");
      return;
    }
    if (lines.size() > 1) wrappedCellCount++;
    maxLineCount = std::max(maxLineCount, lines.size());
  }
  tableRowCells.clear();
  const auto clearLayoutLines = [this]() {
    for (auto& lines : tableCellLines) {
      lines.clear();
    }
    tableLineVisibleOffsets.clear();
  };

  for (size_t lineIndex = 0; lineIndex < maxLineCount; ++lineIndex) {
    const uint32_t lineVisibleOffset =
        lineIndex < tableLineVisibleOffsets.size() ? tableLineVisibleOffsets[lineIndex] : visibleTextOffset;
    int16_t rowLineHeight = lineHeight;
    for (size_t column = 0; column < columnCount; ++column) {
      if (lineIndex < tableCellLines[column].size()) {
        rowLineHeight = std::max<int16_t>(
            rowLineHeight, static_cast<int16_t>(lineHeight + tableCellLines[column][lineIndex]->getRubyShift(
                                                                 renderer.getFontAscenderSize(fontId))));
      }
    }

    const bool pageFull =
        currentPage && !currentPage->elements.empty() && currentPageNextY + rowLineHeight > viewportHeight;
    if (!currentPage || pageFull) {
      if (pageFull) {
        setCurrentPageVisibleOffset(lineVisibleOffset);
        completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, currentPageVisibleOffset);
        completedPageCount++;
      }
      currentPage = makeUniqueNoThrow<Page>();
      if (!currentPage) {
        markLowMemoryFailure("table row page");
        clearLayoutLines();
        return;
      }
      currentPageNextY = 0;
      currentPageVisibleOffsetSet = false;
    }

    applyPublisherPageLabel(*currentPage, lineVisibleOffset);

    const int16_t rowY = currentPageNextY;
    const size_t requiredCapacity = currentPage->elements.size() + columnCount;
    if (currentPage->elements.capacity() < requiredCapacity) {
      const size_t linesThatFit =
          std::max<size_t>(1, static_cast<size_t>((viewportHeight - currentPageNextY) / rowLineHeight));
      const size_t linesToReserve = std::min(maxLineCount - lineIndex, linesThatFit);
      currentPage->elements.reserve(currentPage->elements.size() + linesToReserve * columnCount + 1);
    }
    for (size_t column = 0; column < columnCount; ++column) {
      if (lineIndex >= tableCellLines[column].size()) {
        continue;
      }

      auto& line = tableCellLines[column][lineIndex];
      auto style = line->getBlockStyle();
      const size_t physicalColumn = rowRtl ? columnCount - column - 1 : column;
      style.marginLeft = static_cast<int16_t>(physicalColumn * cellWidth + TABLE_CELL_HORIZONTAL_PADDING);
      style.paddingLeft = 0;
      line->setBlockStyle(style);

      // Reset Y so every cell in this slice shares one baseline.
      currentPageNextY = rowY;
      // Grid cells are laid out against their own fixed cell width. Their left
      // position is an absolute column coordinate, not a nested paragraph inset,
      // so do not apply the minimum-readable-width clamp a second time.
      addLineToPage(line, lineVisibleOffset, style.marginLeft);
    }
    currentPageNextY = static_cast<int16_t>(rowY + rowLineHeight);
  }

  addTableRowSeparator();
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
  render_lab::recordTableRowFinished(true, static_cast<uint16_t>(wrappedCellCount), static_cast<uint16_t>(maxLineCount),
                                     static_cast<uint16_t>(completedPageCount));
#endif
  tableRowStacked = false;
  clearLayoutLines();
}

void XMLCALL ChapterHtmlSlimParser::startElement(void* userData, const XML_Char* name, const XML_Char** atts) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  if (self->shouldAbortForLowMemory("element start")) return;
  if (strcasecmp(name, "body") == 0) {
    // Case-insensitive to match ParagraphStreamer's tag matching (ProgressMapper). A case
    // mismatch here would leave visibleTextOffset at 0 for the whole section, so every page
    // would record offset 0 while the sync resolver still counts a non-zero offset.
    self->insideBody = true;
  }
  if (self->insideBody && (self->nonVisibleTextDepth > 0 || isNonVisibleTextTag(name))) {
    self->nonVisibleTextDepth++;
  }

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    self->depth += 1;
    return;
  }

  if (strcmp(name, "p") == 0) {
    self->xpathParagraphIndex++;
  }
  if (strcmp(name, "li") == 0) {
    self->xpathListItemIndex++;
  }

  // Extract class, style, id, and dir attributes for CSS/RTL processing
  std::string classAttr;
  std::string styleAttr;
  std::string dirAttr;
  if (atts != nullptr) {
    for (int i = 0; atts[i]; i += 2) {
      if (strcmp(atts[i], "class") == 0) {
        classAttr = atts[i + 1];
      } else if (strcmp(atts[i], "style") == 0) {
        styleAttr = atts[i + 1];
      } else if (strcmp(atts[i], "id") == 0) {
        // Defer both anchor recording and TOC page breaks until startNewTextBlock,
        // after the previous block is flushed to pages via makePages().
        //
        // Skip IDs on non-navigable inline elements (e.g. <span>): these are never
        // link targets in epub content, but reading-system converters can inject tens
        // of thousands of them per chapter, exhausting the heap. TOC anchors are
        // always recorded regardless of element type, since they drive page breaks.
        const char* idValue = atts[i + 1];
        const auto pageAnchor = std::find_if(self->publisherPageAnchors.begin(), self->publisherPageAnchors.end(),
                                             [idValue](const auto& entry) { return entry.first == idValue; });
        const bool isPublisherPageAnchor = pageAnchor != self->publisherPageAnchors.end();
        if (isPublisherPageAnchor) {
          self->addPublisherPageMarker(self->visibleTextOffset, pageAnchor->second.c_str());
        }
        const bool isTocAnchor =
            std::find(self->tocAnchors.begin(), self->tocAnchors.end(), idValue) != self->tocAnchors.end();
        // page-list targets are commonly empty inline spans. Preserve those
        // known navigation anchors without reopening the heap risk from
        // recording every converter-generated span ID.
        if (isTocAnchor || isPublisherPageAnchor ||
            (!isNonNavigableInlineElement(name) && self->anchorData.size() < MAX_ANCHORS_PER_CHAPTER)) {
          // Flush a displaced anchor before overwriting. Consecutive non-block elements
          // (e.g. <aside id="fn1">text</aside><aside id="fn2">) with no intervening block
          // never trigger startNewTextBlock, so fn1 gets silently overwritten. That leaves
          // fn1 missing from the anchor map -> getPageForAnchor returns nullopt -> reader
          // lands at page 0 (section start) instead of the footnote.
          if (!self->pendingAnchorId.empty()) {
            self->flushPendingAnchor();
          }
          self->pendingAnchorId = idValue;
        }
      } else if (strcmp(atts[i], "dir") == 0) {
        dirAttr = atts[i + 1];
      }
    }
  }

  auto centeredBlockStyle = BlockStyle();
  centeredBlockStyle.textAlignDefined = true;
  centeredBlockStyle.alignment = CssTextAlign::Center;

  // Compute CSS style for this element early so display:none can short-circuit
  // before tag-specific branches emit any content or metadata.
  CssStyle cssStyle;
  if (self->cssParser) {
    const CssParser::DescendantMask ancestorMask =
        self->renderMode == EpubRenderMode::Simplified ? 0 : self->activeCssAncestorMask();
    cssStyle = self->cssParser->resolveStyle(name, classAttr, ancestorMask);
    if (!styleAttr.empty()) {
      CssStyle inlineStyle = CssParser::parseInlineStyle(styleAttr);
      cssStyle.applyOver(inlineStyle);
    }
  }

  // Keep the distinction between a declaration on this element and a value
  // inherited from its parent. Semantic defaults such as <b>, <i>, <h1> and
  // <th> apply only when the author did not explicitly override them.
  const bool specifiedFontWeight = cssStyle.hasFontWeight();
  const bool specifiedFontStyle = cssStyle.hasFontStyle();
  const bool specifiedFontVariantCaps = cssStyle.hasFontVariantCaps();
  const bool specifiedTextDecoration = cssStyle.hasTextDecoration();
  const bool specifiedTextAlign = cssStyle.hasTextAlign();
  const bool specifiedVerticalAlign = cssStyle.hasVerticalAlign();

  // HTML dir attribute overrides CSS direction (case-insensitive per HTML spec)
  if (!dirAttr.empty()) {
    if (strcasecmp(dirAttr.c_str(), "rtl") == 0) {
      cssStyle.direction = CssTextDirection::Rtl;
      cssStyle.defined.direction = 1;
    } else if (strcasecmp(dirAttr.c_str(), "ltr") == 0) {
      cssStyle.direction = CssTextDirection::Ltr;
      cssStyle.defined.direction = 1;
    }
  }
  const bool specifiedDirection = cssStyle.hasDirection();

  if (self->renderMode == EpubRenderMode::Simplified) {
    cssStyle.defined.textDecoration = 0;
    cssStyle.textDecoration = CssTextDecoration::None;
  }

  // Direction is inherited in HTML/CSS. If this element does not define one, carry
  // the currently active inherited direction into its computed style.
  if (!cssStyle.hasDirection() && self->effectiveDirectionDefined) {
    cssStyle.direction = self->effectiveDirection;
    cssStyle.defined.direction = 1;
  }
  if (!cssStyle.hasTextAlign() && self->effectiveTextAlignDefined) {
    cssStyle.textAlign = self->effectiveTextAlign;
    cssStyle.defined.textAlign = 1;
  }

  // font-weight and font-style inherit across block boundaries too. Previously
  // <div class="bold"><p>...</p></div> lost its bold style when <p> replaced
  // currentCssStyle with its own declarations.
  if (!cssStyle.hasFontWeight() && self->effectiveFontWeightDefined) {
    cssStyle.fontWeight = self->effectiveBold ? CssFontWeight::Bold : CssFontWeight::Normal;
    cssStyle.defined.fontWeight = 1;
  }
  if (!cssStyle.hasFontStyle() && self->effectiveFontStyleDefined) {
    cssStyle.fontStyle = self->effectiveItalic ? CssFontStyle::Italic : CssFontStyle::Normal;
    cssStyle.defined.fontStyle = 1;
  }

  // Semantic HTML defaults behave like low-priority user-agent rules: an
  // author declaration on the element wins, while a merely inherited value
  // does not suppress the default.
  if (!specifiedFontWeight && (matches(name, HEADER_TAGS, std::size(HEADER_TAGS)) ||
                               matches(name, BOLD_TAGS, std::size(BOLD_TAGS)) || strcmp(name, "th") == 0)) {
    cssStyle.fontWeight = CssFontWeight::Bold;
    cssStyle.defined.fontWeight = 1;
  }
  if (!specifiedFontStyle && matches(name, ITALIC_TAGS, std::size(ITALIC_TAGS))) {
    cssStyle.fontStyle = CssFontStyle::Italic;
    cssStyle.defined.fontStyle = 1;
  }

  // font-variant-caps is inherited. Preserve an explicit `normal` on a child,
  // otherwise carry the active small-caps state into its computed style.
  if (!cssStyle.hasFontVariantCaps() && self->effectiveSmallCaps) {
    cssStyle.fontVariantCaps = CssFontVariantCaps::SmallCaps;
    cssStyle.defined.fontVariantCaps = 1;
  }

  // Text decorations propagate through descendants rather than behaving like
  // ordinary inherited properties: a child can add a line but cannot cancel a
  // line established by an ancestor.
  if (self->effectiveTextDecoration != CssTextDecoration::None) {
    cssStyle.textDecoration = cssStyle.hasTextDecoration() ? cssStyle.textDecoration | self->effectiveTextDecoration
                                                           : self->effectiveTextDecoration;
    cssStyle.defined.textDecoration = 1;
  }

  // Skip elements with display:none before all fast paths (tables, links, etc.).
  if (cssStyle.hasDisplay() && cssStyle.display == CssDisplay::None) {
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  self->trackCssAncestor(name, classAttr);

  if (self->tableDepth == 0 && isHeaderOrBlock(name)) {
    if (cssStyle.hasPageBreakBefore() && cssStyle.pageBreakBefore == CssPageBreak::Always) {
      self->forcePageBreak();
    }
    if (cssStyle.hasPageBreakAfter() && cssStyle.pageBreakAfter == CssPageBreak::Always) {
      self->registerPageBreakAfter(self->depth);
    }
  }

  // Buffer one simple row; oversized rows fall back to full-width flow.
  if (strcmp(name, "table") == 0) {
    // Flatten nested content without allocating a recursive row buffer.
    if (self->tableDepth > 0) {
      if (self->tableDepth == 1 && self->insideTableCell && self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      self->nextWordContinues = false;
      self->nextWordBreakWithoutSpace = false;
      self->tableDepth += 1;
      self->depth += 1;
      return;
    }

    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      self->makePages();
      self->currentTextBlock.reset();
    }
    self->flushPendingAnchor();
    self->pushTableTextStyleEntry(cssStyle);
    self->tableDepth = 1;
    self->insideTableCell = false;
    self->tableRowStacked = false;
    self->tableRowRtl = cssStyle.hasDirection() && cssStyle.direction == CssTextDirection::Rtl;
    self->tableRowsSpannedRemaining = 0;
    self->tableCellTextBytes = 0;
    self->tableRowCells.clear();
    self->tableRowCells.reserve(MAX_GRID_TABLE_COLUMNS);
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
    render_lab::recordTableStarted();
#endif
    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 && strcmp(name, "tr") == 0) {
    self->finishTableRow();
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      // Text before the first row is typically a <caption>.
      self->makePages();
    }
    self->currentTextBlock.reset();
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
    render_lab::recordTableRowStarted(static_cast<uint16_t>(self->completedPageCount));
#endif
    self->tableRowStacked = self->renderMode == EpubRenderMode::Simplified || self->tableRowsSpannedRemaining > 0;
    self->tableRowRtl = cssStyle.hasDirection() && cssStyle.direction == CssTextDirection::Rtl;
    if (self->tableRowsSpannedRemaining != UINT16_MAX && self->tableRowsSpannedRemaining > 0) {
      self->tableRowsSpannedRemaining--;
    }
    self->pushTableTextStyleEntry(cssStyle);
    self->depth += 1;
    return;
  }

  if (self->tableDepth == 1 && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->closeTableCell();
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      self->makePages();
    }
    self->currentTextBlock.reset();

    const uint16_t columnSpan = parseTableSpan(getAttribute(atts, "colspan"));
    const uint16_t rowSpan = parseTableSpan(getAttribute(atts, "rowspan"));
    if (columnSpan > 1 || rowSpan > 1) {
      self->fallbackTableRowToStacked();
    }
    if (rowSpan > 1) {
      const uint16_t remaining = rowSpan == UINT16_MAX ? UINT16_MAX : static_cast<uint16_t>(rowSpan - 1);
      self->tableRowsSpannedRemaining = std::max(self->tableRowsSpannedRemaining, remaining);
    }

    auto tableCellBlockStyle = BlockStyle();
    tableCellBlockStyle.textAlignDefined = true;
    tableCellBlockStyle.alignment =
        cssStyle.hasTextAlign()
            ? cssStyle.textAlign
            : (self->effectiveTextAlignDefined
                   ? self->effectiveTextAlign
                   : (cssStyle.hasDirection() && cssStyle.direction == CssTextDirection::Rtl ? CssTextAlign::Right
                                                                                             : CssTextAlign::Left));
    if (cssStyle.hasDirection()) {
      tableCellBlockStyle.directionDefined = true;
      tableCellBlockStyle.isRtl = cssStyle.direction == CssTextDirection::Rtl;
    }

    self->currentTextBlock =
        makeUniqueNoThrow<ParsedText>(self->extraParagraphSpacing, self->hyphenationEnabled, self->focusReadingEnabled,
                                      tableCellBlockStyle, self->wordSpacing);
    if (!self->currentTextBlock) {
      self->markLowMemoryFailure("table cell allocation");
      return;
    }
    self->insideTableCell = true;
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
    render_lab::recordTableCellStarted();
#endif
    self->tableCellTextBytes = 0;
    self->wordsExtractedInBlock = 0;
    self->flushPendingAnchor();
    self->pushTableTextStyleEntry(cssStyle);

    self->depth += 1;
    return;
  }

  if (self->tableDepth >= 1 && strcmp(name, "hr") == 0) {
    self->depth += 1;
    return;
  }

  if (self->tableDepth >= 1 && self->insideTableCell && isHeaderOrBlock(name)) {
    // Collapse block markup inside a cell to a word boundary.
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->nextWordContinues = false;
    self->nextWordBreakWithoutSpace = false;
    self->depth += 1;
    return;
  }

  if (self->tableDepth >= 1 && self->insideTableCell && matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS))) {
    // Preserve alt text without allocating an image framebuffer in the row.
    const char* alt = getAttribute(atts, "alt");
    if (alt && alt[0] != '\0') {
      self->syntheticCharacterData = true;
      self->characterData(userData, alt, strlen(alt));
      self->syntheticCharacterData = false;
    }
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  if (matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS))) {
    std::string src;
    std::string alt;
    if (atts != nullptr) {
      for (int i = 0; atts[i]; i += 2) {
        if (strcmp(atts[i], "src") == 0) {
          src = atts[i + 1];
        } else if (src.empty() && (strcmp(atts[i], "href") == 0 || strcmp(atts[i], "xlink:href") == 0)) {
          src = atts[i + 1];
        } else if (strcmp(atts[i], "alt") == 0) {
          alt = atts[i + 1];
        }
      }

      // EPUB image references are IRIs. A query/fragment identifies a view of
      // the same ZIP item, not part of its entry name. Strip delimiters before
      // percent-decoding so a literal encoded '?' or '#' in a filename remains
      // addressable.
      const size_t suffixPos = src.find_first_of("?#");
      if (suffixPos != std::string::npos) {
        src.resize(suffixPos);
      }

      // imageRendering: 0=display, 1=placeholder (alt text only), 2=suppress entirely
      if (self->imageRendering == 2) {
        self->skipUntilDepth = self->depth;
        self->depth += 1;
        return;
      }

      if (!src.empty() && self->imageRendering != 1) {
        LOG_DBG("EHP", "Found image: src=%s", src.c_str());

        {
          // Resolve the image path relative to the HTML file
          std::string resolvedPath = FsHelpers::normalisePath(FsHelpers::decodeUriEscapes(self->contentBase + src));

          // Probe content before looking at the href suffix. Valid EPUB items
          // are sometimes extensionless or use an opaque/misleading suffix;
          // the previous extension-only gate silently replaced those images
          // with their alt text even when the payload was a supported JPEG/PNG.
          ImageDimensions dims = {0, 0};
          ImageDimsProbe headerProbe;
          self->epub->readItemContentsToStream(resolvedPath, headerProbe, 1024, /*allowEarlyStop=*/true);
          bool gotDimensions = headerProbe.getDimensions(dims);
          const ImageDimsProbe::Format detectedFormat = headerProbe.getFormat();
          const bool supportedExtension =
              FsHelpers::hasJpgExtension(resolvedPath) || FsHelpers::hasPngExtension(resolvedPath);

          if (gotDimensions || supportedExtension) {
            std::string ext;
            if (detectedFormat == ImageDimsProbe::Format::Jpeg) {
              ext = ".jpg";
            } else if (detectedFormat == ImageDimsProbe::Format::Png) {
              ext = ".png";
            } else {
              const size_t extPos = resolvedPath.rfind('.');
              ext = extPos == std::string::npos ? std::string{} : resolvedPath.substr(extPos);
            }
            std::string cachedImagePath = self->imageBasePath + std::to_string(self->imageCounter++) + ext;

            {
              if (!gotDimensions) {
                // No header within the stream (rare) — fall back to extracting the
                // whole image and probing the file. That can take seconds, so
                // surface the indexing popup first (single-shot per parser).
                if (self->popupFn && !self->imagePopupFired) {
                  self->imagePopupFired = true;
                  self->popupFn();
                }
                HalFile cachedImageFile;
                bool extractSuccess = false;
                if (Storage.openFileForWrite("EHP", cachedImagePath, cachedImageFile)) {
                  extractSuccess = self->epub->readItemContentsToStream(resolvedPath, cachedImageFile, 4096);
                  cachedImageFile.flush();
                  cachedImageFile.close();
                }
                if (extractSuccess) {
                  // Retry to absorb SD-card sync latency on slow cards, and to close
                  // the silent-drop bug where a single getDimensions failure was fatal.
                  ImageToFramebufferDecoder* decoder = ImageDecoderFactory::getDecoder(cachedImagePath);
                  for (int attempt = 0; attempt < 3 && !gotDimensions; attempt++) {
                    if (attempt > 0) {
                      delay(50);  // Give a slow SD card time to finish syncing before retrying
                    }
                    gotDimensions = decoder && decoder->getDimensions(cachedImagePath, dims);
                  }
                } else {
                  LOG_ERR("EHP", "Failed to extract image");
                }
              }

              if (gotDimensions) {
                LOG_DBG("EHP", "Image dimensions: %dx%d", dims.width, dims.height);

                int displayWidth = 0;
                int displayHeight = 0;
                const float emSize = static_cast<float>(self->renderer.getFontAscenderSize(self->fontId));
                const CssStyle& imgStyle = cssStyle;
                const bool hasCssHeight = imgStyle.hasImageHeight();
                const bool hasCssWidth = imgStyle.hasImageWidth();

                // Compute effective container width for percentage-based image sizes.
                // If the image is inside a block with horizontal margins/padding (e.g.
                // <div style="margin: 1em 40%">), percentage widths like width:100%
                // should resolve against the container width, not the full viewport.
                auto imageHorizontalLayout = BlockStyle{}.resolveHorizontalLayout(self->viewportWidth);
                if (self->currentTextBlock) {
                  imageHorizontalLayout =
                      self->currentTextBlock->getBlockStyle().resolveHorizontalLayout(self->viewportWidth);
                }
                const int containerWidth = imageHorizontalLayout.contentWidth;

                if (hasCssHeight && hasCssWidth && dims.width > 0 && dims.height > 0) {
                  // Both CSS height and width set: resolve both, then clamp to viewport preserving requested ratio
                  displayHeight = static_cast<int>(
                      imgStyle.imageHeight.toPixels(emSize, static_cast<float>(self->viewportHeight)) + 0.5f);
                  displayWidth =
                      static_cast<int>(imgStyle.imageWidth.toPixels(emSize, static_cast<float>(containerWidth)) + 0.5f);
                  if (displayHeight < 1) displayHeight = 1;
                  if (displayWidth < 1) displayWidth = 1;
                  if (displayWidth > containerWidth || displayHeight > self->viewportHeight) {
                    float scaleX =
                        (displayWidth > containerWidth) ? static_cast<float>(containerWidth) / displayWidth : 1.0f;
                    float scaleY = (displayHeight > self->viewportHeight)
                                       ? static_cast<float>(self->viewportHeight) / displayHeight
                                       : 1.0f;
                    float scale = (scaleX < scaleY) ? scaleX : scaleY;
                    displayWidth = static_cast<int>(displayWidth * scale + 0.5f);
                    displayHeight = static_cast<int>(displayHeight * scale + 0.5f);
                    if (displayWidth < 1) displayWidth = 1;
                    if (displayHeight < 1) displayHeight = 1;
                  }
                  LOG_DBG("EHP", "Display size from CSS height+width: %dx%d", displayWidth, displayHeight);
                } else if (hasCssHeight && !hasCssWidth && dims.width > 0 && dims.height > 0) {
                  // Use CSS height (resolve % against viewport height) and derive width from aspect ratio
                  displayHeight = static_cast<int>(
                      imgStyle.imageHeight.toPixels(emSize, static_cast<float>(self->viewportHeight)) + 0.5f);
                  if (displayHeight < 1) displayHeight = 1;
                  displayWidth =
                      static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
                  if (displayHeight > self->viewportHeight) {
                    displayHeight = self->viewportHeight;
                    // Rescale width to preserve aspect ratio when height is clamped
                    displayWidth =
                        static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
                    if (displayWidth < 1) displayWidth = 1;
                  }
                  if (displayWidth > containerWidth) {
                    displayWidth = containerWidth;
                    // Rescale height to preserve aspect ratio when width is clamped
                    displayHeight =
                        static_cast<int>(displayWidth * (static_cast<float>(dims.height) / dims.width) + 0.5f);
                    if (displayHeight < 1) displayHeight = 1;
                  }
                  if (displayWidth < 1) displayWidth = 1;
                  LOG_DBG("EHP", "Display size from CSS height: %dx%d", displayWidth, displayHeight);
                } else if (hasCssWidth && !hasCssHeight && dims.width > 0 && dims.height > 0) {
                  // Use CSS width (resolve % against container width) and derive height from aspect ratio
                  displayWidth =
                      static_cast<int>(imgStyle.imageWidth.toPixels(emSize, static_cast<float>(containerWidth)) + 0.5f);
                  if (displayWidth > containerWidth) displayWidth = containerWidth;
                  if (displayWidth < 1) displayWidth = 1;
                  displayHeight =
                      static_cast<int>(displayWidth * (static_cast<float>(dims.height) / dims.width) + 0.5f);
                  if (displayHeight > self->viewportHeight) {
                    displayHeight = self->viewportHeight;
                    // Rescale width to preserve aspect ratio when height is clamped
                    displayWidth =
                        static_cast<int>(displayHeight * (static_cast<float>(dims.width) / dims.height) + 0.5f);
                    if (displayWidth < 1) displayWidth = 1;
                  }
                  if (displayHeight < 1) displayHeight = 1;
                  LOG_DBG("EHP", "Display size from CSS width: %dx%d", displayWidth, displayHeight);
                } else {
                  // Scale to fit container while maintaining aspect ratio
                  int maxWidth = containerWidth;
                  int maxHeight = self->viewportHeight;
                  float scaleX = (dims.width > maxWidth) ? (float)maxWidth / dims.width : 1.0f;
                  float scaleY = (dims.height > maxHeight) ? (float)maxHeight / dims.height : 1.0f;
                  float scale = (scaleX < scaleY) ? scaleX : scaleY;
                  if (scale > 1.0f) scale = 1.0f;

                  displayWidth = (int)(dims.width * scale);
                  displayHeight = (int)(dims.height * scale);
                  LOG_DBG("EHP", "Display size: %dx%d (scale %.2f)", displayWidth, displayHeight, scale);
                }

                // Flush any pending text block so it appears before the image
                if (self->partWordBufferIndex > 0) {
                  self->flushPartWordBuffer();
                }
                if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
                  const BlockStyle parentBlockStyle = self->currentTextBlock->getBlockStyle();
                  self->startNewTextBlock(parentBlockStyle);
                  if (self->failure_ == Failure::LowMemory) return;
                }

                // Apply vertical margins from the container to the image.
                // Top margin lives on the empty text block (deposited via vertical merge
                // in startNewTextBlock). Bottom margin was stripped by withoutBottom() for
                // deferred application at element close, so read it from the stack.
                int16_t imageMarginTop = 0;
                int16_t imageMarginBottom = 0;
                if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
                  const auto& bs = self->currentTextBlock->getBlockStyle();
                  imageMarginTop =
                      static_cast<int16_t>(std::clamp<int32_t>(bs.topInset(), 0, std::numeric_limits<int16_t>::max()));
                  if (self->blockStyleStack.size() > 1) {
                    imageMarginBottom = static_cast<int16_t>(std::clamp<int32_t>(
                        self->blockStyleStack.back().bottomInset(), 0, std::numeric_limits<int16_t>::max()));
                  }
                }

                // Create page for image - only break if image won't fit remaining space
                if (self->currentPage && !self->currentPage->elements.empty() &&
                    (self->currentPageNextY + imageMarginTop + displayHeight + imageMarginBottom >
                     self->viewportHeight)) {
                  self->completePageFn(std::move(self->currentPage), self->xpathParagraphIndex,
                                       self->xpathListItemIndex, self->currentPageVisibleOffset);
                  self->completedPageCount++;
                  self->currentPage = makeUniqueNoThrow<Page>();
                  if (!self->currentPage) {
                    self->markLowMemoryFailure("image page break");
                    return;
                  }
                  self->currentPageNextY = 0;
                  self->currentPageVisibleOffsetSet = false;
                  self->currentPage->setPublisherPageLabel(self->activePublisherPageLabel);
                } else if (!self->currentPage) {
                  self->currentPage = makeUniqueNoThrow<Page>();
                  if (!self->currentPage) {
                    self->markLowMemoryFailure("initial image page");
                    return;
                  }
                  self->currentPageNextY = 0;
                  self->currentPageVisibleOffsetSet = false;
                  self->currentPage->setPublisherPageLabel(self->activePublisherPageLabel);
                }

                // Apply top margin from container block. Clamp it so the image never
                // overflows the page bottom: a full-viewport-height image leaves no room
                // for the margin, and the break above only fires on non-empty pages, so a
                // fresh page would otherwise place the image at y=marginTop and run
                // marginTop pixels past viewportHeight. A large bottom reserve (status
                // bar / big screen margin) absorbs that overflow silently, but with a
                // thin reserve it crosses the physical screen edge and fails
                // ImageBlock::render's bounds check, dropping the image entirely.
                if (self->currentPageNextY + imageMarginTop + displayHeight > self->viewportHeight) {
                  const int room = self->viewportHeight - displayHeight - self->currentPageNextY;
                  imageMarginTop = static_cast<int16_t>(room > 0 ? room : 0);
                }
                self->currentPageNextY += imageMarginTop;

                // Create ImageBlock and add to page
                // Images arrive mid-parse when the heap is at its most loaded;
                // allocate without exceptions so OOM fails soft into the
                // null-check below.
                auto imageBlock = makeUniqueNoThrow<ImageBlock>(std::move(cachedImagePath), std::move(resolvedPath),
                                                                displayWidth, displayHeight);
                if (!imageBlock) {
                  self->markLowMemoryFailure("image block allocation");
                  return;
                }
                const int xPos = imageHorizontalLayout.xOffset + (containerWidth - displayWidth) / 2;
                auto pageImage = std::shared_ptr<PageImage>(
                    new (std::nothrow) PageImage(std::move(imageBlock), xPos, self->currentPageNextY));
                if (!pageImage) {
                  self->markLowMemoryFailure("page image allocation");
                  return;
                }
                self->applyPublisherPageLabel(*self->currentPage, self->visibleTextOffset);
                self->currentPage->elements.push_back(pageImage);
                self->setCurrentPageVisibleOffset(self->visibleTextOffset);
#if defined(SIMULATOR) && defined(CROSSPOINT_RENDER_LAB)
                render_lab::recordImageLayout(
                    ext == ".jpg" || ext == ".jpeg", static_cast<uint16_t>(dims.width),
                    static_cast<uint16_t>(dims.height), static_cast<uint16_t>(displayWidth),
                    static_cast<uint16_t>(displayHeight), static_cast<int16_t>(xPos),
                    static_cast<int16_t>(self->currentPageNextY), static_cast<uint16_t>(self->viewportWidth),
                    static_cast<uint16_t>(self->viewportHeight), static_cast<uint16_t>(self->completedPageCount));
#endif
                self->currentPageNextY += displayHeight + imageMarginBottom;

                // The image consumed the empty block's accumulated vertical spacing.
                // Reset the block so the Vertical merge in startNewTextBlock doesn't
                // re-apply the same margins to the next text paragraph.
                if (self->currentTextBlock && self->currentTextBlock->isEmpty()) {
                  BlockStyle resetStyle;
                  resetStyle.alignment = (self->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                                             ? CssTextAlign::Justify
                                             : static_cast<CssTextAlign>(self->paragraphAlignment);
                  self->currentTextBlock->setBlockStyle(resetStyle);
                }

                self->depth += 1;
                return;
              } else {
                LOG_ERR("EHP", "Failed to get image dimensions");
                Storage.remove(cachedImagePath.c_str());
              }
            }
          }
        }
      }

      // Fallback to alt text if image processing fails
      if (!alt.empty()) {
        alt = "[Image: " + alt + "]";
        self->startNewTextBlock(self->blockStyleStack.back()
                                    .getCombinedBlockStyle(centeredBlockStyle, BlockStyle::CombineAxis::Horizontal)
                                    .withoutBottom());
        StyleStackEntry altStyle;
        altStyle.depth = self->depth;
        altStyle.hasItalic = true;
        altStyle.italic = true;
        if (!self->pushInlineStyle(altStyle)) return;
        self->updateEffectiveInlineStyle();
        self->depth += 1;
        self->syntheticCharacterData = true;
        self->characterData(userData, alt.c_str(), alt.length());
        self->syntheticCharacterData = false;
        // Skip any child content (skip until parent as we pre-advanced depth above)
        self->skipUntilDepth = self->depth - 1;
        return;
      }

      // No alt text, skip
      self->skipUntilDepth = self->depth;
      self->depth += 1;
      return;
    }
  }

  // Ruby tag handling
  if (strcmp(name, "ruby") == 0) {
    // <ruby> is an inline element: a base that follows text with no whitespace between them
    // continues the same visual word, exactly like <b>/<i> handling in endElement().
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->inRuby = true;
    self->rubyStartWordIndex = self->currentTextBlock ? static_cast<int>(self->currentTextBlock->size()) : 0;
    if (self->currentTextBlock) {
      self->currentTextBlock->ensureRubyCapacity();
    }
    self->rubyTextBuffer.clear();
    self->depth += 1;
    return;
  }
  if (strcmp(name, "rt") == 0) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->collectingRubyText = true;
    self->depth += 1;
    return;
  }

  if (VisibleTextUtils::isNonVisibleElement(name)) {
    // start skip
    self->skipUntilDepth = self->depth;
    self->depth += 1;
    return;
  }

  // EPUB pagebreak markers are non-visible, but retain their bounded label and
  // content offset so the laid-out page can expose the publisher's numbering.
  if (atts != nullptr) {
    bool isPublisherPageBreak = false;
    for (int i = 0; atts[i]; i += 2) {
      if ((strcmp(atts[i], "role") == 0 && hasSpaceSeparatedToken(atts[i + 1], "doc-pagebreak")) ||
          (strcmp(atts[i], "epub:type") == 0 && hasSpaceSeparatedToken(atts[i + 1], "pagebreak")))
        isPublisherPageBreak = true;
    }
    if (isPublisherPageBreak) {
      const char* label = getAttribute(atts, "aria-label");
      if (!label || label[0] == '\0') label = getAttribute(atts, "title");
      if (!label || label[0] == '\0') label = getAttribute(atts, "id");
      self->addPublisherPageMarker(self->visibleTextOffset, label);
      self->skipUntilDepth = self->depth;
      self->depth += 1;
      return;
    }
  }

  // Detect internal <a href="..."> links (footnotes, cross-references)
  // Note: <aside epub:type="footnote"> elements are rendered as normal content
  // without special handling. Links pointing to them are collected as footnotes.
  if (strcmp(name, "a") == 0) {
    const char* href = getAttribute(atts, "href");

    bool isInternalLink = isInternalEpubLink(href);

    // Special case: javascript:void(0) links with data attributes
    // Example: <a href="javascript:void(0)"
    // data-xyz="{&quot;name&quot;:&quot;OPS/ch2.xhtml&quot;,&quot;frag&quot;:&quot;id46&quot;}">
    if (href && strncmp(href, "javascript:", 11) == 0) {
      isInternalLink = false;
      // TODO: Parse data-* attributes to extract actual href
    }

    if (isInternalLink) {
      // Footnote indices are block-relative, so linked rows use ordinary flow.
      if (self->tableDepth >= 1 && self->insideTableCell && !self->tableRowStacked) {
        self->fallbackTableRowToStacked();
      }

      // Flush buffer before style change
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
      self->insideFootnoteLink = true;
      self->footnoteLinkDepth = self->depth;
      self->currentFootnoteLinkId = self->currentTextBlock ? self->currentTextBlock->addLinkTarget(href) : 0;
      strncpy(self->currentFootnote.href, href, sizeof(self->currentFootnote.href) - 1);
      self->currentFootnote.href[sizeof(self->currentFootnote.href) - 1] = '\0';
      self->currentFootnote.number[0] = '\0';
      self->currentFootnoteLinkTextLen = 0;

      // Internal links are underlined by default, but author CSS on the link
      // may replace that default. Ancestor decorations still propagate.
      self->pushDecorationStyleEntry(CssTextDecoration::Underline, cssStyle);

      // Skip CSS resolution — we already handled styling for this <a> tag
      self->depth += 1;
      return;
    }
  }

  const float emSize = static_cast<float>(self->renderer.getFontAscenderSize(self->fontId));
  auto userAlignmentBlockStyle = BlockStyle::fromCssStyle(
      cssStyle, emSize, static_cast<CssTextAlign>(self->paragraphAlignment), self->viewportWidth);
  // A number of Vietnamese EPUBs encode paragraphs as bare <p> elements and
  // lose their first-line indent during conversion. Repair only that narrow
  // case: explicit (including hanging) indents and centred/right-aligned text
  // remain publisher-controlled.
  if (self->repairParagraphIndent && strcmp(name, "p") == 0 &&
      (!userAlignmentBlockStyle.textIndentDefined || userAlignmentBlockStyle.textIndent == 0) &&
      (userAlignmentBlockStyle.alignment == CssTextAlign::Left ||
       userAlignmentBlockStyle.alignment == CssTextAlign::Justify ||
       userAlignmentBlockStyle.alignment == CssTextAlign::None)) {
    userAlignmentBlockStyle.textIndentDefined = true;
    userAlignmentBlockStyle.textIndent = static_cast<int16_t>(emSize);
  }

  if (strcmp(name, "hr") == 0) {
    auto hrBlockStyle = BlockStyle::fromCssStyle(cssStyle, emSize, CssTextAlign::Left, self->viewportWidth);
    if (!self->embeddedStyle) {
      hrBlockStyle.marginLeft = 0;
      hrBlockStyle.marginRight = 0;
      hrBlockStyle.marginTop = 0;
      hrBlockStyle.marginBottom = 0;
      hrBlockStyle.paddingLeft = 0;
      hrBlockStyle.paddingRight = 0;
      hrBlockStyle.paddingTop = 0;
      hrBlockStyle.paddingBottom = 0;
      hrBlockStyle.textIndentDefined = false;
      hrBlockStyle.textIndent = 0;
    }
    self->emitHorizontalRule(hrBlockStyle);
    self->depth += 1;
    return;
  }

  if (matches(name, HEADER_TAGS, std::size(HEADER_TAGS))) {
    self->currentCssStyle = cssStyle;
    auto headerBlockStyle = BlockStyle::fromCssStyle(cssStyle, emSize, CssTextAlign::Center, self->viewportWidth);
    headerBlockStyle.textAlignDefined = true;
    if (self->embeddedStyle && cssStyle.hasTextAlign()) {
      headerBlockStyle.alignment = cssStyle.textAlign;
    }
    const auto accumulated =
        self->blockStyleStack.back().getCombinedBlockStyle(headerBlockStyle, BlockStyle::CombineAxis::Horizontal);
    self->blockStyleStack.push_back(accumulated);
    self->blockCssStyleStack.push_back(cssStyle);
    self->startNewTextBlock(accumulated.withoutBottom());
    self->updateEffectiveInlineStyle();
  } else if (matches(name, BLOCK_TAGS, std::size(BLOCK_TAGS))) {
    if (strcmp(name, "br") == 0) {
      if (self->partWordBufferIndex > 0) {
        // flush word preceding <br/> to currentTextBlock before calling startNewTextBlock
        self->flushPartWordBuffer();
      }
      // A <br> after text is a line break: start the next block with the container's
      // vertical margins stripped, matching browsers, which never apply paragraph
      // margins at a <br>. This is what keeps <br>-per-paragraph books (common CJK
      // web-novel formatting) from re-adding container spacing at every paragraph
      // and collapsing page capacity.
      // A <br> on an empty block (consecutive <br>s, or a standalone <br> between
      // blocks) is a scene-break separator: keep the container margins so deposited
      // vertical spacing survives. Either way the block is tagged so that if it
      // stays empty, startNewTextBlock injects a full line-height gap when the next
      // block opens; once text follows the tag is inert.
      // Style comes from the block style stack, not the current block, so a closed
      // element's style can't leak through (#2679).
      BlockStyle brStyle = self->blockStyleStack.back();
      if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
        brStyle = brStyle.withoutTop().withoutBottom();
      }
      brStyle.fromBrElement = true;
      self->startNewTextBlock(brStyle);
    } else {
      self->currentCssStyle = cssStyle;
      const auto accumulated = self->blockStyleStack.back().getCombinedBlockStyle(userAlignmentBlockStyle,
                                                                                  BlockStyle::CombineAxis::Horizontal);
      self->blockStyleStack.push_back(accumulated);
      self->blockCssStyleStack.push_back(cssStyle);
      self->startNewTextBlock(accumulated.withoutBottom());
      self->updateEffectiveInlineStyle();

      if (strcmp(name, "li") == 0) {
        self->currentTextBlock->addWord("\xe2\x80\xa2", EpdFontFamily::REGULAR, false, false, self->visibleTextOffset);
        self->listItemBulletOnly = true;
      }
    }
  } else if (matches(name, UNDERLINE_TAGS, std::size(UNDERLINE_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->pushDecorationStyleEntry(CssTextDecoration::Underline, cssStyle);
  } else if (matches(name, LINETHROUGH_TAGS, std::size(LINETHROUGH_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    self->pushDecorationStyleEntry(CssTextDecoration::LineThrough, cssStyle);
  } else if (matches(name, BOLD_TAGS, std::size(BOLD_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    // Push inline style entry for bold tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasBold = true;
    entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
    if (cssStyle.hasFontStyle()) {
      entry.hasItalic = true;
      entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
    }
    applyTextDecorationToEntry(entry, cssStyle);
    applySmallCapsToEntry(entry, cssStyle);
    applyDirectionToEntry(entry, cssStyle);
    if (!self->pushInlineStyle(entry)) return;
    self->updateEffectiveInlineStyle();
  } else if (matches(name, ITALIC_TAGS, std::size(ITALIC_TAGS))) {
    // Flush buffer before style change so preceding text gets current style
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    // Push inline style entry for italic tag
    StyleStackEntry entry;
    entry.depth = self->depth;  // Track depth for matching pop
    entry.hasItalic = true;
    entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
    if (cssStyle.hasFontWeight()) {
      entry.hasBold = true;
      entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
    }
    applyTextDecorationToEntry(entry, cssStyle);
    applySmallCapsToEntry(entry, cssStyle);
    applyDirectionToEntry(entry, cssStyle);
    if (!self->pushInlineStyle(entry)) return;
    self->updateEffectiveInlineStyle();
  } else if (strcmp(name, "sup") == 0 || strcmp(name, "sub") == 0) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
      self->nextWordContinues = true;
    }
    StyleStackEntry entry;
    entry.depth = self->depth;
    applySmallCapsToEntry(entry, cssStyle);
    if (strcmp(name, "sup") == 0) {
      entry.hasSup = true;
      entry.sup = true;
    } else {
      entry.hasSub = true;
      entry.sub = true;
    }
    if (!self->pushInlineStyle(entry)) return;
    self->updateEffectiveInlineStyle();
  } else if (strcmp(name, "span") == 0 || !isHeaderOrBlock(name)) {
    // Handle span and other inline elements for CSS styling
    const bool specifiedTableTextAlign = self->tableDepth >= 1 && specifiedTextAlign;
    if (specifiedFontWeight || specifiedFontStyle || specifiedFontVariantCaps || specifiedTextDecoration ||
        specifiedDirection || specifiedVerticalAlign || specifiedTableTextAlign) {
      // Flush buffer before style change so preceding text gets current style
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
      StyleStackEntry entry;
      entry.depth = self->depth;  // Track depth for matching pop
      if (specifiedFontWeight) {
        entry.hasBold = true;
        entry.bold = cssStyle.fontWeight == CssFontWeight::Bold;
      }
      if (specifiedFontStyle) {
        entry.hasItalic = true;
        entry.italic = cssStyle.fontStyle == CssFontStyle::Italic;
      }
      if (specifiedFontVariantCaps) applySmallCapsToEntry(entry, cssStyle);
      if (specifiedTextDecoration) applyTextDecorationToEntry(entry, cssStyle);
      if (specifiedDirection) applyDirectionToEntry(entry, cssStyle);
      entry.setsParagraphDirection = strcmp(name, "html") == 0 || strcmp(name, "body") == 0;
      if (specifiedTableTextAlign) {
        entry.hasTextAlign = true;
        entry.textAlign = cssStyle.textAlign;
      }
      if (specifiedVerticalAlign) applyVerticalAlignToEntry(entry, cssStyle);
      if (!self->pushInlineStyle(entry)) return;
      self->updateEffectiveInlineStyle();
    }
  }

  // Unprocessed tag, just increasing depth and continue forward
  self->depth += 1;
}

void XMLCALL ChapterHtmlSlimParser::characterData(void* userData, const XML_Char* s, const int len) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  if (self->shouldAbortForLowMemory("character data")) return;
  const bool countVisibleOffsets = self->insideBody && self->nonVisibleTextDepth == 0 && !self->syntheticCharacterData;
  const uint32_t callbackVisibleOffset = self->visibleTextOffset;
  if (countVisibleOffsets) {
    // Expat supplies character data as (pointer, length), not as a NUL-terminated
    // string. Count UTF-8 sequence starts inside that exact bound instead of
    // calling utf8NextCodepoint(), which is intentionally a C-string decoder and
    // may inspect continuation bytes beyond this callback. Expat has already
    // validated the XML, so every non-continuation byte is one visible scalar.
    for (int i = 0; i < len; ++i) {
      if ((static_cast<uint8_t>(s[i]) & 0xC0) != 0x80) {
        self->visibleTextOffset++;
      }
    }
  }

  // Nested content needs an enclosing bounded cell collector.
  if (self->tableDepth > 1 && !self->insideTableCell) {
    return;
  }

  // Middle of skip
  if (self->skipUntilDepth < self->depth) {
    return;
  }

  // Collect ruby text instead of normal word processing.
  if (self->collectingRubyText) {
    self->rubyTextBuffer.append(s, len);
    return;
  }

  if (self->tableDepth == 1 && !self->insideTableCell) {
    bool onlyWhitespace = true;
    for (int i = 0; i < len; ++i) {
      if (!isWhitespace(s[i])) {
        onlyWhitespace = false;
        break;
      }
    }
    if (onlyWhitespace) {
      return;
    }
  }

  // Recreate flow storage for valid text (for example a caption) after a row.
  if (!self->currentTextBlock) {
    const BlockStyle flowStyle =
        self->blockStyleStack.empty() ? BlockStyle() : self->blockStyleStack.back().withoutBottom();
    self->currentTextBlock = makeUniqueNoThrow<ParsedText>(self->extraParagraphSpacing, self->hyphenationEnabled,
                                                           self->focusReadingEnabled, flowStyle, self->wordSpacing);
    if (!self->currentTextBlock) {
      self->markLowMemoryFailure("character-data text block");
      return;
    }
    self->wordsExtractedInBlock = 0;
  }

  // Collect footnote link display text (for the number label)
  // Skip whitespace and brackets to normalize noterefs like "[1]" → "1"
  if (self->insideFootnoteLink) {
    int start = 0;
    int end = len - 1;

    // Example input and output texts:
    // "     [  12  ]   " => "12"
    // "   turn to 256  " => "turn to 256"

    // Ignore leading whitespaces and left square brackets
    while (start < len && (isWhitespace(s[start]) || (s[start] == '['))) {
      ++start;
    }

    // Ignore trailing whitespaces and right square brackets
    while (end >= start && (isWhitespace(s[end]) || (s[end] == ']'))) {
      --end;
    }

    // Extract footnote link text
    for (int i = start; (self->currentFootnoteLinkTextLen < sizeof(self->currentFootnote.number) - 1) && (i <= end);
         ++i) {
      self->currentFootnote.number[self->currentFootnoteLinkTextLen++] = s[i];
    }
    self->currentFootnote.number[self->currentFootnoteLinkTextLen] = '\0';
  }

  uint32_t nextCodepointOffset = callbackVisibleOffset;
  for (int i = 0; i < len; i++) {
    const uint32_t codepointOffset = nextCodepointOffset;
    if (countVisibleOffsets && (static_cast<uint8_t>(s[i]) & 0xC0) != 0x80) {
      nextCodepointOffset++;
    }

    if (isWhitespace(s[i])) {
      // Currently looking at whitespace, if there's anything in the partWordBuffer, flush it
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      // Whitespace is a real word boundary — reset continuation state
      self->nextWordContinues = false;
      self->nextWordBreakWithoutSpace = false;
      // Skip the whitespace char
      continue;
    }

    // U+200B ZERO WIDTH SPACE is an invisible line-break opportunity. Keep
    // the neighbouring text visually attached when it stays on one line, but
    // expose a non-stretching boundary to the line breaker. Leaving U+200B in
    // the glyph token made long identifiers and some converted EPUB text wrap
    // only through emergency hyphenation.
    if (static_cast<uint8_t>(s[i]) == 0xE2 && i + 2 < len && static_cast<uint8_t>(s[i + 1]) == 0x80 &&
        static_cast<uint8_t>(s[i + 2]) == 0x8B) {
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }
      self->nextWordContinues = true;
      self->nextWordBreakWithoutSpace = true;
      i += 2;
      continue;
    }

    // Detect U+00A0 (non-breaking space, UTF-8: 0xC2 0xA0) or
    //        U+202F (narrow no-break space, UTF-8: 0xE2 0x80 0xAF).
    //
    // Both are rendered as a visible space but must never allow a line break around them.
    // We split the no-break space into its own word token and link the surrounding words
    // with continuation flags so the layout engine treats them as an indivisible group.
    //
    // Example: "200&#xA0;Quadratkilometer" or "200&#x202F;Quadratkilometer"
    //   Input bytes:  "200\xC2\xA0Quadratkilometer"  (or 0xE2 0x80 0xAF for U+202F)
    //   Tokens produced:
    //     [0] "200"               continues=false
    //     [1] " "                 continues=true   (attaches to "200", no gap)
    //     [2] "Quadratkilometer"  continues=true   (attaches to " ", no gap)
    //
    //   The continuation flags prevent the line-breaker from inserting a line break
    //   between "200" and "Quadratkilometer". However, "Quadratkilometer" is now a
    //   standalone word for hyphenation purposes, so Liang patterns can produce
    //   "200 Quadrat-" / "kilometer" instead of the unusable "200" / "Quadratkilometer".
    if (static_cast<uint8_t>(s[i]) == 0xC2 && i + 1 < len && static_cast<uint8_t>(s[i + 1]) == 0xA0) {
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }

      self->partWordBuffer[0] = ' ';
      self->partWordBuffer[1] = '\0';
      self->partWordBufferIndex = 1;
      self->partWordVisibleOffset = codepointOffset;
      self->nextWordContinues = true;  // Attach space to previous word (no break).
      self->flushPartWordBuffer();

      self->nextWordContinues = true;  // Next real word attaches to this space (no break).

      i++;  // Skip the second byte (0xA0)
      continue;
    }

    // U+202F (narrow no-break space) — identical logic to U+00A0 above.
    if (static_cast<uint8_t>(s[i]) == 0xE2 && i + 2 < len && static_cast<uint8_t>(s[i + 1]) == 0x80 &&
        static_cast<uint8_t>(s[i + 2]) == 0xAF) {
      if (self->partWordBufferIndex > 0) {
        self->flushPartWordBuffer();
      }

      self->partWordBuffer[0] = ' ';
      self->partWordBuffer[1] = '\0';
      self->partWordBufferIndex = 1;
      self->partWordVisibleOffset = codepointOffset;
      self->nextWordContinues = true;
      self->flushPartWordBuffer();

      self->nextWordContinues = true;

      i += 2;  // Skip the remaining two bytes (0x80 0xAF)
      continue;
    }

    // Skip Zero Width No-Break Space / BOM (U+FEFF) = 0xEF 0xBB 0xBF
    const XML_Char FEFF_BYTE_1 = static_cast<XML_Char>(0xEF);
    const XML_Char FEFF_BYTE_2 = static_cast<XML_Char>(0xBB);
    const XML_Char FEFF_BYTE_3 = static_cast<XML_Char>(0xBF);

    if (s[i] == FEFF_BYTE_1) {
      // Check if the next two bytes complete the 3-byte sequence
      if ((i + 2 < len) && (s[i + 1] == FEFF_BYTE_2) && (s[i + 2] == FEFF_BYTE_3)) {
        // Sequence 0xEF 0xBB 0xBF found!
        i += 2;    // Skip the next two bytes
        continue;  // Move to the next iteration
      }
    }

    // If we're about to run out of space, then cut the word off and start a new one.
    // For CJK text (no spaces), this is the primary word-breaking mechanism.
    // We must avoid splitting multi-byte UTF-8 sequences across word boundaries,
    // otherwise the trailing bytes become orphaned continuation bytes that the
    // decoder can't interpret.
    if (self->partWordBufferIndex >= MAX_WORD_SIZE) {
      int safeLen = utf8SafeTruncateBuffer(self->partWordBuffer, self->partWordBufferIndex);

      if (safeLen < self->partWordBufferIndex && safeLen > 0) {
        // Incomplete UTF-8 sequence at the end — save it before flushing
        int overflow = self->partWordBufferIndex - safeLen;
        uint32_t overflowVisibleOffset = self->partWordVisibleOffset;
        const unsigned char* offsetPtr = reinterpret_cast<const unsigned char*>(self->partWordBuffer);
        const unsigned char* const safeEnd = offsetPtr + safeLen;
        while (offsetPtr < safeEnd) {
          utf8NextCodepoint(&offsetPtr);
          overflowVisibleOffset++;
        }
        char saved[4];
        for (int j = 0; j < overflow; j++) {
          saved[j] = self->partWordBuffer[safeLen + j];
        }
        self->partWordBufferIndex = safeLen;
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
        for (int j = 0; j < overflow; j++) {
          self->partWordBuffer[j] = saved[j];
        }
        self->partWordBufferIndex = overflow;
        self->partWordVisibleOffset = overflowVisibleOffset;
      } else {
        self->flushPartWordBuffer();
        self->nextWordContinues = true;
      }
    }

    if (self->partWordBufferIndex == 0) {
      self->partWordVisibleOffset = codepointOffset;
    }
    self->partWordBuffer[self->partWordBufferIndex++] = s[i];
  }

  // Keep token growth bounded: CSS-heavy spans can fragment text into many tiny
  // words, so flush earlier when embedded CSS is active. We still keep the
  // "exclude last line" behavior to preserve paragraph flow across chunks.
  const size_t blockWordCount = self->currentTextBlock->size();
  const size_t softFlushThreshold =
      self->embeddedStyle ? TEXT_BLOCK_SOFT_FLUSH_WORDS_WITH_CSS : TEXT_BLOCK_SOFT_FLUSH_WORDS;
  if (blockWordCount > softFlushThreshold && !self->inRuby) {
    LOG_DBG("EHP", "Text block soft flush (%u words)", static_cast<unsigned>(blockWordCount));
    const auto horizontalLayout =
        resolveTextHorizontalLayout(self->currentTextBlock->getBlockStyle(), self->viewportWidth,
                                    self->renderer.getLineHeight(self->fontId, self->lineCompression));
    if (!self->currentTextBlock->layoutAndExtractLines(
            self->renderer, self->fontId, horizontalLayout.contentWidth,
            [self](const std::shared_ptr<TextBlock>& textBlock, const uint32_t offset) {
              self->addLineToPage(textBlock, offset);
            },
            false)) {
      self->markLowMemoryFailure("partial text layout");
    }
  }
}

void XMLCALL ChapterHtmlSlimParser::defaultHandlerExpand(void* userData, const XML_Char* s, const int len) {
  // Check if this looks like an entity reference (&...;)
  if (len >= 3 && s[0] == '&' && s[len - 1] == ';') {
    const char* utf8Value = lookupHtmlEntity(s, static_cast<size_t>(len));
    if (utf8Value != nullptr) {
      // Known entity: expand to its UTF-8 value
      characterData(userData, utf8Value, strlen(utf8Value));
      return;
    }
    // Unknown entity: preserve original &...; sequence
    characterData(userData, s, len);
    return;
  }
  // Not an entity we recognize - skip it
}

void XMLCALL ChapterHtmlSlimParser::endElement(void* userData, const XML_Char* name) {
  auto* self = static_cast<ChapterHtmlSlimParser*>(userData);
  if (self->failure_ == Failure::LowMemory) return;
  if (self->nonVisibleTextDepth > 0) {
    self->nonVisibleTextDepth--;
  }

  // Ruby text: </rt> distributes ruby to base words, </ruby> resets ruby state
  if (strcmp(name, "rt") == 0) {
    self->collectingRubyText = false;
    if (self->inRuby && self->currentTextBlock) {
      const int currentWordCount = static_cast<int>(self->currentTextBlock->size());
      const int baseWordCount = currentWordCount - self->rubyStartWordIndex;
      std::string cleanRuby = utf8ComposeNfc(trimAndNormalize(self->rubyTextBuffer));
      if (!cleanRuby.empty()) {
        if (baseWordCount > 0) {
          self->currentTextBlock->setRubyGroupAt(self->rubyStartWordIndex, baseWordCount, cleanRuby);
          self->rubyStartWordIndex = currentWordCount;
        } else if (self->rubyStartWordIndex > 0) {
          int leaderIdx = self->rubyStartWordIndex - 1;
          while (leaderIdx >= 0 &&
                 (self->currentTextBlock->getWordStyleAt(leaderIdx) & EpdFontFamily::RUBY_CONTINUE) != 0) {
            leaderIdx--;
          }
          if (leaderIdx >= 0) {
            std::string prevRuby = self->currentTextBlock->getRubyTextAt(leaderIdx);
            self->currentTextBlock->setRubyForWordAt(leaderIdx, prevRuby + cleanRuby);
          }
        }
      }
    }
    self->rubyTextBuffer.clear();
    // Inline close: the next base (e.g. 字 in <ruby>漢<rt>かん</rt>字<rt>じ</rt></ruby>) joins the
    // preceding one with no space. Whitespace in the source resets this in characterData().
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      self->nextWordContinues = true;
    }
    self->depth -= 1;
    return;
  }
  if (strcmp(name, "ruby") == 0 && self->inRuby) {
    self->inRuby = false;
    self->rubyStartWordIndex = -1;
    self->rubyTextBuffer.clear();
    // Inline close: text following </ruby> joins the annotated base with no space.
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      self->nextWordContinues = true;
    }
    self->depth -= 1;
    return;
  }
  // Check if any style state will change after we decrement depth
  // If so, we MUST flush the partWordBuffer with the CURRENT style first
  // Note: depth hasn't been decremented yet, so we check against (depth - 1)
  const bool willPopStyleStack =
      !self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth - 1;
  const bool styleWillChange = willPopStyleStack;
  const bool headerOrBlockTag = isHeaderOrBlock(name);
  const bool tableStructuralTag = isTableStructuralTag(name);
  const bool insideSkippedSubtree = self->depth - 1 >= self->skipUntilDepth;

  if (!insideSkippedSubtree && self->tableDepth > 1 && strcmp(name, "table") == 0) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->nextWordContinues = false;
    self->nextWordBreakWithoutSpace = false;
    self->tableDepth -= 1;
    self->depth -= 1;
    LOG_DBG("EHP", "nested table flattened into enclosing cell");
    return;
  }

  if (!insideSkippedSubtree && self->tableDepth >= 1 && self->insideTableCell && headerOrBlockTag) {
    if (self->partWordBufferIndex > 0) {
      self->flushPartWordBuffer();
    }
    self->nextWordContinues = false;
    self->nextWordBreakWithoutSpace = false;
    self->depth -= 1;
    return;
  }

  // Flush buffer with current style BEFORE any style changes
  if (self->partWordBufferIndex > 0) {
    // Flush if style will change OR if we're closing a block/structural element
    const bool isInlineTag = !headerOrBlockTag && !tableStructuralTag &&
                             !matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS)) && self->depth != 1;
    const bool shouldFlush = styleWillChange || headerOrBlockTag || matches(name, BOLD_TAGS, std::size(BOLD_TAGS)) ||
                             matches(name, ITALIC_TAGS, std::size(ITALIC_TAGS)) ||
                             matches(name, UNDERLINE_TAGS, std::size(UNDERLINE_TAGS)) ||
                             matches(name, LINETHROUGH_TAGS, std::size(LINETHROUGH_TAGS)) || tableStructuralTag ||
                             matches(name, IMAGE_TAGS, std::size(IMAGE_TAGS)) || self->depth == 1;

    if (shouldFlush) {
      self->flushPartWordBuffer();
      // If closing an inline element, the next word fragment continues the same visual word
      if (isInlineTag) {
        self->nextWordContinues = true;
      }
    }
  }

  self->depth -= 1;

  // Closing a footnote link — create entry from collected text and href
  if (self->insideFootnoteLink && self->depth == self->footnoteLinkDepth) {
    if (self->currentFootnote.number[0] != '\0' && self->currentFootnote.href[0] != '\0') {
      FootnoteEntry entry;
      strncpy(entry.number, self->currentFootnote.number, sizeof(entry.number) - 1);
      entry.number[sizeof(entry.number) - 1] = '\0';
      strncpy(entry.href, self->currentFootnote.href, sizeof(entry.href) - 1);
      entry.href[sizeof(entry.href) - 1] = '\0';
      int wordIndex =
          self->wordsExtractedInBlock + (self->currentTextBlock ? static_cast<int>(self->currentTextBlock->size()) : 0);
      self->pendingFootnotes.push_back({wordIndex, entry});
    }
    self->insideFootnoteLink = false;
    self->currentFootnoteLinkId = 0;
  }

  // Leaving skip
  if (self->skipUntilDepth == self->depth) {
    self->skipUntilDepth = INT_MAX;
  }

  if (!insideSkippedSubtree && self->tableDepth == 1 && (strcmp(name, "td") == 0 || strcmp(name, "th") == 0)) {
    self->closeTableCell();
    self->nextWordContinues = false;
    self->nextWordBreakWithoutSpace = false;
  }

  if (!insideSkippedSubtree && self->tableDepth == 1 && (strcmp(name, "tr") == 0)) {
    self->finishTableRow();
    self->nextWordContinues = false;
    self->nextWordBreakWithoutSpace = false;
  }

  if (!insideSkippedSubtree && self->tableDepth == 1 && strcmp(name, "table") == 0) {
    self->finishTableRow();
    if (self->currentTextBlock && !self->currentTextBlock->isEmpty()) {
      self->makePages();
    }
    self->currentTextBlock.reset();
    self->tableDepth = 0;
    self->insideTableCell = false;
    self->tableRowStacked = false;
    self->tableRowsSpannedRemaining = 0;
    self->tableCellTextBytes = 0;
    self->tableRowCells.clear();
    self->nextWordContinues = false;
    self->nextWordBreakWithoutSpace = false;

    const BlockStyle flowStyle =
        self->blockStyleStack.empty() ? BlockStyle() : self->blockStyleStack.back().withoutBottom();
    self->currentTextBlock = makeUniqueNoThrow<ParsedText>(self->extraParagraphSpacing, self->hyphenationEnabled,
                                                           self->focusReadingEnabled, flowStyle, self->wordSpacing);
    if (!self->currentTextBlock) {
      self->markLowMemoryFailure("post-table text block");
    }
    self->wordsExtractedInBlock = 0;
  }

  // Pop from inline style stack if we pushed an entry at this depth
  // This handles all inline elements: b, i, u, span, etc.
  if (!self->inlineStyleStack.empty() && self->inlineStyleStack.back().depth == self->depth) {
    self->inlineStyleStack.pop_back();
    self->updateEffectiveInlineStyle();
  }

  // Clear block style when leaving header or block elements
  if (headerOrBlockTag && !insideSkippedSubtree) {
    // br is self-closing and not a container — it doesn't push/pop the stack.
    if (strcmp(name, "br") != 0 && self->blockStyleStack.size() > 1) {
      // Apply closing element's bottom margin to the current text block so
      // container spacing appears after the element's content (on the last child),
      // not on the first child via the empty-block merge in startNewTextBlock.
      if (self->currentTextBlock) {
        const auto style = self->currentTextBlock->getBlockStyle();
        self->currentTextBlock->setBlockStyle(style.addBottom(self->blockStyleStack.back()));
      }
      self->blockStyleStack.pop_back();
      if (self->blockCssStyleStack.size() > 1) self->blockCssStyleStack.pop_back();
      self->currentCssStyle = self->blockCssStyleStack.back();
      self->updateEffectiveInlineStyle();
      // Start a new text block with the parent style to prevent subsequent bare text
      // from inheriting the closed block style (e.g. alignment or margins).
      self->startNewTextBlock(self->blockStyleStack.back());
    }

    // </li> closes: if the bullet never got inline text (empty <li> or <li> with only
    // block children that were flushed), clear the flag so the next sibling doesn't
    // merge into this block.
    if (strcmp(name, "li") == 0) {
      self->listItemBulletOnly = false;
    }
  }
  if (!insideSkippedSubtree && self->consumePageBreakAfter(self->depth)) {
    self->forcePageBreak();
  }
  if (strcmp(name, "body") == 0) {
    self->insideBody = false;
  }
  if (strcmp(name, "html") == 0) {
    self->htmlEnded_ = true;
  }
}

ChapterHtmlSlimParser::~ChapterHtmlSlimParser() { abortParse(); }

bool ChapterHtmlSlimParser::beginParse() {
  failure_ = Failure::None;
  attemptedLowMemoryFontCacheRelease_ = false;
  htmlEnded_ = false;
  if (shouldAbortForLowMemory("parser initialization")) return false;
  if (!styleArena.initialized()) {
    if (!styleArena.init(2048, 16 * 1024)) {
      markLowMemoryFailure("style arena initialization");
      return false;
    }
  } else {
    styleArena.clear();
  }
  inlineStyleStack.resetStorage();
  cssAncestorMasks.fill(0);
  pageBreakAfterCount = 0;
  publisherPageMarkerCount = 0;
  nextPublisherPageMarker = 0;
  activePublisherPageLabel[0] = '\0';
  for (const auto& pageAnchor : publisherPageAnchors) {
    if (pageAnchor.first.empty()) addPublisherPageMarker(0, pageAnchor.second.c_str());
  }
  // Initialize block style stack with a root entry representing "no ancestor block elements".
  // The user's paragraph alignment is set as the default so child elements without explicit
  // text-align inherit it correctly through getCombinedBlockStyle.
  BlockStyle rootBlockStyle;
  rootBlockStyle.alignment = (this->paragraphAlignment == static_cast<uint8_t>(CssTextAlign::None))
                                 ? CssTextAlign::Justify
                                 : static_cast<CssTextAlign>(this->paragraphAlignment);
  blockStyleStack.clear();
  blockStyleStack.reserve(8);
  blockStyleStack.push_back(rootBlockStyle);
  blockCssStyleStack.clear();
  blockCssStyleStack.reserve(8);
  blockCssStyleStack.emplace_back();
  currentCssStyle = blockCssStyleStack.back();

  tableDepth = 0;
  insideTableCell = false;
  tableRowStacked = false;
  tableRowsSpannedRemaining = 0;
  tableCellTextBytes = 0;
  tableRowCells.clear();
  for (auto& lines : tableCellLines) {
    lines.clear();
  }
  tableLineVisibleOffsets.clear();

  auto paragraphAlignmentBlockStyle = BlockStyle();
  paragraphAlignmentBlockStyle.textAlignDefined = true;
  const auto align = rootBlockStyle.alignment;
  paragraphAlignmentBlockStyle.alignment = align;
  startNewTextBlock(paragraphAlignmentBlockStyle);
  if (failure_ == Failure::LowMemory) return false;

  xmlParser_ = XML_ParserCreate(nullptr);
  if (!xmlParser_) {
    markLowMemoryFailure("XML parser allocation");
    return false;
  }

  // Handle HTML entities (like &nbsp;) that aren't in XML spec or DTD
  // Using DefaultHandlerExpand preserves normal entity expansion from DOCTYPE
  XML_SetDefaultHandlerExpand(xmlParser_, defaultHandlerExpand);

  if (!Storage.openFileForRead("EHP", filepath, parseFile_)) {
    failure_ = Failure::Io;
    destroyXmlParser(xmlParser_);
    xmlParser_ = nullptr;
    return false;
  }

  // Get file size to decide whether to show indexing popup.
  if (popupFn && parseFile_.size() >= MIN_SIZE_FOR_POPUP) {
    popupFn();
  }

  XML_SetUserData(xmlParser_, this);
  XML_SetElementHandler(xmlParser_, startElement, endElement);
  XML_SetCharacterDataHandler(xmlParser_, characterData);

  parseStartTime_ = millis();
  return true;
}

ChapterHtmlSlimParser::ParseStatus ChapterHtmlSlimParser::parseStep() {
  if (shouldAbortForLowMemory("XML parse chunk")) return ParseStatus::Error;
  void* const buf = XML_GetBuffer(xmlParser_, PARSE_BUFFER_SIZE);
  if (!buf) {
    markLowMemoryFailure("XML parse buffer");
    return ParseStatus::Error;
  }

  const size_t len = parseFile_.read(buf, PARSE_BUFFER_SIZE);

  if (len == 0 && parseFile_.available() > 0) {
    LOG_ERR("EHP", "File read error");
    failure_ = Failure::Io;
    return ParseStatus::Error;
  }

  const int done = parseFile_.available() == 0;

  if (XML_ParseBuffer(xmlParser_, static_cast<int>(len), done) == XML_STATUS_ERROR) {
    if (failure_ == Failure::LowMemory) return ParseStatus::Error;
    if (XML_GetErrorCode(xmlParser_) == XML_ERROR_NO_MEMORY) {
      markLowMemoryFailure("XML parser");
      return ParseStatus::Error;
    }
    if (htmlEnded_) {
      LOG_DBG("EHP", "Ignoring trailing data after </html>: %s", XML_ErrorString(XML_GetErrorCode(xmlParser_)));
      return ParseStatus::Done;
    }
    LOG_ERR("EHP", "Parse error at line %lu:\n%s", XML_GetCurrentLineNumber(xmlParser_),
            XML_ErrorString(XML_GetErrorCode(xmlParser_)));
    failure_ = Failure::InvalidContent;
    return ParseStatus::Error;
  }

  return done ? ParseStatus::Done : ParseStatus::More;
}

void ChapterHtmlSlimParser::abortParse() {
  if (xmlParser_) {
    destroyXmlParser(xmlParser_);
    xmlParser_ = nullptr;
  }
  // Only close the file if it was successfully opened in beginParse()
  if (parseFile_.isOpen()) {
    parseFile_.close();
  }
}

bool ChapterHtmlSlimParser::finishParse() {
  if (failure_ == Failure::LowMemory) {
    abortParse();
    return false;
  }
  if (xmlParser_) {
    LOG_DBG("EHP", "Time to parse and build pages: %lu ms", millis() - parseStartTime_);
    destroyXmlParser(xmlParser_);
    xmlParser_ = nullptr;
  }
  parseFile_.close();

  // Process last page if there is still text
  if (currentTextBlock) {
    makePages();
    if (failure_ == Failure::LowMemory) return false;
    if (!pendingAnchorId.empty()) {
      anchorData.push_back({std::move(pendingAnchorId), static_cast<uint16_t>(completedPageCount)});
      pendingAnchorId.clear();
    }
    setCurrentPageVisibleOffset(visibleTextOffset);
    completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, currentPageVisibleOffset);
    completedPageCount++;
    currentPage.reset();
    currentTextBlock.reset();
  }

  return true;
}

bool ChapterHtmlSlimParser::parseAndBuildPages() {
  if (!beginParse()) {
    return false;
  }
  for (;;) {
    const ParseStatus status = parseStep();
    if (status == ParseStatus::Error) {
      abortParse();
      return false;
    }
    if (status == ParseStatus::Done) {
      break;
    }
  }
  return finishParse();
}

void ChapterHtmlSlimParser::addLineToPage(std::shared_ptr<TextBlock> line, const uint32_t visibleOffset,
                                          const int16_t xOffsetOverride) {
  if (shouldAbortForLowMemory("page line layout")) return;
  const int baseLineHeight = renderer.getLineHeight(fontId, lineCompression);
  const int lineHeight = baseLineHeight + line->getRubyShift(renderer.getFontAscenderSize(fontId));

  if (!currentPage) {
    currentPage = makeUniqueNoThrow<Page>();
    if (!currentPage) {
      markLowMemoryFailure("page allocation");
      return;
    }
    currentPageNextY = 0;
    currentPageVisibleOffsetSet = false;
    currentPage->setPublisherPageLabel(activePublisherPageLabel);
  }

  if (currentPageNextY + lineHeight > viewportHeight) {
    setCurrentPageVisibleOffset(visibleOffset);
    completePageFn(std::move(currentPage), xpathParagraphIndex, xpathListItemIndex, currentPageVisibleOffset);
    completedPageCount++;
    currentPage = makeUniqueNoThrow<Page>();
    if (!currentPage) {
      markLowMemoryFailure("page-break allocation");
      return;
    }
    currentPageNextY = 0;
    currentPageVisibleOffsetSet = false;
    currentPage->setPublisherPageLabel(activePublisherPageLabel);
  }
  setCurrentPageVisibleOffset(visibleOffset);
  applyPublisherPageLabel(*currentPage, visibleOffset);

  // Track cumulative words to assign footnotes to the page containing their anchor
  wordsExtractedInBlock += line->wordCount();
  auto footnoteIt = pendingFootnotes.begin();
  while (footnoteIt != pendingFootnotes.end() && footnoteIt->first <= wordsExtractedInBlock) {
    currentPage->addFootnote(footnoteIt->second.number, footnoteIt->second.href);
    ++footnoteIt;
  }
  pendingFootnotes.erase(pendingFootnotes.begin(), footnoteIt);

  // Apply horizontal left inset (margin + padding) as x position offset
  const int16_t xOffset =
      xOffsetOverride >= 0 ? xOffsetOverride
                           : resolveTextHorizontalLayout(line->getBlockStyle(), viewportWidth, baseLineHeight).xOffset;
  const int rubyShift = line->getRubyShift(renderer.getFontAscenderSize(fontId));
  for (const auto& link : line->takeLinkSpans()) {
    if (!currentPage->addLink(link.href, static_cast<int16_t>(xOffset + link.x),
                              static_cast<int16_t>(currentPageNextY + rubyShift - link.topLift), link.width,
                              static_cast<int16_t>(baseLineHeight + link.topLift))) {
      LOG_DBG("EHP", "Dropped page link: %.48s", link.href);
    }
  }
  auto pageLine = std::shared_ptr<PageLine>(new (std::nothrow) PageLine(std::move(line), xOffset, currentPageNextY));
  if (!pageLine) {
    markLowMemoryFailure("page-line allocation");
    return;
  }
  currentPage->elements.push_back(std::move(pageLine));
  currentPageNextY += lineHeight;
}

void ChapterHtmlSlimParser::makePages() {
  if (shouldAbortForLowMemory("page layout")) return;
  if (!currentTextBlock) {
    LOG_ERR("EHP", "!! No text block to make pages for !!");
    return;
  }

  if (!currentPage) {
    currentPage = makeUniqueNoThrow<Page>();
    if (!currentPage) {
      markLowMemoryFailure("page layout allocation");
      return;
    }
    currentPageNextY = 0;
    currentPageVisibleOffsetSet = false;
    currentPage->setPublisherPageLabel(activePublisherPageLabel);
  }

  const int lineHeight = renderer.getLineHeight(fontId, lineCompression);

  // Apply top spacing before the paragraph (stored in pixels)
  const BlockStyle& blockStyle = currentTextBlock->getBlockStyle();
  if (blockStyle.marginTop > 0) {
    currentPageNextY += blockStyle.marginTop;
  }
  if (blockStyle.paddingTop > 0) {
    currentPageNextY += blockStyle.paddingTop;
  }

  // Calculate effective width accounting for horizontal margins/padding
  const auto horizontalLayout = resolveTextHorizontalLayout(blockStyle, viewportWidth, lineHeight);

  if (!currentTextBlock->layoutAndExtractLines(renderer, fontId, horizontalLayout.contentWidth,
                                               [this](const std::shared_ptr<TextBlock>& textBlock,
                                                      const uint32_t offset) { addLineToPage(textBlock, offset); })) {
    markLowMemoryFailure("text layout");
    return;
  }
  if (failure_ == Failure::LowMemory) return;

  // Fallback: transfer any remaining pending footnotes to current page.
  // Normally addLineToPage handles this via word-index tracking, but this catches
  // edge cases where a footnote's word index equals the exact block size.
  if (!pendingFootnotes.empty() && currentPage) {
    for (const auto& [idx, fn] : pendingFootnotes) {
      currentPage->addFootnote(fn.number, fn.href);
    }
    pendingFootnotes.clear();
  }

  // Apply bottom spacing after the paragraph (stored in pixels)
  if (blockStyle.marginBottom > 0) {
    currentPageNextY += blockStyle.marginBottom;
  }
  if (blockStyle.paddingBottom > 0) {
    currentPageNextY += blockStyle.paddingBottom;
  }

  // Extra paragraph spacing if enabled (default behavior)
  if (extraParagraphSpacing) {
    currentPageNextY += lineHeight / 2;
  }
}
