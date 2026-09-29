add_executable(KeyboardLayersTest
  KeyboardLayersTest.cpp
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/src/FreeInkUI.cpp
)
target_include_directories(KeyboardLayersTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/freeink-sdk/libs/ui/FreeInkUI/include
)
target_link_libraries(KeyboardLayersTest PRIVATE crosspoint_test_common GTest::gtest_main)
gtest_discover_tests(KeyboardLayersTest)
