add_executable(VanNhanSoUpdatePolicyTest
  VanNhanSoUpdatePolicyTest.cpp
  ${REPO_ROOT}/src/features/vannhanso/VanNhanSoUpdatePolicy.cpp
)

target_link_libraries(VanNhanSoUpdatePolicyTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_include_directories(VanNhanSoUpdatePolicyTest PRIVATE
  ${REPO_ROOT}/src
)

gtest_discover_tests(VanNhanSoUpdatePolicyTest)
