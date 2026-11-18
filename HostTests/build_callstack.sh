#!/bin/sh
# Build and run the host tests of CallStack (call stack analyzer of the live debugger).
# Usage : sh HostTests/build_callstack.sh   (from any directory)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-C:/msys64/mingw64/bin/g++}"
OUT="${TMPDIR:-/tmp}/test_callstack.exe"
cd "$HERE"
"$CXX" -std=c++17 -Wall -Wextra -I../Commands -o "$OUT" TestCallStack.cpp ../Commands/CallStack.cpp
"$OUT"
