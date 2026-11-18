#!/bin/sh
# Build and run the host tests of OpcodeDecoder (68000 disassembler).
# Usage : sh HostTests/build_opcodedecoder.sh   (from any directory)
set -e
HERE="$(cd "$(dirname "$0")" && pwd)"
CXX="${CXX:-C:/msys64/mingw64/bin/g++}"
OUT="${TMPDIR:-/tmp}/test_opcodedecoder.exe"
cd "$HERE"
"$CXX" -std=c++17 -Wall -Wextra -I.. -o "$OUT" TestOpcodeDecoder.cpp ../OpcodeDecoder.cpp
set +e
"$OUT"
RC=$?
rm -f "$OUT"
exit $RC
