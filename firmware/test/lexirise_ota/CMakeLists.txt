add_executable(LexiriseOtaTest
  ReleaseVersionTest.cpp
  ReleaseAssetTest.cpp
  ${REPO_ROOT}/src/lexirise/ota/ReleaseVersion.cpp
)


target_include_directories(LexiriseOtaTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(LexiriseOtaTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseOtaTest)
