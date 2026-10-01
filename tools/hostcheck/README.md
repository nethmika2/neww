# Host side verification harness

The PlatformIO registry is unreachable from the build sandbox, so `pio run`
cannot be used to check the firmware.  This harness compiles **the real
firmware sources** against a small set of host stubs and then drives the parts
that do not need the panel: the Pomodoro store, the phase flow, the countdown
arithmetic, the on-screen keyboards, the four point touch calibration and the
earbud (AVRCP) handling.  It also records every draw call so the screens can be
reviewed as PNGs.

```sh
./check.sh        # compile only (fast, catches signature/type mistakes)
./build_host.sh   # build + run the test suite, writes shots/*.txt
python3 render.py # replay the shots into png/*.png
python3 render.py 09-pomo-tasks 11-pomo-presets out.png   # contact sheet
```

`build/`, `shots/` and `png/` are generated and not tracked.

Stubs live in `stubs/`: Arduino/FreeRTOS basics, Adafruit GFX + ILI9341 (which
record draw calls instead of pushing pixels), SPI/Wire/FS/SD (an in-memory card
with a WAV writer), Preferences, WiFi, ESP-IDF logging and the AVRCP target API
including a small model of the notification handshake.  The `Adafruit_GFX` stub
also owns the font objects, so nothing in `src/` has to be conditionally
compiled for the host.
