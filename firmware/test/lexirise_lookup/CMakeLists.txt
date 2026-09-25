add_executable(LexiriseLookupTest
  MatchTest.cpp
  LookupParseTest.cpp
  LexiriseLookupTest.cpp
  StarDictCandidatesTest.cpp
  LongPressTest.cpp
  StarDictChoiceTest.cpp
  WholeWordsTest.cpp
  ${REPO_ROOT}/src/lexirise/lookup/LexiriseLookup.cpp
  ${REPO_ROOT}/src/lexirise/lookup/WholeWords.cpp
  ${REPO_ROOT}/src/lexirise/lookup/LookupCard.cpp
  ${REPO_ROOT}/src/lexirise/api/Responses.cpp
  ${REPO_ROOT}/src/lexirise/net/JsonReader.cpp
  ${REPO_ROOT}/src/lexirise/text/BookLanguage.cpp
  ${REPO_ROOT}/src/lexirise/settings/Settings.cpp
  ${REPO_ROOT}/src/lexirise/net/Http.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_compile_definitions(LexiriseLookupTest PRIVATE LEXIRISE=1)

target_include_directories(LexiriseLookupTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(LexiriseLookupTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseLookupTest)
