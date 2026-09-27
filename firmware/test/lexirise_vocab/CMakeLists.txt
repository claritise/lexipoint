add_executable(LexiriseVocabTest
  VocabMirrorTest.cpp
  VocabSyncSimTest.cpp
  ${REPO_ROOT}/src/lexirise/vocab/VocabMirror.cpp
  ${REPO_ROOT}/src/lexirise/api/VocabPage.cpp
  ${REPO_ROOT}/src/lexirise/api/Requests.cpp
  ${REPO_ROOT}/src/lexirise/api/Responses.cpp
  ${REPO_ROOT}/src/lexirise/api/LexiriseClient.cpp
  ${REPO_ROOT}/src/lexirise/net/JsonStream.cpp
  ${REPO_ROOT}/src/lexirise/net/JsonReader.cpp
  ${REPO_ROOT}/src/lexirise/net/JsonWriter.cpp
  ${REPO_ROOT}/src/lexirise/net/Http.cpp
  ${REPO_ROOT}/src/lexirise/settings/SafeFile.cpp
  ${REPO_ROOT}/src/lexirise/settings/Settings.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_compile_definitions(LexiriseVocabTest PRIVATE LEXIRISE=1)

target_include_directories(LexiriseVocabTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/test/lexirise_fakes
  ${REPO_ROOT}/test/lexirise_fakes/stubs
)

target_link_libraries(LexiriseVocabTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseVocabTest)
