# LEXIPOINT: FontCacheManager built as the firmware builds it with LEXIRISE (the card's scan limit).
add_executable(LexiriseFontScanTest
  FontScanTest.cpp
  ${REPO_ROOT}/lib/GfxRenderer/FontCacheManager.cpp
  ${REPO_ROOT}/lib/Utf8/Utf8.cpp
)

target_compile_definitions(LexiriseFontScanTest PRIVATE LEXIRISE=1)

target_include_directories(LexiriseFontScanTest PRIVATE
  ${REPO_ROOT}/test/font_cache_manager/stubs
  ${REPO_ROOT}/lib/GfxRenderer
  ${REPO_ROOT}/lib/Utf8
)

target_link_libraries(LexiriseFontScanTest PRIVATE
  crosspoint_test_common
  GTest::gtest_main
)

gtest_discover_tests(LexiriseFontScanTest)
