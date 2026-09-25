add_executable(LexiriseDevTest
  DevProtocolTest.cpp
  DevGestureTest.cpp
  DevOverlayTest.cpp
  DevTimingTest.cpp
  ${REPO_ROOT}/src/lexirise/dev/DevProtocol.cpp
  ${REPO_ROOT}/src/lexirise/dev/DevGesture.cpp
)

target_compile_definitions(LexiriseDevTest PRIVATE LEXIPOINT_DEV_HARNESS=1)

target_include_directories(LexiriseDevTest PRIVATE
  ${REPO_ROOT}/lib/hal
  ${REPO_ROOT}/src/lexirise/dev
)

target_link_libraries(LexiriseDevTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseDevTest)
