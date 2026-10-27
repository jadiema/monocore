#!/bin/sh
# Cyclone V netlist of the synthesis trial with Yosys (YoWASP) -> $1 (JSON). Runs from a copy of the
# sources, because YoWASP only sees the current directory.
set -e
ROOT=$(cd "$(dirname "$0")/../.." && pwd)
OUT=$(realpath -m "$1")
W=$(mktemp -d /home/claude/ysta.XXXX)
cp -r "$ROOT/rtl" "$ROOT/tools/quartus_trial" "$W/"
rm -rf "$W/rtl/obj_dir" "$W/rtl/obj_apu"
cd "$W"
{
  echo "read_verilog -sv -Irtl/hazard3 -Irtl/hazard3/arith quartus_trial/monocore_trial_top.v"
  for f in mc_core mc_ram mc_rom_cache mc_rom_arb mc_io mc_gpu mc_apu mc_vtiming mc_util mc_layer mc_sprites mc_render; do
    echo "read_verilog -sv -Irtl/hazard3 -Irtl/hazard3/arith rtl/$f.v"
  done
  sed -n 's/^file /read_verilog -sv -Irtl\/hazard3 -Irtl\/hazard3\/arith rtl\/hazard3\//p' rtl/hazard3/hazard3.f
  # keep the register output names, so that the timing report can name the start and end of each path
  echo "hierarchy -top monocore_trial_top; proc; flatten"
  echo "select -set regq t:\$*dff* %co:+[Q] w:* %i; setattr -set keep 1 @regq; select -clear"
  echo "synth_intel_alm -family cyclonev -top monocore_trial_top"
  echo "write_json net.json"
} > s.ys
yowasp-yosys -q -l synth.log -s s.ys > /dev/null 2>&1 || { tail -20 synth.log; exit 1; }
cp net.json "$OUT"
cd /; rm -rf "$W"
