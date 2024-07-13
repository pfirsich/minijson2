cmake_minimum_required(VERSION 3.0.0)

project(minijson2)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_EXPORT_COMPILE_COMMANDS on)

add_library(minijson2 STATIC src/minijson2.cpp)
target_include_directories(minijson2 PUBLIC include/)
target_compile_options(minijson2 PRIVATE -Wall -Wextra -pedantic)

option(MINIJSON2_BUILD_TEST "Build the minijson2-test executable" OFF)

if(MINIJSON2_BUILD_TEST)
  add_executable(minijson2-test src/minijson2-test.cpp)
  target_link_libraries(minijson2-test minijson2)
  target_compile_options(minijson2-test PRIVATE -Wall -Wextra -pedantic)
endif()
