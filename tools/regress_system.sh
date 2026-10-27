#!/bin/sh
# Full-system regression (core + SDRAM controller and chip model + loader + scan-out) against the
# simulator: test ROM screens, random video ROM, worst case. Pictures come from the video output.
# usage: tools/regress_system.sh [workdir]
cd "$(dirname "$0")/.."
W=${1:-/tmp/claude-0/regsys}
rm -rf $W; mkdir -p $W/js $W/sys $W/fz/js $W/fz/sys $W/fs/js $W/fs/sys
SCRIPT="60:shot=menu 70:p1=10 71:p1=0 100:shot=card320 101:p1=10 102:p1=0 130:shot=card256 131:p1=10 132:p1=0 160:shot=card384 161:p1=10 162:p1=0 190:shot=card224 191:p1=20 192:p1=0 194:p1=2 195:p1=0 196:p1=10 197:p1=0 260:shot=planes 261:p1=10 262:p1=0 263:p1=40 264:p1=0 300:shot=planes2 301:p1=20 302:p1=0 303:p1=2 304:p1=0 305:p1=10 306:p1=0 340:shot=sprites"
for i in $(seq 341 2 387); do SCRIPT="$SCRIPT $i:p1=10 $((i+1)):p1=0"; done
SCRIPT="$SCRIPT 389:p1=19 420:shot=sprites2 421:p1=0 500:shot=sprites3"
FZ=""; f=3; while [ $f -le ${FUZZ_LAST:-41} ]; do FZ="$FZ $f:shot=v$f"; f=$((f+2)); done
node tools/run.js rom/build/testrom.mnc $W/js "$SCRIPT" > /dev/null
node tools/run.js tools/vidfuzz/vidfuzz.mnc $W/fz/js "$FZ" > /dev/null
node tools/run.js tools/vidfuzz/vfs.mnc $W/fs/js "$FZ" > /dev/null
rtl/obj_sys/Vmc_system rom/build/testrom.mnc $W/sys "$SCRIPT" > $W/sys.log 2>&1 &
rtl/obj_sys/Vmc_system tools/vidfuzz/vidfuzz.mnc $W/fz/sys "$FZ" > $W/fz.log 2>&1 &
rtl/obj_sys/Vmc_system tools/vidfuzz/vfs.mnc $W/fs/sys "$FZ" > $W/fs.log 2>&1 &
wait
echo "== ROM de test"; grep "frames\|debug" $W/sys.log
for n in menu card320 card256 card384 card224 planes planes2 sprites sprites2 sprites3; do
	python3 tools/cmp_ppm.py --cpu-meter $W/js/$n.ppm $W/sys/$n.ppm $W/d_$n.png
done
for d in fz fs; do
	echo "== $d"; grep "frames" $W/$d.log
	n=0; ok=0
	for p in $W/$d/js/*.ppm; do
		b=$(basename $p); r=$(python3 tools/cmp_ppm.py $p $W/$d/sys/$b $W/$d/d_$b.png)
		n=$((n+1)); case "$r" in *" 0 pixels"*) ok=$((ok+1));; *) echo "$r";; esac
	done
	echo "images identiques : $ok sur $n"
done

# High resolution and interlaced output (scan-out with 640-pixel lines, VGA_F1): frames of the
# random ROM in 512 x 448i, 640 x 512i, 640 x 480i and 640 x 270 (see tools/vidfuzz/vidfuzz.c, DIP 512)
echo "== haute resolution et entrelace"
mkdir -p $W/hr/js $W/hr/sys
HR=""; for f in 11 13 59 61 107 109 155 157; do HR="$HR $f:shot=v$f"; done
node tools/run.js tools/vidfuzz/vidfuzz_hr.mnc $W/hr/js "$HR" > /dev/null
rtl/obj_sys/Vmc_system tools/vidfuzz/vidfuzz_hr.mnc $W/hr/sys "$HR" > $W/hr.log 2>&1
grep "frames" $W/hr.log
for f in 11 13 59 61 107 109 155 157; do python3 tools/cmp_ppm.py $W/hr/js/v$f.ppm $W/hr/sys/v$f.ppm $W/hr/d_v$f.png; done

# Loading a ROM over a running one (OSD): the test ROM plays its music, then hello is loaded.
# Nothing of the first game may remain: same picture as hello alone, sound silent apart from a click.
echo "== chargement par-dessus une autre ROM"
mkdir -p $W/ld/js $W/ld/sys
LD="20:p1=2 23:p1=0 26:p1=2 29:p1=0 32:p1=2 35:p1=0 38:p1=10 41:p1=0"
f=50; for i in $(seq 9); do LD="$LD $f:p1=2 $((f+3)):p1=0"; f=$((f+6)); done
LD="$LD 110:p1=10 113:p1=0 160:load=examples/hello/build/hello.mnc 230:shot=hello"
node tools/run.js examples/hello/build/hello.mnc $W/ld/js "60:shot=hello" > /dev/null
rtl/obj_sys/Vmc_system rom/build/testrom.mnc $W/ld/sys "$LD" 2>&1 | grep "peak"
python3 tools/cmp_ppm.py $W/ld/js/hello.ppm $W/ld/sys/hello.ppm $W/ld/d_hello.png
