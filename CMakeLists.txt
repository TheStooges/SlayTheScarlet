cmake_minimum_required(VERSION 3.15)
project(SlayTheScarlet C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# The DLL and everything linked into it must use the same runtime as SokuLib.
if(MSVC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

# core/ and tests/ are portable C and build with any compiler (clang on macOS, MSVC on Windows).
add_subdirectory(core)

enable_testing()
add_subdirectory(tests)

# mod/ (the SokuLib DLL) is Windows + MSVC x86 only.
if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
  if(EXISTS ${CMAKE_SOURCE_DIR}/lib/SokuLib/CMakeLists.txt)
    enable_language(CXX)
    add_subdirectory(mod)
  else()
    message(WARNING "lib/SokuLib is missing, skipping mod/. Run: git submodule update --init")
  endif()
endif()
