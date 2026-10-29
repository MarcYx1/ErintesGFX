# ErintesGFX

Linux editor for TouchGFX projects (tested with TouchGFX 4.26.1, ART12_DASH). Opens a `.touchgfx`
file, edits screens, writes `generated/gui_generated` byte-identical to TouchGFX Designer, and runs
the project's own Makefiles for the simulator and `target.hex`.

Not affiliated with or endorsed by STMicroelectronics. TouchGFX is a trademark of STMicroelectronics.
ErintesGFX contains no TouchGFX code: the framework and its tools come from your own TouchGFX project.

## Setup (Bazzite, once)

```sh
distrobox create -Y -n erintes-dev -i docker.io/library/ubuntu:24.04
distrobox enter erintes-dev -- sudo apt-get install -y build-essential cmake ninja-build \
  qt6-base-dev libgl1-mesa-dev nlohmann-json3-dev ruby ruby-nokogiri libsdl2-dev libsdl2-image-dev
```

Arm GCC: unpack `arm-gnu-toolchain-*-x86_64-arm-none-eabi.tar.xz` from developer.arm.com into `~/opt`.
The newest `~/opt/arm-gnu-toolchain-*` is put on PATH for builds.

Build:

```sh
distrobox enter erintes-dev -- sh -c 'export PATH=/usr/bin:/bin CC=/usr/bin/gcc CXX=/usr/bin/g++; \
  cmake -S . -B build -G Ninja && cmake --build build'
```

Run: `./erintesgfx.sh path/to/TouchGFX/MyApplication.touchgfx` (opens the last project without an argument).
CLI code generation only: `build/erintesgfx --generate MyApplication.touchgfx`.

## Keys

| Key | Action |
| --- | --- |
| F4 | Save + generate code |
| F5 | Generate + build and run simulator |
| F6 | Generate + build target.hex |
| Ctrl+S / Ctrl+Z / Ctrl+Shift+Z | Save / undo / redo |
| Ctrl+D, Del | Duplicate, delete widget |
| Ctrl+PgUp / Ctrl+PgDown | Move widget forward / backward |
| Arrows (Shift: 10 px) | Nudge widget |
| Ctrl+wheel, Ctrl+0 | Zoom, 100 % |

## Fixes applied for projects made on Windows

The project files are not changed; builds get these on the make command line:

- `imageconvert.out` / `fontconvert.out` lose the exec bit in git: set again before each build.
- Makefile paths with wrong case (`TouchGFX/Target` vs `TouchGFX/target`): passed as overrides.
- Ruby 3.2 removed `File.exists?` used by textconvert: `tools/ruby_compat.rb` through `RUBYOPT`.
- Simulator: `-Werror` trips on GCC 13 warnings in ST's HAL, and ST's `libtouchgfx.a` is not PIC:
  `WARN=...` and `linker_options=... -no-pie`.

## Not supported yet

Custom containers, text areas with two wildcards, interactions other than hardware button -> go to
screen (they are kept in the file but not generated), widgets other than Box, Image, TextArea, Line,
Shape, BoxProgress, LineProgress, Container (drawn as a placeholder, skipped by the generator with a
warning), editing shape points, flashing (no STM32_Programmer_CLI here).

## License

GPL-3.0, see `LICENSE`.

Output exception: files ErintesGFX writes into your project (everything under `generated/` and the
View/Presenter files it creates under `gui/`) are yours and are not covered by the GPL. You may use,
change and ship them under any license, including in closed firmware linked with TouchGFX.
