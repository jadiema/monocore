// MONOCORE-1 "nvslots": the 8 NVRAM slots of an editor (MONOTRACKER, MONOPIXEL, MONOSFX), alone or
// sharing the NVRAM of the atelier (examples/atelier).
//
// Alone (the ROM of the editor): the NVRAM is the editor's own, as before: its magic ("MTNV",
// "MGNV", "MSNV"), byte 4 the keyboard layout, byte 5 the last slot, bytes 6-15 settings of the
// editor, then 8 x {u32 offset, u32 size} from 16, items from 80.
// In the atelier: "ATNV", byte 4 the layout, bytes 5-7 the last slot of each editor, bytes 8-11
// settings of the editors, 12-14 the slots the game plays (0xFF: the last ones), the last 256 bytes
// of the NVRAM for the save of the game (mj_save), then 32 x {offset, size} from 16 (editor e: slots 8e to 8e + 7), items from 272; the
// item space is shared, packed in the order of the slots.
// In project.txt: modules = nvslots. Documentation: ATELIER.md.
#pragma once
#include <stdint.h>
#include <stdbool.h>

enum { NVS_TRACKER, NVS_PIXEL, NVS_SFX, NVS_BASIC };  // editor numbers in the atelier (slots 8e to 8e + 7)
#define NVS_GAME NVS_BASIC                // a game only reads the others' items
#define NVS_SLOTS 8

// magic: the editor's own NVRAM ("MGNV"); app: its number; check: 0 if an item is valid (else the
// slot is emptied). Returns true when the NVRAM is the atelier's.
bool nvs_init(const char *magic, int app, int (*check)(const void *item, uint32_t size));
bool nvs_shared(void);
uint32_t nvs_size(int k);                 // 0: empty
const void *nvs_data(int k);
uint32_t nvs_free(int replacing);         // bytes that can be written (counting slot k given back)
void nvs_delete(int k);
uint8_t *nvs_alloc(int k, uint32_t size); // replaces slot k by size bytes, to write; NULL if full
int  nvs_last(void);                      // last slot used (0xFF: none)
void nvs_set_last(int k);
int  nvs_get(int i);                      // settings byte i: 4 = keyboard layout; 6-9 the editor's own
void nvs_set(int i, int v);               // (bytes 8-11 in the atelier); 12-14: the game, in the atelier
void nvs_format_atelier(void);            // the launcher: makes the NVRAM the atelier's (all is lost)
// the atelier: slot k of editor app (another editor's items, for the game)
uint32_t nvs_size_of(int app, int k);
const void *nvs_data_of(int app, int k);
int nvs_last_of(int app);
