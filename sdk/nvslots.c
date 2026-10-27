// MONOCORE-1 "nvslots": NVRAM slots of the editors, alone or in the atelier (see nvslots.h).
#include "mc.h"
#include "nvslots.h"

#define NV MC_NVRAM
static int shared, app_no, table_slots = 8;
static uint32_t data_start = 80;
#define TOP (shared ? MC_NVRAM_SIZE - 256 : MC_NVRAM_SIZE)   // atelier: the last 256 bytes for the game (mj_save)

static uint32_t rd(uint32_t a) { return NV[a] | (NV[a + 1] << 8) | (NV[a + 2] << 16) | ((uint32_t)NV[a + 3] << 24); }
static void wr(uint32_t a, uint32_t v) { for (int i = 0; i < 4; i++) NV[a + i] = (uint8_t)(v >> (8 * i)); }
static uint32_t al4(uint32_t v) { return (v + 3) & ~3u; }
static int phys(int app, int k) { return shared ? app * NVS_SLOTS + k : k; }
static uint32_t off_p(int p) { return rd(16 + 8 * p); }
static uint32_t size_p(int p) { return rd(20 + 8 * p); }
static uint32_t end_all(void) {
  uint32_t e = data_start;
  for (int p = 0; p < table_slots; p++) if (size_p(p)) { uint32_t f = off_p(p) + al4(size_p(p)); if (f > e) e = f; }
  return e;
}

bool nvs_init(const char *magic, int app, int (*check)(const void *, uint32_t)) {
  app_no = app;
  shared = NV[0] == 'A' && NV[1] == 'T' && NV[2] == 'N' && NV[3] == 'V';
  table_slots = shared ? 32 : 8; data_start = shared ? 16 + 8 * 32 : 80;
  if (!shared && !(NV[0] == magic[0] && NV[1] == magic[1] && NV[2] == magic[2] && NV[3] == magic[3])) {
    for (uint32_t i = 0; i < 80; i++) NV[i] = 0;            // a new NVRAM for this editor
    for (int i = 0; i < 4; i++) NV[i] = (uint8_t)magic[i];
    NV[5] = 0xFF;
  }
  for (int k = 0; k < NVS_SLOTS; k++) {                       // drop what is not valid
    int p = phys(app, k);
    uint32_t s = size_p(p);
    if (s && (off_p(p) < data_start || off_p(p) + s > TOP || (check && check((const void *)(NVRAM_BASE + off_p(p)), s)))) {
      wr(16 + 8 * p, 0); wr(20 + 8 * p, 0);
    }
  }
  return shared;
}
bool nvs_shared(void) { return shared; }
void nvs_format_atelier(void) {
  for (uint32_t i = 0; i < 16 + 8 * 32; i++) NV[i] = 0;
  NV[0] = 'A'; NV[1] = 'T'; NV[2] = 'N'; NV[3] = 'V';
  for (int i = 5; i <= 7; i++) NV[i] = 0xFF;
  for (int i = 12; i <= 15; i++) NV[i] = 0xFF;
}
uint32_t nvs_size_of(int app, int k) { return k >= 0 && k < NVS_SLOTS && (shared || app == app_no) ? size_p(phys(app, k)) : 0; }
const void *nvs_data_of(int app, int k) { return (const void *)(NVRAM_BASE + off_p(phys(app, k))); }
static int last_byte(int app) { return app < 3 ? 5 + app : 15; }   // atelier: MONOBASIC in byte 15
int nvs_last_of(int app) { return shared ? NV[last_byte(app)] : (app == app_no ? NV[5] : 0xFF); }
uint32_t nvs_size(int k) { return nvs_size_of(app_no, k); }
const void *nvs_data(int k) { return nvs_data_of(app_no, k); }
uint32_t nvs_free(int k) { return TOP - end_all() + (k >= 0 ? al4(nvs_size(k)) : 0); }
void nvs_delete(int k) {
  int p = phys(app_no, k);
  uint32_t off = off_p(p), size = al4(size_p(p)), end = end_all();
  if (!size_p(p)) return;
  for (uint32_t i = off; i + size < end; i++) NV[i] = NV[i + size];
  for (int j = 0; j < table_slots; j++) if (size_p(j) && off_p(j) > off) wr(16 + 8 * j, off_p(j) - size);
  wr(16 + 8 * p, 0); wr(20 + 8 * p, 0);
}
uint8_t *nvs_alloc(int k, uint32_t size) {
  if (size > nvs_free(k)) return 0;
  nvs_delete(k);
  uint32_t off = end_all();
  int p = phys(app_no, k);
  wr(16 + 8 * p, off); wr(20 + 8 * p, size);
  return (uint8_t *)(NVRAM_BASE + off);
}
int nvs_last(void) { return nvs_last_of(app_no); }
void nvs_set_last(int k) { NV[shared ? last_byte(app_no) : 5] = (uint8_t)k; }
static int setting(int i) { return (shared && i >= 6 && i <= 9 ? i + 2 : i) & 15; }   // shared: 5-7 are last slots
int nvs_get(int i) { return NV[setting(i)]; }
void nvs_set(int i, int v) { NV[setting(i)] = (uint8_t)v; }
