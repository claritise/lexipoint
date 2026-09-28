add_executable(LexirisePageTest
  PageAnalysisTest.cpp
  PageStoreTest.cpp
  PrefetchTest.cpp
  InputTest.cpp
  SavedStateTest.cpp
  UtilTest.cpp
  ${REPO_ROOT}/src/lexirise/page/PageAnalysis.cpp
  ${REPO_ROOT}/src/lexirise/page/PageStore.cpp
  ${REPO_ROOT}/src/lexirise/page/Prefetch.cpp
  ${REPO_ROOT}/src/lexirise/page/PageSentences.cpp
  ${REPO_ROOT}/src/lexirise/lookup/WholeWords.cpp
  ${REPO_ROOT}/src/lexirise/api/Responses.cpp
  ${REPO_ROOT}/src/lexirise/api/Requests.cpp
  ${REPO_ROOT}/src/lexirise/api/LexiriseClient.cpp
  ${REPO_ROOT}/src/lexirise/vocab/VocabMirror.cpp
  ${REPO_ROOT}/src/lexirise/api/VocabPage.cpp
  ${REPO_ROOT}/src/lexirise/net/JsonStream.cpp
  ${REPO_ROOT}/src/lexirise/net/JsonReader.cpp
  ${REPO_ROOT}/src/lexirise/net/JsonWriter.cpp
  ${REPO_ROOT}/src/lexirise/net/Http.cpp
  ${REPO_ROOT}/src/lexirise/settings/SafeFile.cpp
  ${REPO_ROOT}/src/lexirise/settings/Settings.cpp
  ${REPO_ROOT}/src/lexirise/text/SentenceBuilder.cpp
  ${REPO_ROOT}/src/lexirise/text/Punctuation.cpp
  ${REPO_ROOT}/src/lexirise/text/BookLanguage.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_compile_definitions(LexirisePageTest PRIVATE LEXIRISE=1)

target_include_directories(LexirisePageTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/test/lexirise_fakes
  ${REPO_ROOT}/test/lexirise_fakes/stubs
)

target_link_libraries(LexirisePageTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexirisePageTest)
