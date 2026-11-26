# The in-game DLL. Windows, MSVC, 32-bit only: game functions need __thiscall.
add_definitions(-DWINVER=0x0501 -D_WIN32_WINNT=0x0501)
# C4819 ("character that cannot be represented in the current code page") comes only from comments in
# SokuLib, whose Japanese text was already lost to U+FFFD replacement characters upstream. Nothing
# compiled is affected, and on a Japanese-locale Windows (code page 932) it fills the build output.
# Set before SokuLib is added, so it covers SokuLib's own sources and the headers the mod includes.
add_compile_options(/wd4819)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/SokuLib ${CMAKE_BINARY_DIR}/SokuLib)

# cards.csv, rarity.csv, the translations, and the card sounds are built into the DLL as resources.
set(STS_DATA_DIR ${CMAKE_SOURCE_DIR}/data)
configure_file(embedded.rc.in ${CMAKE_CURRENT_BINARY_DIR}/embedded.rc @ONLY)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/embedded.rc PROPERTIES
  OBJECT_DEPENDS "${STS_DATA_DIR}/cards.csv;${STS_DATA_DIR}/rarity.csv;${STS_DATA_DIR}/lang/ja.txt;${STS_DATA_DIR}/card_sounds.csv")

# The top of mod/ has the entry points and what every part shares (logging, text, card sounds);
# run/ keeps the run deck in the game, screens/ draws and drives the mod's screens, and net/ is
# everything online.
add_library(SlayTheScarlet MODULE
  ${CMAKE_CURRENT_BINARY_DIR}/embedded.rc
  main.cpp
  card_sounds.cpp
  card_text.cpp
  embedded.cpp
  log.cpp
  text.cpp
  run/match.cpp
  run/run_data.cpp
  run/run_deck.cpp
  screens/gallery.cpp
  screens/history.cpp
  screens/offer_effects.cpp
  screens/screen.cpp
  screens/screen_draw.cpp
  screens/settings_screen.cpp
  net/netlog.cpp
  net/peers.cpp
  net/spectator.cpp)
set_target_properties(SlayTheScarlet PROPERTIES CXX_STANDARD 17)
target_compile_options(SlayTheScarlet PRIVATE /Zi)
# Release keeps a PDB for reading crash dumps. PDBALTPATH keeps the build folder, and so the
# developer's Windows user name, out of the DLL.
target_link_options(SlayTheScarlet PRIVATE
  $<$<CONFIG:Release>:/DEBUG>
  $<$<CONFIG:Release>:/OPT:REF>
  $<$<CONFIG:Release>:/OPT:ICF>
  $<$<CONFIG:Release>:/PDBALTPATH:SlayTheScarlet.pdb>)
target_compile_definitions(SlayTheScarlet PRIVATE
  DIRECTINPUT_VERSION=0x0800 _CRT_SECURE_NO_WARNINGS $<$<CONFIG:Debug>:_DEBUG>)
# Headers are included by name alone, wherever they are in mod/.
target_include_directories(SlayTheScarlet PRIVATE ${CMAKE_SOURCE_DIR}/lib/SokuLib/directx
  ${CMAKE_CURRENT_SOURCE_DIR} ${CMAKE_CURRENT_SOURCE_DIR}/run ${CMAKE_CURRENT_SOURCE_DIR}/screens
  ${CMAKE_CURRENT_SOURCE_DIR}/net)
target_link_directories(SlayTheScarlet PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_libraries(SlayTheScarlet PRIVATE sts_core SokuLib shlwapi d3d9 d3dx9)

# The DLL reads settings.ini from its own folder.
add_custom_command(TARGET SlayTheScarlet POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/data/settings.ini $<TARGET_FILE_DIR:SlayTheScarlet>)
