#!/bin/sh
# Build the Verilator model of the MONOCORE-1 core with the C++ testbench -> obj_dir/Vmc_core
set -e
cd "$(dirname "$0")"
VERILATOR=${VERILATOR:-verilator}
$VERILATOR --cc --exe --build -j 0 -O3 -Wno-fatal -Wno-lint -Wno-style \
  -MAKEFLAGS CFG_CXXFLAGS_PCH_I=-include -CFLAGS -O2 -Ihazard3 -Ihazard3/arith --top-module mc_core --Mdir obj_dir \
  mc_core.v mc_ram.v mc_rom_cache.v mc_rom_arb.v mc_io.v mc_gpu.v mc_apu.v mc_vtiming.v \
  mc_util.v mc_layer.v mc_sprites.v mc_render.v \
  $(sed -n 's/^file /hazard3\//p' hazard3/hazard3.f) tb/tb_main.cpp
