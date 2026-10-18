#!/bin/sh
# build vidfuzz.mnc; optional argument: default DIP switches (see vidfuzz.c)
set -e
cd "$(dirname "$0")"
SDK=../../sdk; A="--target=riscv32 -march=rv32im -mabi=ilp32 -mno-relax"; B=build
mkdir -p $B
[ -f blob.bin ] || python3 -c "import random; random.seed(7); open('blob.bin','wb').write(bytes(random.getrandbits(8) for _ in range(65536)))"
clang $A -O2 -ffreestanding -fno-builtin -nostdlib -Wall -I$SDK -c vidfuzz.c -o $B/v.o
clang $A -c $SDK/crt0.S -o $B/crt0.o
clang $A -O2 -ffreestanding -fno-builtin -nostdlib -I$SDK -c $SDK/mini.c -o $B/mini.o
clang $A -c blob.S -o $B/blob.o
ld.lld -T $SDK/link.ld $B/crt0.o $B/v.o $B/mini.o $B/blob.o -o $B/v.elf
llvm-objcopy -O binary $B/v.elf $B/v.bin
python3 $SDK/mkrom.py $B/v.bin ${2:-vidfuzz.mnc} "VIDEO FUZZ" "test RTL" 0 ${1:-0}
