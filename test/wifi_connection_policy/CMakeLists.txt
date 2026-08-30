add_executable(WifiConnectionPolicyTest
  WifiConnectionPolicyTest.cpp
  ${REPO_ROOT}/src/network/WifiConnectionPolicy.cpp
)

target_link_libraries(WifiConnectionPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_include_directories(WifiConnectionPolicyTest PRIVATE
  ${REPO_ROOT}/src
)

gtest_discover_tests(WifiConnectionPolicyTest)
