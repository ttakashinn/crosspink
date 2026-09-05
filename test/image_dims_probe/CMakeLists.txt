add_executable(ImageDimsProbeTest
  ImageDimsProbeTest.cpp
  ImageDimensionValidationStub.cpp
  ${REPO_ROOT}/lib/Epub/Epub/converters/ImageDimsProbe.cpp
)
target_include_directories(ImageDimsProbeTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/lib/Epub/Epub/converters
)
target_link_libraries(ImageDimsProbeTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ImageDimsProbeTest)
