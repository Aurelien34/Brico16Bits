#!/bin/sh
# Build and run the host tests of GetBranchTarget (branch target of the live debugger disassembly).
# Usage : sh HostTests/build_branchtarget.sh   (from any directory)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-C:/msys64/mingw64/bin/g++}"
OUT="${TMPDIR:-/tmp}/test_branchtarget.exe"
cd "$HERE"
"$CXX" -std=c++17 -Wall -Wextra -I../Commands -o "$OUT" TestBranchTarget.cpp
"$OUT"
