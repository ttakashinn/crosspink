add_executable(NetworkModeTest
  NetworkModeTest.cpp
)

target_link_libraries(NetworkModeTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_include_directories(NetworkModeTest PRIVATE
  ${REPO_ROOT}/src
)

gtest_discover_tests(NetworkModeTest)
