add_executable(LexiriseDevCoordsTest
  LexiriseDevCoordsTest.cpp
)

target_link_libraries(LexiriseDevCoordsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseDevCoordsTest)
