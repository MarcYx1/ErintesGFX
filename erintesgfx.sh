#!/bin/sh
# Start ErintesGFX inside the erintes-dev distrobox (Qt, ruby, SDL2 live there).
# Usage: ./erintesgfx.sh [path/to/Project.touchgfx]
exec distrobox enter erintes-dev -- "$(dirname "$(readlink -f "$0")")/build/erintesgfx" "$@"
