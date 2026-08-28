add_executable(ArenaTest ArenaTest.cpp)
target_include_directories(ArenaTest PRIVATE ${REPO_ROOT}/lib/Memory ${CMAKE_CURRENT_SOURCE_DIR}/stubs)
target_link_libraries(ArenaTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ArenaTest)
