add_executable(LexiriseNetTest
  JsonWriterTest.cpp
  HttpTest.cpp
  JsonReaderTest.cpp
  ResponsesTest.cpp
  LexiriseClientTest.cpp
  WifiLeaseTest.cpp
  KeyCheckTest.cpp
  RequestsTest.cpp
  HiddenPathTest.cpp
  OriginTest.cpp
  ${REPO_ROOT}/src/lexirise/net/JsonWriter.cpp
  ${REPO_ROOT}/src/lexirise/net/JsonReader.cpp
  ${REPO_ROOT}/src/lexirise/net/Http.cpp
  ${REPO_ROOT}/src/lexirise/api/Responses.cpp
  ${REPO_ROOT}/src/lexirise/api/LexiriseClient.cpp
  ${REPO_ROOT}/src/lexirise/api/KeyCheck.cpp
  ${REPO_ROOT}/src/lexirise/api/Requests.cpp
  ${REPO_ROOT}/src/lexirise/settings/Settings.cpp
)

target_compile_definitions(LexiriseNetTest PRIVATE LEXIRISE=1)

target_include_directories(LexiriseNetTest PRIVATE
  ${REPO_ROOT}/src
)

target_link_libraries(LexiriseNetTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseNetTest)
