add_executable(HomeMenuViewportTest
  HomeMenuViewportTest.cpp
)

target_include_directories(HomeMenuViewportTest PRIVATE
  ${REPO_ROOT}/src/activities/home
)

target_link_libraries(HomeMenuViewportTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HomeMenuViewportTest)
