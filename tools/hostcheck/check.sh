#!/bin/bash
# Static check: compile every firmware source plus the host stubs.  This is the
# stand-in for `pio run` (the PlatformIO registry is unreachable from the
# sandbox) and catches type, signature and header mistakes.
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
FLAGS="-std=gnu++17 -O0 -g -I stubs -I stubs/Fonts -I $FW/include -I $FW/src -I . -w -DHOSTCHECK"
mkdir -p build
fail=0
for f in $FW/src/*.cpp; do
  obj="build/$(basename "${f%.cpp}").o"
  g++ $FLAGS -c "$f" -o "$obj" || { echo "FAILED: $f"; fail=1; }
done
gcc -std=gnu11 -O0 -I $FW/src -w -c $FW/src/tinyexpr.c -o build/tinyexpr_c.o || { echo "FAILED: tinyexpr.c"; fail=1; }
for f in stubs/*.cpp; do
  obj="build/$(basename "${f%.cpp}").o"
  g++ $FLAGS -c "$f" -o "$obj" || { echo "FAILED: $f"; fail=1; }
done
# The HTML converter is part of the pipeline too: its romaniser has its own
# checks so Sinhala notes cannot silently come out as garbage.
if [ -f "$FW/tools/mkstudy.py" ]; then
  python3 "$FW/tools/mkstudy.py" --selftest || fail=1
fi
[ $fail -eq 0 ] && echo "CHECK PASSED" || { echo "CHECK FAILED"; exit 1; }
