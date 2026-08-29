add_executable(VanNhanSoProfileTest
  VanNhanSoProfileTest.cpp
  ${REPO_ROOT}/src/features/vannhanso/VanNhanSoProfile.cpp
)

target_include_directories(VanNhanSoProfileTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src/features/vannhanso
)

target_link_libraries(VanNhanSoProfileTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(VanNhanSoProfileTest)
