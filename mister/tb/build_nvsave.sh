#!/bin/sh
# Verilator test of mister/mc_nvsave.sv with the NVRAM (rtl/mc_ram.v) and a model of hps_io's SD
# block access: an empty save clears the NVRAM, autosave writes it, a reload brings it back.
set -e
cd "$(dirname "$0")"
verilator --cc --exe --build -j 0 -O2 -Wno-fatal -Wno-lint -Wno-style --top-module nvtop --Mdir obj_nvsave \
  nvtop.sv ../mc_nvsave.sv ../../rtl/mc_ram.v tb_nvsave.cpp
./obj_nvsave/Vnvtop
