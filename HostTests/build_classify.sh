#!/bin/sh
# Build and run the host tests of ClassifyInstruction (control flow category used by step over / call stack).
# Usage : sh HostTests/build_classify.sh   (from any directory)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-C:/msys64/mingw64/bin/g++}"
OUT="${TMPDIR:-/tmp}/test_classify.exe"
cd "$HERE"
"$CXX" -std=c++17 -Wall -Wextra -I../Commands -o "$OUT" TestClassifyInstruction.cpp
"$OUT"
