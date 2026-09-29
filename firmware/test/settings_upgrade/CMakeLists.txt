# LEXIPOINT (v0.2 V8): an old settings.json through the real CrossPointSettings::fromJson / toJson.
FetchContent_Declare(
  ArduinoJson
  GIT_REPOSITORY https://github.com/bblanchon/ArduinoJson.git
  GIT_TAG v7.4.2  # platformio.ini's bblanchon/ArduinoJson
)
FetchContent_MakeAvailable(ArduinoJson)

# The string tables, generated where the firmware build puts them (scripts/gen_i18n.py, a pre: script there).
file(GLOB I18N_YAML CONFIGURE_DEPENDS ${REPO_ROOT}/lib/I18n/translations/*.yaml)
execute_process(
  COMMAND python3 ${REPO_ROOT}/scripts/gen_i18n.py ${REPO_ROOT}/lib/I18n/translations ${REPO_ROOT}/lib/I18n/
  RESULT_VARIABLE I18N_RESULT
  OUTPUT_QUIET
)
if(NOT I18N_RESULT EQUAL 0)
  message(FATAL_ERROR "gen_i18n.py failed")
endif()

add_executable(SettingsUpgradeTest
  SettingsUpgradeTest.cpp
  Links.cpp
  ${REPO_ROOT}/src/CrossPointSettings.cpp
  ${REPO_ROOT}/src/ReaderFontSizes.cpp
  ${REPO_ROOT}/lib/I18n/I18n.cpp
  ${REPO_ROOT}/lib/I18n/I18nStrings.cpp
)

target_compile_definitions(SettingsUpgradeTest PRIVATE
  SETTINGS_FIXTURE="${CMAKE_CURRENT_SOURCE_DIR}/fixtures/settings-before-v8.json"
)

target_include_directories(SettingsUpgradeTest PRIVATE
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/stubs
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/Epub
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/I18n
  ${REPO_ROOT}/lib/Serialization
  ${REPO_ROOT}/test/lexirise_fakes/stubs
)

target_link_libraries(SettingsUpgradeTest PRIVATE
  crosspoint_test_common
  ArduinoJson
  GTest::gtest_main
)

gtest_discover_tests(SettingsUpgradeTest)
