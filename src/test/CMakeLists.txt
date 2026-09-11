cmake_minimum_required (VERSION 4.3)
project (test)

include_directories(..)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-Wall -Wextra -pthread")
set(CMAKE_C_FLAGS_DEBUG "-pedantic -Werror -fstack-protector-strong -g")

add_executable(actor_system_test actor_system_test.c)
add_test(test_actor_system actor_system_test)

add_test(NAME test_factorial COMMAND ${CMAKE_SOURCE_DIR}/test/factorial/test_factorial.sh $<TARGET_FILE:factorial>)
add_test(NAME test_matrix COMMAND ${CMAKE_SOURCE_DIR}/test/matrix/test_matrix.sh $<TARGET_FILE:matrix>)

set_tests_properties(test_actor_system PROPERTIES TIMEOUT 1)
set_tests_properties(test_factorial PROPERTIES TIMEOUT 10)
# Test should take 29s the longest, would take 33s+ if execution was one
# threaded.
set_tests_properties(test_matrix PROPERTIES TIMEOUT 30)
