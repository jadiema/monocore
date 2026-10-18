#!/bin/sh
# Build the stand-alone sound engine testbench -> obj_apu/Vmc_apu
set -e
cd "$(dirname "$0")"
VERILATOR=${VERILATOR:-verilator}
$VERILATOR --cc --exe --build -j 0 -O3 -Wno-fatal -Wno-lint -Wno-style \
  -MAKEFLAGS CFG_CXXFLAGS_PCH_I=-include -CFLAGS -O2 --top-module mc_apu --Mdir obj_apu \
  mc_apu.v mc_ram.v tb/tb_apu.cpp
