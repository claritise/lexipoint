add_executable(LexiriseDeckTest
  BookDeckTest.cpp
  ${REPO_ROOT}/src/lexirise/deck/BookDeck.cpp
  ${REPO_ROOT}/src/lexirise/settings/BookTags.cpp
  ${REPO_ROOT}/src/lexirise/settings/SafeFile.cpp
  ${REPO_ROOT}/src/lexirise/settings/Settings.cpp
  ${REPO_ROOT}/src/lexirise/text/BookSlug.cpp
  ${REPO_ROOT}/src/lexirise/net/Http.cpp
  ${REPO_ROOT}/src/lexirise/net/JsonReader.cpp
  ${REPO_ROOT}/src/lexirise/net/JsonWriter.cpp
  ${REPO_ROOT}/src/lexirise/api/Requests.cpp
  ${REPO_ROOT}/src/lexirise/api/Responses.cpp
)

target_compile_definitions(LexiriseDeckTest PRIVATE LEXIRISE=1)

target_include_directories(LexiriseDeckTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/test/lexirise_fakes
  ${REPO_ROOT}/test/lexirise_fakes/stubs
)

target_link_libraries(LexiriseDeckTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseDeckTest)
