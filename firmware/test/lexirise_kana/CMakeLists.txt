add_executable(LexiriseKanaTest
  KanaTest.cpp
  ConjugationTest.cpp
  Utf8PrefixTest.cpp
  ${REPO_ROOT}/src/lexirise/text/Kana.cpp
  ${REPO_ROOT}/src/lexirise/text/Conjugation.cpp
  ${REPO_ROOT}/src/lexirise/text/Punctuation.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_compile_definitions(LexiriseKanaTest PRIVATE LEXIRISE=1)

target_include_directories(LexiriseKanaTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(LexiriseKanaTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseKanaTest)
