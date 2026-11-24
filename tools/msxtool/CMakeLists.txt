add_executable(msxtool main.cpp)
target_link_libraries(msxtool PRIVATE msxcore)

if(MSVC)
  target_compile_options(msxtool PRIVATE /W4)
  set_target_properties(msxtool PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE,5.01")
else()
  target_compile_options(msxtool PRIVATE -Wall -Wextra)
endif()

if(MINGW)
  # Keep the Windows distribution self-contained (no libstdc++/libgcc DLLs).
  target_link_options(msxtool PRIVATE -static-libgcc -static-libstdc++)
endif()
