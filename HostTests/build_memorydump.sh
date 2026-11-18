#!/bin/sh
# Builds and runs the MemoryDump / DebugExpression host test (MSYS2 g++)
# The executable goes to the temp directory, never into the source tree
cd "$(dirname "$0")" || exit 1
EXE="${TMPDIR:-/tmp}/TestMemoryDump.exe"
/c/msys64/mingw64/bin/g++ -std=c++17 -Wall -Wextra -I../Commands TestMemoryDump.cpp ../Commands/DebugExpression.cpp ../Commands/MemoryDump.cpp -o "$EXE" || exit 1
"$EXE"
RESULT=$?
rm -f "$EXE"
exit $RESULT
