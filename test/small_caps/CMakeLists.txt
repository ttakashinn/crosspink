add_executable(small_caps_test SmallCapsTest.cpp)
target_link_libraries(small_caps_test PRIVATE crosspoint_test_common GTest::gtest_main)
target_include_directories(small_caps_test PRIVATE ${REPO_ROOT}/lib/GfxRenderer)
gtest_discover_tests(small_caps_test)
