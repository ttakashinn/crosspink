add_executable(HomeCoverTest
  HomeCoverTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/Bitmap.cpp
  ${REPO_ROOT}/lib/GfxRenderer/BitmapHelpers.cpp
)

target_include_directories(HomeCoverTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/src/components
)

target_link_libraries(HomeCoverTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HomeCoverTest)
