add_executable(LexiriseLanguageTest
  BookLanguageTest.cpp
  ${REPO_ROOT}/src/lexirise/text/BookLanguage.cpp
  ${REPO_ROOT}/src/lexirise/settings/Settings.cpp
  ${REPO_ROOT}/src/lexirise/net/Http.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_compile_definitions(LexiriseLanguageTest PRIVATE LEXIRISE=1)

target_include_directories(LexiriseLanguageTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(LexiriseLanguageTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseLanguageTest)
