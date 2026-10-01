#!/bin/bash
# Builds and runs the host test binary: firmware sources + stubs, with the
# tests in host_main.cpp.  This is the verification path that replaces a real
# `pio run` (the PlatformIO registry is unreachable from the sandbox).
set -e
cd "$(dirname "$0")"
# Firmware root: walk up from this script until the PlatformIO project is found,
# so the harness works both inside tools/ and next to a checkout of the project.
FW=""
dir="$(pwd)"
while [ "$dir" != "/" ]; do
  if [ -f "$dir/platformio.ini" ]; then FW="$dir"; break; fi
  dir="$(dirname "$dir")"
done
if [ -z "$FW" ]; then FW="$(cd ../neww 2>/dev/null && pwd)"; fi
if [ -z "$FW" ]; then echo "cannot locate the firmware sources"; exit 1; fi
mkdir -p build
FLAGS="-std=gnu++17 -O0 -g -I stubs -I stubs/Fonts -I $FW/include -I $FW/src -I . -w -DHOSTCHECK"
SRC=$(ls $FW/src/*.cpp | grep -v tinyexpr)
g++ $FLAGS $SRC stubs/*.cpp host_main.cpp build/tinyexpr_c.o -o build/host -lm
./build/host
