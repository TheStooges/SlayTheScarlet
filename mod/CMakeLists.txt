# The in-game DLL. Windows, MSVC, 32-bit only: game functions need __thiscall.
add_definitions(-DWINVER=0x0501 -D_WIN32_WINNT=0x0501)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/SokuLib ${CMAKE_BINARY_DIR}/SokuLib)

# cards.csv and the translations are built into the DLL as resources.
set(STS_DATA_DIR ${CMAKE_SOURCE_DIR}/data)
configure_file(embedded.rc.in ${CMAKE_CURRENT_BINARY_DIR}/embedded.rc @ONLY)
set_source_files_properties(${CMAKE_CURRENT_BINARY_DIR}/embedded.rc PROPERTIES
  OBJECT_DEPENDS "${STS_DATA_DIR}/cards.csv;${STS_DATA_DIR}/lang/ja.txt")

add_library(SlayTheScarlet MODULE
  ${CMAKE_CURRENT_BINARY_DIR}/embedded.rc
  main.cpp
  card_text.cpp
  embedded.cpp
  history.cpp
  log.cpp
  match.cpp
  netlog.cpp
  run_data.cpp
  run_deck.cpp
  screen.cpp
  screen_draw.cpp
  settings_screen.cpp
  text.cpp)
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
target_include_directories(SlayTheScarlet PRIVATE ${CMAKE_SOURCE_DIR}/lib/SokuLib/directx)
target_link_directories(SlayTheScarlet PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_libraries(SlayTheScarlet PRIVATE sts_core SokuLib shlwapi d3d9 d3dx9)

# The DLL reads settings.ini from its own folder.
add_custom_command(TARGET SlayTheScarlet POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/data/settings.ini $<TARGET_FILE_DIR:SlayTheScarlet>)
