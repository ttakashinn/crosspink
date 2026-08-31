add_executable(StorageUsageTest
  StorageUsageTest.cpp
  ${REPO_ROOT}/src/util/StorageUsage.cpp
)

target_compile_definitions(StorageUsageTest PRIVATE SIMULATOR=1)
target_include_directories(StorageUsageTest PRIVATE ${REPO_ROOT}/src)
target_link_libraries(StorageUsageTest PRIVATE crosspoint_test_common GTest::gtest_main)

gtest_discover_tests(StorageUsageTest)
