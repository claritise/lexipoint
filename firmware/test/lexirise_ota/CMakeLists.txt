add_executable(LexiriseOtaTest
  ReleaseVersionTest.cpp
  ${REPO_ROOT}/src/lexirise/ota/ReleaseVersion.cpp
)

target_compile_definitions(LexiriseOtaTest PRIVATE LEXIRISE=1)

target_include_directories(LexiriseOtaTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(LexiriseOtaTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseOtaTest)
