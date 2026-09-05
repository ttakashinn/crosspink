add_executable(FontReadSafetyTest FontReadSafetyTest.cpp)

target_include_directories(FontReadSafetyTest PRIVATE
  ${REPO_ROOT}/lib/EpdFont
)

target_link_libraries(FontReadSafetyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(FontReadSafetyTest)
