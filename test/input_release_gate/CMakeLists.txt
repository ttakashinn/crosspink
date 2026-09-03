add_executable(InputReleaseGateTest
  InputReleaseGateTest.cpp
)

target_include_directories(InputReleaseGateTest PRIVATE
  ${CMAKE_SOURCE_DIR}/../src
)

target_link_libraries(InputReleaseGateTest PRIVATE
  GTest::gtest_main
)

gtest_discover_tests(InputReleaseGateTest)
