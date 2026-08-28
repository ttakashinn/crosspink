add_executable(DitherUtilsTest DitherUtilsTest.cpp)
target_link_libraries(DitherUtilsTest PRIVATE GTest::gtest_main crosspoint_test_common)
target_include_directories(DitherUtilsTest PRIVATE ${REPO_ROOT}/lib/Epub)
gtest_discover_tests(DitherUtilsTest)
