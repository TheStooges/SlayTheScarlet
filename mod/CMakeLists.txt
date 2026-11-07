# The in-game DLL. Windows, MSVC, 32-bit only: game functions need __thiscall.
add_definitions(-DWINVER=0x0501 -D_WIN32_WINNT=0x0501)

add_subdirectory(${CMAKE_SOURCE_DIR}/lib/SokuLib ${CMAKE_BINARY_DIR}/SokuLib)

add_library(SlayTheScarlet MODULE main.cpp netlog.cpp screen.cpp)
set_target_properties(SlayTheScarlet PROPERTIES CXX_STANDARD 17)
target_compile_options(SlayTheScarlet PRIVATE /Zi)
target_compile_definitions(SlayTheScarlet PRIVATE
  DIRECTINPUT_VERSION=0x0800 _CRT_SECURE_NO_WARNINGS $<$<CONFIG:Debug>:_DEBUG>)
target_include_directories(SlayTheScarlet PRIVATE ${CMAKE_SOURCE_DIR}/lib/SokuLib/directx)
target_link_directories(SlayTheScarlet PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/lib)
target_link_libraries(SlayTheScarlet PRIVATE sts_core SokuLib shlwapi d3d9 d3dx9)

# The DLL loads these from its own folder.
add_custom_command(TARGET SlayTheScarlet POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E copy_if_different
          ${CMAKE_SOURCE_DIR}/data/cards.csv ${CMAKE_SOURCE_DIR}/data/settings.ini
          $<TARGET_FILE_DIR:SlayTheScarlet>)
