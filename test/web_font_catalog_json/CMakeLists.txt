add_executable(WebFontCatalogJsonTest
  WebFontCatalogJsonTest.cpp
)

target_link_libraries(WebFontCatalogJsonTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

target_include_directories(WebFontCatalogJsonTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
)

gtest_discover_tests(WebFontCatalogJsonTest)
