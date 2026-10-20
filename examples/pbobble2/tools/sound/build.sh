#!/bin/sh
# Builds the offline sound renderer in build/snd/ (fetches ymfm and the Z80 core, never committed).
set -e
cd "$(dirname "$0")/../.." && mkdir -p build/snd && cd build/snd
[ -d ymfm ] || git clone -q --depth 1 https://github.com/aaronsgiles/ymfm.git
[ -d z80 ] || git clone -q --depth 1 https://github.com/superzazu/z80.git
# the Neo Geo selects its sound ROM banks with the high byte of the I/O address: record it
sed -e 's/^\(.*\)\*r = z->port_in(z, z->c);/\1z80_io_high = z->b; *r = z->port_in(z, z->c);/' \
    -e 's/^\(.*\)uint8_t val = z->port_in(z, z->c);/\1z80_io_high = z->b; uint8_t val = z->port_in(z, z->c);/' \
    -e 's/^\(.*\)z->a = z->port_in(z, port);/\1z80_io_high = z->a; z->a = z->port_in(z, port);/' z80/z80.c > z80io.tmp
{ printf '#include <stdint.h>\nuint8_t z80_io_high;\n'; cat z80io.tmp; } > z80io.c
grep -c z80_io_high z80io.c
gcc -O2 -c -Iz80 z80io.c -o z80io.o
g++ -O2 -std=c++14 -Iymfm/src -Iz80 -o render ../../tools/sound/render.cpp ymfm/src/ymfm_opn.cpp ymfm/src/ymfm_adpcm.cpp ymfm/src/ymfm_ssg.cpp ymfm/src/ymfm_misc.cpp z80io.o
echo build/snd/render ready
