add_executable(ClippingCodecTest ClippingCodecTest.cpp ${REPO_ROOT}/src/clippings/ClippingCodec.cpp)
target_include_directories(ClippingCodecTest PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/stubs ${REPO_ROOT}/src/clippings)
target_link_libraries(ClippingCodecTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ClippingCodecTest)
