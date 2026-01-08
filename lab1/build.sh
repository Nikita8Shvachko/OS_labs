#!/bin/bash

# Compile object files
echo "Compiling Daemon.cpp..."
g++ -std=c++17 -Wall -Werror -c Daemon.cpp -o Daemon.o

echo "Compiling main.cpp..."
g++ -std=c++17 -Wall -Werror -c main.cpp -o main.o

# Link object files
echo "Linking..."
g++ Daemon.o main.o -o daemon_lab1

# Clean up intermediate files
echo "Cleaning up..."
rm -f Daemon.o main.o

echo "Build complete. Executable: ./daemon_lab1"

