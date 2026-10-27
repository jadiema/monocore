// MONOCORE-1 "atelier": several programs in one ROM (examples/atelier, built by tools/mkatelier.py).
//
// The atelier ROM holds the launcher (at address 0) and complete programs (MONOTRACKER,
// MONOPIXEL, MONOSFX, a game...), each linked at its own ROM address and all using the same WRAM:
// one runs at a time, and going to another starts it afresh (its start-up code). Being one ROM,
// they share one NVRAM: the nvslots module keeps the items of each one there.
// The table of the programs is at ROM address AT_TABLE: "MATL", u32 count, count x {u32 base,
// char title[16], u32 reserved}.
// In project.txt: modules = atelier. Documentation: ATELIER.md.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define AT_TABLE 0x001F0000u
#define AT_PROGRAMS 8

bool at_present(void);                    // this ROM is an atelier
int  at_count(void);
const char *at_title(int i);              // its title ("MONOPIXEL"...)
int  at_find(const char *title);          // its number, or -1
void at_run(int i);                       // starts program i (0: the launcher); does not return
void at_exit(void);                       // back to the launcher (when at_present)
