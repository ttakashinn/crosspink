add_executable(DictionaryReviewCardTest DictionaryReviewCardTest.cpp DictionaryReviewLayoutTest.cpp
               ../../src/features/dictionary_review/DictionaryReviewCard.cpp)
target_include_directories(DictionaryReviewCardTest PRIVATE ${CMAKE_SOURCE_DIR}/../src)
target_link_libraries(DictionaryReviewCardTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(DictionaryReviewCardTest)
