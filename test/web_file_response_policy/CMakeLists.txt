add_executable(web_file_response_policy_test
  WebFileResponsePolicyTest.cpp
)

target_link_libraries(web_file_response_policy_test PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(web_file_response_policy_test)
