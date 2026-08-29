add_executable(ClippingCodecTest
  ClippingCodecTest.cpp
  ClippingStoreTest.cpp
  SavedItemsCatalogTest.cpp
  ${REPO_ROOT}/src/clippings/ClippingCodec.cpp
  ${REPO_ROOT}/src/clippings/ClippingStore.cpp
  ${REPO_ROOT}/src/saved_items/SavedItemsCatalog.cpp
)
target_include_directories(ClippingCodecTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/test/minibidi_arabic/stubs
  ${REPO_ROOT}/src/clippings
  ${REPO_ROOT}/src
)
target_link_libraries(ClippingCodecTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(ClippingCodecTest)
