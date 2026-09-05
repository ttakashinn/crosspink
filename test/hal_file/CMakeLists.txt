add_executable(HalFileTest
  HalFileTest.cpp
  ${REPO_ROOT}/lib/EpdFont/SdCardFontRegistry.cpp
  ${REPO_ROOT}/lib/hal/HalStorage.cpp
)

target_include_directories(HalFileTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/hal
)

target_compile_definitions(HalFileTest PRIVATE
  FREEINK_CAP_USB_MSC=0
  HAL_STORAGE_TESTING=1
)

target_link_libraries(HalFileTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(HalFileTest)
