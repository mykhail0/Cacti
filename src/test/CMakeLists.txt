cmake_minimum_required (VERSION 3.1)
project (test)

#set(CMAKE_C_STANDARD ...)
set(CMAKE_C_FLAGS "-g -Wall -Wextra -pthread")

add_executable(actor_system actorSystem_test.c)
add_executable(deadlock deadLock_test.c)
#add_executable(chaos chaos.cpp)
