add_executable(LexiriseCardTest
  CardLayoutTest.cpp
  CardControllerTest.cpp
  ${REPO_ROOT}/src/lexirise/card/CardController.cpp
  ${REPO_ROOT}/src/lexirise/card/CardInput.cpp
  ${REPO_ROOT}/src/lexirise/card/CardFrame.cpp
  ${REPO_ROOT}/src/lexirise/card/CardLayout.cpp
  ${REPO_ROOT}/src/lexirise/card/BenchFixtures.cpp
  ${REPO_ROOT}/src/lexirise/card/BenchPage.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_compile_definitions(LexiriseCardTest PRIVATE LEXIRISE=1)

target_include_directories(LexiriseCardTest PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(LexiriseCardTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseCardTest)

# Host tool for scripts/lexipoint/cardshots.py (not a test): one card state as a JSON display list.
add_executable(LexiriseCardRender
  CardRender.cpp
  ${REPO_ROOT}/src/lexirise/card/CardLayout.cpp
  ${REPO_ROOT}/src/lexirise/card/CardController.cpp
  ${REPO_ROOT}/src/lexirise/card/CardFrame.cpp
  ${REPO_ROOT}/src/lexirise/card/BenchFixtures.cpp
  ${REPO_ROOT}/src/lexirise/card/BenchPage.cpp
  ${REPO_ROOT}/lib/EpdFont/EpdFont.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)
target_compile_definitions(LexiriseCardRender PRIVATE LEXIRISE=1)
target_include_directories(LexiriseCardRender PRIVATE
  ${REPO_ROOT}/src
  ${REPO_ROOT}/lib/EpdFont
  ${REPO_ROOT}/lib/Utf8
)
target_link_libraries(LexiriseCardRender PRIVATE crosspoint_test_common)

# The approved layout, pinned: every reference state's display list against test/lexirise_card/golden.
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
  add_test(NAME LexiriseCardGolden
    COMMAND ${Python3_EXECUTABLE} ${REPO_ROOT}/scripts/lexipoint/cardgolden.py --tool $<TARGET_FILE:LexiriseCardRender>)
endif()
