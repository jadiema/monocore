#!/bin/sh
# run_cmp.sh rom.mnc outdir last_frame : run a ROM on the simulator and the RTL, compare every other frame
ROM=$1; OUT=$2; LAST=${3:-13}
S=""; f=3; while [ $f -le $LAST ]; do S="$S $f:shot=v$f"; f=$((f+2)); done
rm -rf $OUT; mkdir -p $OUT/js $OUT/rtl
node tools/run.js $ROM $OUT/js "$S" > $OUT/js.log
rtl/obj_dir/Vmc_core $ROM $OUT/rtl "$S" > $OUT/rtl.log 2>&1
f=3; while [ $f -le $LAST ]; do python3 tools/cmp_ppm.py $OUT/js/v$f.ppm $OUT/rtl/v$f.ppm $OUT/d$f.png; f=$((f+2)); done
