#!/bin/sh
# Build the Verilator model of the full system (core + SDRAM controller + loader + scan-out) with the
# SDRAM chip model -> obj_sys/Vmc_system
# CART_CHIP_BITS=14 ./build_system.sh -> obj_sys14/Vmc_system, with 16 KB cartridge chips (tests the second chip)
set -e
cd "$(dirname "$0")"
VERILATOR=${VERILATOR:-verilator}
$VERILATOR --cc --exe --build -j 0 -O3 -Wno-fatal -Wno-lint -Wno-style \
  -MAKEFLAGS CFG_CXXFLAGS_PCH_I=-include -CFLAGS -O2 -Ihazard3 -Ihazard3/arith --top-module mc_system --Mdir obj_sys${CART_CHIP_BITS} ${CART_CHIP_BITS:+-GCART_CHIP_BITS=$CART_CHIP_BITS} \
  mister/mc_system.v mister/mc_cart.v mister/mc_sdram.v mister/mc_scanout.v \
  mc_core.v mc_ram.v mc_rom_cache.v mc_rom_arb.v mc_io.v mc_gpu.v mc_apu.v mc_vtiming.v \
  mc_util.v mc_layer.v mc_sprites.v mc_render.v \
  $(sed -n 's/^file /hazard3\//p' hazard3/hazard3.f) tb/tb_system.cpp
