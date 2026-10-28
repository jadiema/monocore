#!/bin/sh
# Full RTL regression against the simulator: CPU self-test, test ROM screens (pixel exact),
# random video ROM (pixel exact), sound engine (bit exact).
# usage: tools/regress.sh [workdir]   (FUZZ_LAST=21 for a shorter run)
cd "$(dirname "$0")/.."
W=${1:-/tmp/claude-0/regress}
rm -rf $W; mkdir -p $W/js $W/rtl
echo "== CPU"
rtl/obj_dir/Vmc_core tools/isatest/isatest.mnc $W/isa "8:shot=x" 2>&1 | grep debug
echo "== ROM de test"
SCRIPT="60:shot=menu 70:p1=10 71:p1=0 100:shot=card320 101:p1=10 102:p1=0 130:shot=card256 131:p1=10 132:p1=0 160:shot=card384 161:p1=10 162:p1=0 190:shot=card224 191:p1=20 192:p1=0 194:p1=2 195:p1=0 196:p1=10 197:p1=0 260:shot=planes 261:p1=10 262:p1=0 263:p1=40 264:p1=0 300:shot=planes2 301:p1=20 302:p1=0 303:p1=2 304:p1=0 305:p1=10 306:p1=0 340:shot=sprites"
for i in $(seq 341 2 387); do SCRIPT="$SCRIPT $i:p1=10 $((i+1)):p1=0"; done
SCRIPT="$SCRIPT 389:p1=19 420:shot=sprites2 421:p1=0 500:shot=sprites3"
node tools/run.js rom/build/testrom.mnc $W/js "$SCRIPT" > /dev/null
rtl/obj_dir/Vmc_core rom/build/testrom.mnc $W/rtl "$SCRIPT" | head -1
for n in menu card320 card256 card384 card224 planes planes2 sprites sprites2 sprites3; do
	python3 tools/cmp_ppm.py --cpu-meter $W/js/$n.ppm $W/rtl/$n.ppm $W/d_$n.png
done
echo "== ROM video aleatoire"
tools/vidfuzz/run_cmp.sh tools/vidfuzz/vidfuzz.mnc $W/fuzz ${FUZZ_LAST:-61} > $W/fuzz.txt 2>&1
echo "images identiques : $(grep -c ' 0 pixels' $W/fuzz.txt) sur $(grep -c 'pixels' $W/fuzz.txt)"
grep -v " 0 pixels" $W/fuzz.txt
echo "fenetre de rendu max (image, cycles) : $(sort -k2 -n $W/fuzz/rtl/windows.txt | tail -1)"
echo "== ROM video pire cas (fenetre a comparer a 2 668 cycles, ligne la plus courte)"
tools/vidfuzz/run_cmp.sh tools/vidfuzz/vfs.mnc $W/fuzzs 21 > $W/fuzzs.txt 2>&1
echo "images identiques : $(grep -c ' 0 pixels' $W/fuzzs.txt) sur $(grep -c 'pixels' $W/fuzzs.txt)"
grep -v " 0 pixels" $W/fuzzs.txt
echo "fenetre de rendu max (image, cycles) : $(sort -k2 -n $W/fuzzs/rtl/windows.txt | tail -1)"
echo "== ROM video aleatoire haute resolution et entrelacee (DIP 512)"
tools/vidfuzz/run_cmp.sh tools/vidfuzz/vidfuzz_hr.mnc $W/fuzzh ${FUZZ_HR_LAST:-161} > $W/fuzzh.txt 2>&1
echo "images identiques : $(grep -c ' 0 pixels' $W/fuzzh.txt) sur $(grep -c 'pixels' $W/fuzzh.txt)"
grep -v " 0 pixels" $W/fuzzh.txt
echo "fenetre de rendu max (image, cycles) : $(sort -k2 -n $W/fuzzh/rtl/windows.txt | tail -1)"
echo "== ROM video pire cas haute resolution (DIP 768 ; 2 832 cycles par ligne en 640 x 270)"
tools/vidfuzz/run_cmp.sh tools/vidfuzz/vfs_hr.mnc $W/fuzzhs ${FUZZ_HR_LAST:-161} > $W/fuzzhs.txt 2>&1
echo "images identiques : $(grep -c ' 0 pixels' $W/fuzzhs.txt) sur $(grep -c 'pixels' $W/fuzzhs.txt)"
grep -v " 0 pixels" $W/fuzzhs.txt
echo "fenetre de rendu max (image, cycles) : $(sort -k2 -n $W/fuzzhs/rtl/windows.txt | tail -1)"
echo "== Sauvegardes d'etat du simulateur (sauver, continuer, recharger ailleurs : memes images et meme son)"
for r in rom/build/testrom.mnc tools/vidfuzz/vidfuzz_hr.mnc examples/monotracker/build/monotracker.mnc examples/graviton/build/graviton.mnc; do
  node tools/state_test.js $r 300 200
done
echo "== Son"
# register write logs (tools/apu/logs: sessions of the test ROM and random writes), replayed by the
# simulator's sound engine and by the RTL one; PCM data is read from the current test ROM
A=tools/apu/logs
for n in r1 r2 r3 w_adpcm w_s16; do
	node tools/apu/apu_ref.js rom/build/testrom.mnc $A/$n.log $W/${n}_js.raw > /dev/null
	rtl/obj_apu/Vmc_apu rom/build/testrom.mnc $A/$n.log $W/$n.raw | sed 's/^/  /'
	python3 tools/apu/cmp_raw.py $W/${n}_js.raw $W/$n.raw $A/$n.log | head -1
done
