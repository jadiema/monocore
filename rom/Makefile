# MONOCORE-1 test ROM
SDK     = ../sdk
CC      = clang
LD      = ld.lld
OBJCOPY = llvm-objcopy
ARCH    = --target=riscv32 -march=rv32im -mabi=ilp32 -mno-relax
CFLAGS  = $(ARCH) -O2 -ffreestanding -fno-builtin -nostdlib -Wall -Wno-misleading-indentation -I$(SDK) -Ibuild
OUT     = build/testrom.mnc

all: $(OUT)

build/assets.bin build/assets.h: assets.py
	python3 assets.py build

build/%.o: %.c build/assets.h $(SDK)/monocore.h
	$(CC) $(CFLAGS) -c $< -o $@

build/assets.o: assets.S build/assets.bin
	$(CC) $(ARCH) -c assets.S -o $@

build/crt0.o: $(SDK)/crt0.S
	$(CC) $(ARCH) -c $< -o $@

build/mini.o: $(SDK)/mini.c
	$(CC) $(CFLAGS) -c $< -o $@

build/testrom.elf: build/crt0.o build/test.o build/mini.o build/assets.o $(SDK)/link.ld
	$(LD) -T $(SDK)/link.ld build/crt0.o build/test.o build/mini.o build/assets.o -o $@

$(OUT): build/testrom.elf
	$(OBJCOPY) -O binary $< build/testrom.bin
	python3 $(SDK)/mkrom.py build/testrom.bin $@ "ROM DE TEST" "MONOCORE-1 v0.1"

clean:
	rm -rf build

.PHONY: all clean
