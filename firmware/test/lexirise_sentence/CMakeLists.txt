add_executable(LexiriseSentenceTest
  SentenceBuilderTest.cpp
  ParagraphAndTapTest.cpp
  ${REPO_ROOT}/src/lexirise/text/Punctuation.cpp
  ${REPO_ROOT}/src/lexirise/text/SentenceBuilder.cpp
  ${REPO_ROOT}/src/lexirise/text/BookLanguage.cpp
  ${REPO_ROOT}/src/lexirise/text/TapContext.cpp
  ${REPO_ROOT}/src/lexirise/settings/Settings.cpp
  ${REPO_ROOT}/src/lexirise/net/Http.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_compile_definitions(LexiriseSentenceTest PRIVATE LEXIRISE=1)

target_include_directories(LexiriseSentenceTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(LexiriseSentenceTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseSentenceTest)
