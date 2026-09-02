add_executable(WebServerLoopPolicyTest
  WebServerLoopPolicyTest.cpp
)

target_link_libraries(WebServerLoopPolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_include_directories(WebServerLoopPolicyTest PRIVATE
  ${REPO_ROOT}/src
)

gtest_discover_tests(WebServerLoopPolicyTest)
