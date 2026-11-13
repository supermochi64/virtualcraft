#!/usr/bin/env bash
# Build the Virtual Boy ROM: ./build.sh -> build/virtualcraft.vb
# Needs v810-gcc (GCC 4.9.4 for the NEC V810). Set V810 to its bin directory if it is not on the PATH.
# The generated data (font, sounds, textures, tables) ships in gen/; tools/assets.py and tools/textures.py remake it.
set -e
cd "$(dirname "$0")"
T=${V810:+$V810/}
CF="$DEFS -O3 -mv810 -fno-builtin -ffreestanding -fno-delete-null-pointer-checks -Wall -Isrc -Ibuild"
mkdir -p build
for f in assets.h recip.h sound_data.h tex.bin tex.h; do [ -f build/$f ] || cp gen/$f build/; done
${T}v810-gcc $CF -nostartfiles -nostdlib -T src/vb.ld -o build/virtualcraft.elf src/crt0.s src/main.c src/title.c src/render.c src/world.c src/game.c src/mobs.c src/hud.c src/sound.c src/pcm.S src/seg.s src/texdata.s src/rominfo.c -lgcc
${T}v810-objcopy -O binary build/virtualcraft.elf build/virtualcraft.vb
ls -la build/virtualcraft.vb
