# Template Makefile for a MONOCORE-1 ROM. Copy this folder to start a new game.
NAME    = hello
TITLE   = "BONJOUR"
ORIGIN  = "exemple du SDK"
SOURCES = hello.c
SDK     = ../../sdk

CC      = clang
LD      = ld.lld
OBJCOPY = llvm-objcopy
ARCH    = --target=riscv32 -march=rv32im -mabi=ilp32 -mno-relax
CFLAGS  = $(ARCH) -O2 -ffreestanding -fno-builtin -nostdlib -Wall -I$(SDK)

OBJS = build/crt0.o build/mini.o build/data.o $(SOURCES:%.c=build/%.o)
ASSETS = build/font.chr build/robot.chr

all: build/$(NAME).mnc

build:
	mkdir -p build

# graphics: PNG -> .chr (cells) + .pal (palette)
build/font.chr: $(SDK)/font.png | build
	python3 $(SDK)/tools/png2chr.py $< build/font
build/robot.chr: robot.png | build
	python3 $(SDK)/tools/png2chr.py $< build/robot

build/%.o: %.c $(SDK)/monocore.h | build
	$(CC) $(CFLAGS) -c $< -o $@
build/data.o: data.S $(ASSETS)
	$(CC) $(ARCH) -c data.S -o $@
build/crt0.o: $(SDK)/crt0.S | build
	$(CC) $(ARCH) -c $< -o $@
build/mini.o: $(SDK)/mini.c | build
	$(CC) $(CFLAGS) -c $< -o $@

build/$(NAME).elf: $(OBJS) $(SDK)/link.ld
	$(LD) -T $(SDK)/link.ld $(OBJS) -o $@

build/$(NAME).mnc: build/$(NAME).elf
	$(OBJCOPY) -O binary $< build/$(NAME).bin
	python3 $(SDK)/mkrom.py build/$(NAME).bin $@ $(TITLE) $(ORIGIN)

clean:
	rm -rf build

.PHONY: all clean
