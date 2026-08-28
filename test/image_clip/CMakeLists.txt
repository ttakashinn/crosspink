add_executable(image_clip_tests ImageClipTest.cpp)
target_include_directories(image_clip_tests PRIVATE ../../lib/Epub/Epub/converters)
add_test(NAME image_clip_tests COMMAND image_clip_tests)

