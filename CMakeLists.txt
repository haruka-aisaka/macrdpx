cmake_minimum_required(VERSION 3.16)
project(macrdpx VERSION 1.0.0 LANGUAGES C)

add_library(macrdpx STATIC src/codec.c src/keys.c)
target_include_directories(macrdpx PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(macrdpx PROPERTIES C_STANDARD 99 C_STANDARD_REQUIRED ON)
if(CMAKE_C_COMPILER_ID MATCHES "Clang|GNU")
  target_compile_options(macrdpx PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wno-sign-conversion)
endif()

# Tests only when built on its own, not as a consumer's subdirectory.
if(CMAKE_CURRENT_SOURCE_DIR STREQUAL CMAKE_SOURCE_DIR)
  include(CTest)
  if(BUILD_TESTING)
    add_executable(macrdpx_tests tests/macrdpx_tests.c)
    target_link_libraries(macrdpx_tests PRIVATE macrdpx)
    add_test(NAME macrdpx_tests COMMAND macrdpx_tests)
  endif()
endif()
