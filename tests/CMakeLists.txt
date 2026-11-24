add_executable(msxtests tests.cpp)
target_link_libraries(msxtests PRIVATE msxcore)

if(MSVC)
  target_compile_options(msxtests PRIVATE /W4)
  set_target_properties(msxtests PROPERTIES LINK_FLAGS "/SUBSYSTEM:CONSOLE,5.01")
else()
  target_compile_options(msxtests PRIVATE -Wall -Wextra)
endif()

add_test(NAME msxcore_tests COMMAND msxtests)
