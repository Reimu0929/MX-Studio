cmake_minimum_required(VERSION 3.16)
project(msxcf2000 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 14)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

if(MSVC)
  # Windows XP target: static CRT, Unicode sources, and the 5.01 subsystem.
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
  add_compile_options(/utf-8)
  add_compile_definitions(WINVER=0x0501 _WIN32_WINNT=0x0501 NOMINMAX)
endif()

option(MSX_BUILD_TESTS "Build the portable core tests" ON)
option(MSX_BUILD_TOOLS "Build the msxtool command line utility" ON)
option(MSX_BUILD_WIN32 "Build the Win32 GUI application (Windows only)" OFF)

add_subdirectory(core)

if(MSX_BUILD_TOOLS)
  add_subdirectory(tools/msxtool)
endif()

if(MSX_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

if(MSX_BUILD_WIN32)
  if(WIN32)
    add_subdirectory(app/win32)
  else()
    message(WARNING "MSX_BUILD_WIN32 requires a Windows toolchain; skipping app/win32")
  endif()
endif()
