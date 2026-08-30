#include <gtest/gtest.h>

#include "features/dictionary_review/DictionaryReviewCard.h"

TEST(DictionaryReviewCardTest, ExtractsAvailableStructuredFields) {
  const auto card = dictionary_review::extractCard(
      "take part",
      "IPA: /teɪk pɑːt/\nđộng từ\ntham gia\nVí dụ: She took part in the meeting.\nCollocation: take part in");
  EXPECT_EQ(card.word, "take part");
  EXPECT_EQ(card.phonetic, "/teɪk pɑːt/");
  EXPECT_EQ(card.meaning, "tham gia");
  EXPECT_EQ(card.example, "She took part in the meeting.");
  EXPECT_EQ(card.collocation, "take part in");
  EXPECT_TRUE(card.usable());
}

TEST(DictionaryReviewCardTest, AcceptsWordAndMeaningWithoutOptionalDetails) {
  const auto card = dictionary_review::extractCard("book", "quyển sách");
  EXPECT_EQ(card.meaning, "quyển sách");
  EXPECT_TRUE(card.phonetic.empty());
  EXPECT_TRUE(card.example.empty());
  EXPECT_TRUE(card.collocation.empty());
  EXPECT_TRUE(card.usable());
}

TEST(DictionaryReviewCardTest, RejectsEmptyMeaning) {
  const auto card = dictionary_review::extractCard("book", "Pronunciation: /bʊk/\nExample: This book is useful.");
  EXPECT_TRUE(card.meaning.empty());
  EXPECT_FALSE(card.usable());
}

TEST(DictionaryReviewCardTest, TreatsStarDictTypeSeparatorsAsLineBreaks) {
  std::string definition = "IPA: /bʊk/";
  definition.push_back('\0');
  definition += "quyển sách";
  definition.push_back('\0');
  definition += "Example: This book is useful.";
  const auto card = dictionary_review::extractCard("book", definition);
  EXPECT_EQ(card.phonetic, "/bʊk/");
  EXPECT_EQ(card.meaning, "quyển sách");
  EXPECT_EQ(card.example, "This book is useful.");
}

TEST(DictionaryReviewCardTest, DoesNotMisclassifyDefinitionSentencesContainingExampleWord) {
  const auto card = dictionary_review::extractCard("model", "một ví dụ điển hình dùng để minh họa");
  EXPECT_EQ(card.meaning, "một ví dụ điển hình dùng để minh họa");
  EXPECT_TRUE(card.example.empty());
  EXPECT_TRUE(card.usable());
}

TEST(DictionaryReviewCardTest, KeepsMeaningAfterPartOfSpeechLabel) {
  const auto card = dictionary_review::extractCard("book", "noun: quyển sách");
  EXPECT_EQ(card.meaning, "quyển sách");
  EXPECT_TRUE(card.usable());
}

TEST(DictionaryReviewCardTest, DoesNotTreatPartOfSpeechSubstringAsMetadata) {
  const auto card = dictionary_review::extractCard("proverb", "proverb used to give practical advice");
  EXPECT_EQ(card.meaning, "proverb used to give practical advice");
  EXPECT_TRUE(card.usable());
}

TEST(DictionaryReviewCardTest, StripsHyphenSeparatedOptionalLabel) {
  const auto card = dictionary_review::extractCard("book", "quyển sách\nExample - This book is useful.");
  EXPECT_EQ(card.meaning, "quyển sách");
  EXPECT_EQ(card.example, "This book is useful.");
}
