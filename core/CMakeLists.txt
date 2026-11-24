add_library(msxcore STATIC
  src/diagnostics.cpp
  src/io.cpp
  src/utf8.cpp
  src/charset.cpp
  src/profile.cpp
  src/bcd.cpp
  src/basic_token.cpp
  src/basic_lexer.cpp
  src/basic_compile.cpp
  src/basic_image.cpp
  src/basic_complete.cpp
  src/wav.cpp
  src/fsk.cpp
  src/cas.cpp
)

target_include_directories(msxcore PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

if(MSVC)
  target_compile_options(msxcore PRIVATE /W4 /permissive-)
  # The XP build is produced by the v141_xp toolset; keep the core free of
  # C++17-only library features so it also builds with that toolset.
else()
  target_compile_options(msxcore PRIVATE -Wall -Wextra -Wpedantic)
endif()
