cmake_minimum_required (VERSION 4.3)
project (test)

include_directories(..)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_FLAGS "-Wall -Wextra -pthread")
set(CMAKE_C_FLAGS_DEBUG "-pedantic -Werror -fstack-protector-strong -g")

# TODO
# add_executable(actor_system actorSystem_test.c)
# add_test(actor_system actor_system)
#
# add_executable(deadlock deadLock_test.c)
# add_test(deadlock deadlock)
#
# add_executable(chaos chaos.cpp)
# add_test(chaos chaos)

add_executable(test_empty test_empty.c)
add_test(test_empty test_empty)

set_tests_properties(test_empty PROPERTIES TIMEOUT 1)
