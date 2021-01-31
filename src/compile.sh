#!/bin/bash
g++ -pthread -std=c++17 chaos.cpp -c -o chaos.o
gcc cacti.c -c -o cacti.o
g++ -pthread -std=c++17 chaos.o cacti.o -o chaos
