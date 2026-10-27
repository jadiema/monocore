// MONOCORE-1 "mgfx": the graphics banks of MONOPIXEL, as a library for games.
//
// A bank (file .mgf, or a slot of the MONOPIXEL NVRAM) holds everything drawn in the editor
// (examples/monopixel): a sheet of 8 x 8 cells in 4 bpp (1 to 4 pages of 16 x 16 cells, i.e.
// 128 x 128 pixels a page), up to 16 palettes of 16 colours, up to 64 sprites (a rectangle of the
// sheet, with its palette and its animation) and up to 4 maps (decors) of 8 or 16 pixel tiles.
//
// In a game (library = mc, modules = mgfx in project.txt):
//     data = mybank.mgf                            // exported by tools/mgfx/mgfx.py
//     static mg_bank_t bank;
//     mg_open(&bank, mybank_file, mybank_size);    // reads the bank (nothing is copied yet)
//     mg_load(&bank, 0, 16, MG_LOAD_ALL);          // graphics into the CHR RAM from offset 0,
//                                                  // palettes into hardware palettes 16-31
//     mg_map_layer(&bank, 0, 1, 1);                // map 0 on layer 1, priority 1
//     int hero = mg_sprite_find(&bank, "HEROS");
//     mg_sprite(&bank, 1, hero, mg_frame_at(&bank, hero, mc_frames), x, y, 4);   // hardware sprite 1
// Format: MGFX.md. The same file is read and written in Python by tools/mgfx/mgbank.py.
#pragma once
#include <stdint.h>
#include <stdbool.h>

#define MG_MAGIC        0x3146474Du   // "MGF1"
#define MG_PAGES        4             // pages of the sheet
#define MG_SHEET_W      16            // cells across the sheet (128 pixels)
#define MG_PAGE_CELLS   256           // 16 x 16 cells a page
#define MG_CELL_BYTES   32            // 8 x 8 pixels, 4 bpp
#define MG_PAGE_BYTES   (MG_PAGE_CELLS * MG_CELL_BYTES)   // 8 KB
#define MG_PALS         16
#define MG_SPRITES      64
#define MG_MAPS         4
#define MG_MAP_CELLS    4096          // entries of a map at most (as a hardware layer)
#define MG_FRAMES       16            // images of an animation at most
#define MG_OBJECTS      64            // objects of a map at most

// ---------------------------------------------------------------- file layout
typedef struct {
  uint32_t magic, size;               // size of the whole bank in bytes
  uint8_t  n_pages, n_pals, n_sprites, n_maps;
  uint8_t  flags, version;            // flags: MG_HF_ATTRS | MG_HF_OBJECTS; version: 1 or 2
  uint16_t reserved;
  char     name[16];
} mg_header_t;                        // 32 bytes, then: palettes (n_pals x 16 x u16 RGB555),
                                      // sprites (n_sprites x 16 bytes), maps (n_maps x 8 bytes),
                                      // the packed sheet, the packed maps (each: u32 length, data)

#define MG_HF_ATTRS   1               // the bank has tile attributes (version 2)
#define MG_HF_OBJECTS 2               // the maps have objects (version 2)

typedef struct {
  uint8_t x, y;                       // first cell of image 0, in the sheet (x 0-15, y 0-63)
  uint8_t w, h;                       // size in cells (1-8)
  uint8_t pal;                        // palette of the bank (0-15)
  uint8_t frames;                     // images of the animation (1-16), side by side in the sheet
  uint8_t speed;                      // video frames per image (0 = no animation)
  uint8_t flags;                      // MG_SF_PINGPONG
  char    name[8];
} mg_sprite_t;                        // 16 bytes
#define MG_SF_PINGPONG 1              // the animation goes back and forth (0 1 2 1 0 1 2...)

typedef struct {
  uint8_t wcode, hcode;               // size: 32 << code tiles (0 = 32, 1 = 64, 2 = 128), w x h <= 4096
  uint8_t tile16;                     // 1: tiles of 16 x 16 pixels (2 x 2 cells of the sheet)
  uint8_t reserved[5];
} mg_map_t;                           // 8 bytes
#define MG_MAP_W(m) (32 << (m)->wcode)
#define MG_MAP_H(m) (32 << (m)->hcode)

// tile attributes: one byte per cell of the sheet; a 16-pixel tile has those of its top-left cell
#define MG_SOLID     0x01             // a wall: nothing goes through
#define MG_PLATFORM  0x02             // solid from above only (one goes through it from below, jumping)
#define MG_LADDER    0x04
#define MG_WATER     0x08
#define MG_DANGER    0x10             // spikes, lava...
#define MG_USER_A    0x20             // free for the game
#define MG_USER_B    0x40
#define MG_USER_C    0x80

// an object of a map: a sprite of the bank placed in the decor (the start of the hero, a coin, an
// enemy...); the game decides what each sprite does, usually by its name
typedef struct {
  uint16_t x, y;                      // pixels, from the top left of the map
  uint8_t  sprite;                    // number of the sprite in the bank
  uint8_t  param;                     // free value set in the editor (0-255)
  uint8_t  flags;                     // MG_OF_HFLIP
  uint8_t  reserved;
} mg_object_t;                        // 8 bytes
#define MG_OF_HFLIP 1

// map entries (u16): tile, palette of the bank, mirrors. Tile: a cell of the sheet (0-1023) for
// 8-pixel tiles; for 16-pixel tiles, t covers the cells (t % 8) * 2 and (t / 8) * 2 (0-255).
#define MG_ENTRY(tile, pal, hflip, vflip) \
  ((uint16_t)((tile) | ((pal) << 10) | ((hflip) ? 0x4000 : 0) | ((vflip) ? 0x8000 : 0)))
#define MG_E_TILE(e)  ((e) & 0x3FF)
#define MG_E_PAL(e)   (((e) >> 10) & 15)
#define MG_E_HFLIP(e) (((e) >> 14) & 1)
#define MG_E_VFLIP(e) (((e) >> 15) & 1)

// position of an image of a sprite: to the right of the previous one, or at the start of the next
// band of h cell rows when it does not fit in the 16 columns of the sheet
void mg_frame_pos(const mg_sprite_t *s, int frame, int *cx, int *cy);

// ---------------------------------------------------------------- packing (PackBits-like)
// control byte c < 128: c + 1 bytes follow as they are; c >= 128: the next byte, c - 125 times (3-130)
uint32_t mg_pack(const uint8_t *src, uint32_t n, uint8_t *dst);           // packed size (dst NULL: counts only)
uint32_t mg_pack_bound(uint32_t n);                                        // room dst needs at most
int mg_unpack(const uint8_t *src, uint32_t len, volatile uint8_t *dst, uint32_t n);   // 0 if exact

// ---------------------------------------------------------------- a bank, for a game
typedef struct {
  const mg_header_t *h;
  const uint16_t *pals;               // n_pals x 16 colours
  const mg_sprite_t *spr;
  const mg_map_t *maps;
  const uint8_t *sheet; uint32_t sheet_len;          // packed sheet
  const uint8_t *map_data[MG_MAPS]; uint32_t map_len[MG_MAPS];   // packed maps
  // set by mg_load
  uint32_t chr, chr16, chr_spr;       // CHR RAM offsets: sheet, 16 x 16 tiles, sprite cells
  int pal;                            // first hardware palette
  uint16_t cell[MG_SPRITES];          // first cell of each sprite (from the start of the CHR RAM)
  // set by mg_open (version 2)
  uint8_t attr[MG_PAGES * MG_PAGE_CELLS];          // attributes of the cells (0 for a version 1 bank)
  const mg_object_t *obj[MG_MAPS]; uint16_t n_obj[MG_MAPS];
} mg_bank_t;

int  mg_check(const void *data, uint32_t max);     // 0 if the bank is valid
int  mg_open(mg_bank_t *b, const void *data, uint32_t max);   // 0, or -1 if invalid
const char *mg_name(const mg_bank_t *b);

#define MG_LOAD_SHEET   1             // the sheet, as 8 x 8 tiles (for maps of 8-pixel tiles)
#define MG_LOAD_TILES16 2             // the 16 x 16 tiles made from it (maps of 16-pixel tiles)
#define MG_LOAD_SPRITES 4             // the cells of the sprites, in the order of the hardware
#define MG_LOAD_ALL     7
// Unpacks the bank into the CHR RAM from chr_offset (a multiple of 32; the sheet always first, as
// the other parts are made from it) and its palettes into the hardware palettes first_pal on.
// With MG_LOAD_SPRITES, also points the sprite graphics (SPR_CHR) at the start of the CHR RAM.
// Returns the offset just after what was written, or 0 if it does not fit in the CHR RAM.
uint32_t mg_load(mg_bank_t *b, uint32_t chr_offset, int first_pal, int what);
uint32_t mg_load_size(const mg_bank_t *b, int what);   // bytes of CHR RAM mg_load would use
void mg_palettes(const mg_bank_t *b, int first_pal);   // the palettes alone (fades, swaps...)

// sprites
int  mg_sprite_count(const mg_bank_t *b);
const mg_sprite_t *mg_sprite_def(const mg_bank_t *b, int s);    // NULL if out of range
int  mg_sprite_find(const mg_bank_t *b, const char *name);      // its number, or -1
int  mg_sprite_cell(const mg_bank_t *b, int s, int frame);      // for mc_sprite (after mg_load)
int  mg_frame_at(const mg_bank_t *b, int s, uint32_t t);        // image shown at video frame t
int  mg_sprite_frame(const mg_sprite_t *s, uint32_t t);          // the same, from the sprite itself
// sets hardware sprite hw (shadow table of mc.h) to image frame of sprite s, at x, y
void mg_sprite(const mg_bank_t *b, int hw, int s, int frame, int x, int y, int prio);
int  mg_sprite_w(const mg_bank_t *b, int s);                    // size in pixels
int  mg_sprite_h(const mg_bank_t *b, int s);

// maps
int  mg_map_count(const mg_bank_t *b);
int  mg_map_w(const mg_bank_t *b, int m);                       // in tiles
int  mg_map_h(const mg_bank_t *b, int m);
int  mg_map_tile(const mg_bank_t *b, int m);                    // 8 or 16 (pixels)
// shows map m on hardware layer n (after mg_load): graphics, size, the entries of the tilemap
int  mg_map_layer(const mg_bank_t *b, int m, int layer, int prio);
// unpacks the entries of map m (MG_MAP_W x MG_MAP_H u16, row by row); 0 if fine
int  mg_map_read(const mg_bank_t *b, int m, uint16_t *out);
// objects of map m
int  mg_object_count(const mg_bank_t *b, int m);
const mg_object_t *mg_object(const mg_bank_t *b, int m, int i);   // NULL if out of range
// attributes (MG_SOLID...) of a tile of map m; of the tile under the pixel (px, py) of a layer
// showing a map of this bank (layer: as mg_tile_at)
int  mg_tile_attr(const mg_bank_t *b, int m, int tile);
int  mg_attr_at(const mg_bank_t *b, int layer, int px, int py);
// tile number under the pixel (px, py) of the map shown on layer n (it wraps around, as the
// hardware does); for collisions. mg_layer_entry gives the whole hardware entry.
int  mg_tile_at(int layer, int px, int py);
uint32_t mg_layer_entry(int layer, int tx, int ty);
void mg_layer_set(int layer, int tx, int ty, uint32_t entry);   // changes one tile (a door opens)

// ---------------------------------------------------------------- tools of the editor
// builds the 16 x 16 tiles (count, 128 bytes each) from a sheet already in memory
void mg_make_tiles16(volatile const uint8_t *sheet, volatile uint8_t *out, int count);
