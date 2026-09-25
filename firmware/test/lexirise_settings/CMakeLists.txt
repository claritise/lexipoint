add_executable(LexiriseSettingsTest
  SettingsTest.cpp
  SettingsStoreTest.cpp
  SettingsPatchTest.cpp
  SettingsScreenTest.cpp
  BookLanguagesTest.cpp
  LongPressMenuTest.cpp
  ${REPO_ROOT}/src/lexirise/settings/Settings.cpp
  ${REPO_ROOT}/src/lexirise/settings/SettingsStore.cpp
  ${REPO_ROOT}/src/lexirise/settings/SafeFile.cpp
  ${REPO_ROOT}/src/lexirise/settings/BookLanguages.cpp
  ${REPO_ROOT}/src/lexirise/settings/SettingsPatch.cpp
  ${REPO_ROOT}/src/lexirise/settings/SettingsScreen.cpp
  ${REPO_ROOT}/src/lexirise/net/Http.cpp
)

target_compile_definitions(LexiriseSettingsTest PRIVATE LEXIRISE=1)

target_include_directories(LexiriseSettingsTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/test/lexirise_fakes
  ${REPO_ROOT}/test/lexirise_fakes/stubs
)

target_link_libraries(LexiriseSettingsTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseSettingsTest)
