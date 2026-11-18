#!/bin/sh
# Builds and runs the MemoryDump / DebugExpression host test (MSYS2 g++)
cd "$(dirname "$0")" || exit 1
/c/msys64/mingw64/bin/g++ -std=c++17 -Wall -Wextra -I../Commands TestMemoryDump.cpp ../Commands/DebugExpression.cpp ../Commands/MemoryDump.cpp -o TestMemoryDump.exe || exit 1
./TestMemoryDump.exe
