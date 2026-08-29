add_executable(ReadingStatsTest
  ReadingStatsTest.cpp
  ReadingStatsStoreTest.cpp
  ${REPO_ROOT}/src/activities/reader/ReadingStatsStore.cpp
)
target_include_directories(ReadingStatsTest PRIVATE
  ${REPO_ROOT}/test/clipping_codec/stubs
  ${REPO_ROOT}/test/minibidi_arabic/stubs
  ${REPO_ROOT}/src
)
target_link_libraries(ReadingStatsTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ReadingStatsTest)
