cmake_minimum_required(VERSION 3.25)
project(horned_rat VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

option(HR_CORE_ONLY "Build only the platform-independent core library and its tests" OFF)
option(HR_SANITIZE "Enable ASan + UBSan (GCC/Clang only)" OFF)

set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)

# Static CRT: the proxy DLL must not depend on a VC++ runtime being installed.
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

if(HR_SANITIZE AND NOT MSVC)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(hr_warnings INTERFACE)
if(MSVC)
  target_compile_options(hr_warnings INTERFACE /W4 /WX /permissive- /utf-8)
else()
  target_compile_options(hr_warnings INTERFACE -Wall -Wextra -Wpedantic -Werror)
endif()

include(FetchContent)
FetchContent_Declare(Catch2
  GIT_REPOSITORY https://github.com/catchorg/Catch2.git
  GIT_TAG v3.8.1
  SYSTEM)
FetchContent_MakeAvailable(Catch2)
include(CTest)
include(Catch)

add_subdirectory(src/core)
if(NOT HR_CORE_ONLY)
  if(NOT CMAKE_SIZEOF_VOID_P EQUAL 4)
    message(FATAL_ERROR "The Windows parts must be built for 32-bit x86: enter the dev shell with tools/vsenv.ps1 and use an x86-* or ci preset.")
  endif()
  add_subdirectory(src/platform)
  add_subdirectory(src/wing32)
  add_subdirectory(tools/luma_probe)
endif()
add_subdirectory(tests)
