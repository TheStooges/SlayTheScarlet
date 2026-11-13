add_executable(sts_tests
  test_main.c
  test_util.c
  test_deck.c
  test_lang.c
  test_offer.c
  test_pf.c
  test_pool.c
  test_rng.c
  test_run.c
  test_settings.c
  test_sounds.c
  test_state.c)
target_link_libraries(sts_tests PRIVATE sts_core)
# The mod's sources, so the tests can check every tr("...") has a translation. The list is taken
# when CMake configures, so a new file in mod/ needs CMake to run again.
file(GLOB STS_MOD_SOURCE_LIST ${CMAKE_SOURCE_DIR}/mod/*.cpp)
list(JOIN STS_MOD_SOURCE_LIST "|" STS_MOD_SOURCE_PATHS)
target_compile_definitions(sts_tests PRIVATE
  STS_CARDS_PATH="${CMAKE_SOURCE_DIR}/data/cards.csv"
  STS_SETTINGS_PATH="${CMAKE_SOURCE_DIR}/data/settings.ini"
  STS_CARD_SOUNDS_PATH="${CMAKE_SOURCE_DIR}/data/card_sounds.csv"
  STS_LANG_JA_PATH="${CMAKE_SOURCE_DIR}/data/lang/ja.txt"
  STS_MOD_SOURCES="${STS_MOD_SOURCE_PATHS}")
if(MSVC)
  target_compile_definitions(sts_tests PRIVATE _CRT_SECURE_NO_WARNINGS)
  target_compile_options(sts_tests PRIVATE /W4)
else()
  target_compile_options(sts_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()
add_test(NAME core COMMAND sts_tests)
