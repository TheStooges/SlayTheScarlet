cmake_minimum_required(VERSION 3.15)
project(SlayTheScarlet C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# core/ and tests/ are portable C and build with any compiler (clang on macOS, MSVC on Windows).
add_subdirectory(core)

enable_testing()
add_subdirectory(tests)

# mod/ (the SokuLib DLL) is Windows + MSVC x86 only. It will be added later:
# if(MSVC AND CMAKE_SIZEOF_VOID_P EQUAL 4)
#   add_subdirectory(mod)
# endif()
