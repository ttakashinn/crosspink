add_executable(DictionaryTest
  DictionaryQueryTest.cpp
  DictionaryHistoryStoreTest.cpp
  ${REPO_ROOT}/src/util/DictionaryQuery.cpp
  ${REPO_ROOT}/src/util/DictionaryHistoryStore.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_include_directories(DictionaryTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/minibidi_arabic/stubs
  ${REPO_ROOT}/src/util
  ${REPO_ROOT}/lib/Utf8
)
target_compile_definitions(DictionaryTest PRIVATE UNIT_TEST)
target_link_libraries(DictionaryTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(DictionaryTest)
