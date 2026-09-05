add_executable(ManifestItemIndexTest ManifestItemIndexTest.cpp)
target_include_directories(ManifestItemIndexTest PRIVATE
  ${REPO_ROOT}/lib/Epub/Epub/parsers
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/test/arena/stubs
)
target_link_libraries(ManifestItemIndexTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ManifestItemIndexTest)
