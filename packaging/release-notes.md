Linux editor for TouchGFX projects. Not affiliated with STMicroelectronics.

## Run

```sh
chmod +x ErintesGFX-*-x86_64.AppImage
./ErintesGFX-*-x86_64.AppImage path/to/TouchGFX/MyApplication.touchgfx
```

Built on Ubuntu 24.04 (glibc 2.39): runs on Ubuntu 24.04+, Linux Mint 22+, current Fedora / Bazzite.

## Needed for F5 / F6 (simulator, target.hex)

The AppImage holds the editor. Builds use your TouchGFX project's own Makefiles, so these must be on the machine:

- `make`, `ruby`, `ruby-nokogiri` (Ubuntu/Mint: `sudo apt install make ruby ruby-nokogiri`)
- Arm GNU Toolchain unpacked in `~/opt/arm-gnu-toolchain-*` (developer.arm.com)
- Simulator only: `g++`, `libsdl2-dev`, `libsdl2-image-dev`

On Bazzite run it inside a distrobox that has these (see README).

## What works

Open, edit and save `.touchgfx` projects (Box, Image, TextArea, Line, Shape, BoxProgress, LineProgress,
Container, hardware-button screen interactions); code generation byte-identical to TouchGFX Designer
4.26.1; simulator and target.hex builds with Linux fixes for projects made on Windows.
See README for what is not supported yet.
