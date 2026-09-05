add_executable(XtcPixelPlanesTest XtcPixelPlanesTest.cpp)
target_include_directories(XtcPixelPlanesTest PRIVATE ${REPO_ROOT}/src/util ${REPO_ROOT}/lib/Xtc)
target_link_libraries(XtcPixelPlanesTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(XtcPixelPlanesTest)
