add_executable(msxstudio WIN32
  main.cpp
  audio.cpp
  meter.cpp
  win_util.cpp
  app.rc
)

target_link_libraries(msxstudio PRIVATE msxcore comctl32 winmm gdi32 user32 shell32 comdlg32 ole32 imm32 advapi32)

target_compile_definitions(msxstudio PRIVATE
  UNICODE
  _UNICODE
  WIN32_LEAN_AND_MEAN
  NOMINMAX
)

if(MSVC)
  target_compile_options(msxstudio PRIVATE /W4)
  set_target_properties(msxstudio PROPERTIES LINK_FLAGS "/SUBSYSTEM:WINDOWS,5.01")
else()
  target_compile_options(msxstudio PRIVATE -Wall -Wextra)
  target_link_options(msxstudio PRIVATE -mwindows -static-libgcc -static-libstdc++)
endif()
