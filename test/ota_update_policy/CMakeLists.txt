add_executable(OtaUpdatePolicyTest
  OtaUpdatePolicyTest.cpp
)

target_link_libraries(OtaUpdatePolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_include_directories(OtaUpdatePolicyTest PRIVATE
  ${REPO_ROOT}/src
)

gtest_discover_tests(OtaUpdatePolicyTest)
