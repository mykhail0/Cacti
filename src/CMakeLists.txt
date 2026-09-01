cmake_minimum_required (VERSION 4.3)
project (CACTI)

enable_testing()

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-Wall -Wextra -pthread")
set(CMAKE_C_FLAGS_RELEASE "-O3 -DNDEBUG")
set(CMAKE_C_FLAGS_DEBUG "-pedantic -Werror -fstack-protector-strong -g")

# http://stackoverflow.com/questions/10555706/
macro (add_executable _name)
  # invoke built-in add_executable
  _add_executable(${ARGV})
  if (TARGET ${_name})
    target_link_libraries(${_name} cacti)
  endif()
endmacro()

add_library(cacti STATIC cacti.c err.c)
add_executable(matrix matrix.c)
add_executable(factorial factorial.c)
add_subdirectory(test)

install(TARGETS cacti DESTINATION .)
