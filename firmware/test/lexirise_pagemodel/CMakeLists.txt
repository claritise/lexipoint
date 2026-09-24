add_executable(LexirisePageModelTest
  PageModelAdapterTest.cpp
  LinkStubs.cpp
  ${REPO_ROOT}/src/lexirise/text/PageModelAdapter.cpp
  ${REPO_ROOT}/src/lexirise/text/SentenceBuilder.cpp
  ${REPO_ROOT}/src/lexirise/text/Punctuation.cpp
  ${REPO_ROOT}/lib/Epub/Epub/Page.cpp
  ${REPO_ROOT}/lib/Epub/Epub/blocks/TextBlock.cpp
  ${REPO_ROOT}/lib/MiniBidi/BidiUtils.cpp
  ${REPO_ROOT}/lib/MiniBidi/minibidi.c
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_compile_definitions(LexirisePageModelTest PRIVATE LEXIRISE=1)

# Page/TextBlock build on the host with the parser suite's Arduino/renderer stubs.
target_include_directories(LexirisePageModelTest PRIVATE
  ${REPO_ROOT}/test/chapter_html_slim_parser/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/Epub/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
  ${REPO_ROOT}/lib/Memory
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/lib/MiniBidi
  ${REPO_ROOT}/lib/FsHelpers
)

target_link_libraries(LexirisePageModelTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexirisePageModelTest)
