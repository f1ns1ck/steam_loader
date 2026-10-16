#!/bin/sh
# Account Panel for Linux without CMake: the library and the command line (g++ 9+ or clang++ 10+, C++20).
#   out/libAccountPanelCore.a   the library (core/*.hpp)
#   out/libAccountPanelCore.so  the same, shared
#   out/account-panel           the command line (./out/account-panel --help)
# Usage: ./build.sh [out dir]     CXX=clang++ ./build.sh
set -e

DIR=$(cd "$(dirname "$0")" && pwd)
OUT=${1:-"$DIR/out"}
CXX=${CXX:-g++}
FLAGS="-std=c++20 -O2 -Wall -Wextra -fPIC"

mkdir -p "$OUT/core"

OBJECTS=""
for NAME in Platform Steam SoCache Launch Accounts Cli; do
	"$CXX" $FLAGS -c "$DIR/core/$NAME.cpp" -o "$OUT/core/$NAME.o"
	OBJECTS="$OBJECTS $OUT/core/$NAME.o"
done

rm -f "$OUT/libAccountPanelCore.a"
ar rcs "$OUT/libAccountPanelCore.a" $OBJECTS
"$CXX" -shared -o "$OUT/libAccountPanelCore.so" $OBJECTS
"$CXX" $FLAGS "$DIR/cli/main.cpp" "$OUT/libAccountPanelCore.a" -o "$OUT/account-panel"

echo "$OUT/account-panel"
