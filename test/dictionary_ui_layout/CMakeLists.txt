add_executable(DictionaryUiLayoutTest DictionaryUiLayoutTest.cpp)
target_include_directories(DictionaryUiLayoutTest PRIVATE ${REPO_ROOT}/src/activities/reader ${REPO_ROOT}/src
                                                          ${REPO_ROOT}/src/util)
target_link_libraries(DictionaryUiLayoutTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(DictionaryUiLayoutTest)
