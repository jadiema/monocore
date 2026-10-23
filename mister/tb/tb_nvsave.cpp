#include "Vnvtop.h"
#include <cstdio>
#include <cstdint>
#include <vector>
static Vnvtop *t; static uint64_t cyc;
static void tick() { t->clk = 0; t->eval(); t->clk = 1; t->eval(); cyc++; }
static std::vector<uint8_t> file;               // the .sav
static bool hold_seen;
static void serve() {                           // hps_io: one request, if any (its timing roughly)
  if (!(t->sd_rd || t->sd_wr)) return;
  bool rd = t->sd_rd; uint32_t lba = t->sd_lba;
  for (int i = 0; i < 5; i++) tick();
  t->sd_ack = 1; tick();
  for (int w = 0; w < 256; w++) {
    t->sd_buff_addr = w; tick();
    uint32_t off = lba * 512 + w * 2;
    if (rd) {                                    // the file to the FPGA
      if (file.size() < off + 2) file.resize(off + 2, 0);
      t->sd_buff_dout = file[off] | (file[off + 1] << 8); t->sd_buff_wr = 1; tick(); t->sd_buff_wr = 0; tick(); tick();
    } else {                                     // the FPGA to the file
      tick(); tick();
      uint16_t v = t->sd_buff_din;
      if (file.size() < off + 2) file.resize(off + 2, 0);
      file[off] = v & 0xFF; file[off + 1] = v >> 8;
    }
    if (t->hold) hold_seen = true;
  }
  t->sd_ack = 0; tick(); tick();
}
static void run(int n) { for (int i = 0; i < n; i++) { if (t->hold) hold_seen = true; serve(); tick(); } }
static uint32_t cpu_read(uint32_t a) { t->cpu_en = 1; t->cpu_we = 0; t->cpu_addr = a; tick(); t->cpu_en = 0; tick(); return t->cpu_rdata; }
static void cpu_write(uint32_t a, uint32_t v) { t->cpu_en = 1; t->cpu_we = 0xF; t->cpu_addr = a; t->cpu_wdata = v; tick(); t->cpu_en = 0; t->cpu_we = 0; tick(); }
static void load_rom(uint64_t sav_size) {      // a ROM download, the .sav mounted at its end
  t->downloading = 1; run(50);
  t->img_size = sav_size; t->img_readonly = 0; t->img_mounted = 1; tick(); t->img_mounted = 0;
  run(10); t->downloading = 0;
}
static uint32_t pat(uint32_t a) { return a * 2654435761u ^ 0xA5A5F00Du; }
int main() {
  t = new Vnvtop; int bad = 0;
  t->autosave = 1;
  // garbage in the NVRAM (a previous game)
  for (uint32_t a = 0; a < 8192; a += 97) cpu_write(a, 0xDEADBEEF);
  // 1. a ROM with an empty .sav: the NVRAM cleared, the console held
  hold_seen = false; load_rom(0); run(9000);
  int nz = 0; for (uint32_t a = 0; a < 8192; a++) if (cpu_read(a)) nz++;
  printf("1. empty save: enabled %d, held %d, NVRAM words not zero %d\n", t->enabled, hold_seen, nz); bad += nz || !hold_seen || !t->enabled;
  // 2. the console writes, the OSD opens: autosave
  for (uint32_t a = 0; a < 8192; a++) cpu_write(a, pat(a));
  t->osd_open = 1; run(20000); t->osd_open = 0; run(10);
  int fb = 0; for (uint32_t a = 0; a < 8192; a++) { uint32_t v = file[4*a] | file[4*a+1] << 8 | file[4*a+2] << 16 | (uint32_t)file[4*a+3] << 24; if (v != pat(a)) fb++; }
  printf("2. autosave: file %zu bytes, words wrong %d, busy %d\n", file.size(), fb, t->busy); bad += fb || file.size() != 32768;
  // OSD opened again without a change: no write
  std::vector<uint8_t> before = file; t->osd_open = 1; run(2000); t->osd_open = 0; run(10);
  printf("   OSD again, nothing changed: writes %s\n", t->busy ? "busy?" : "none");
  // 3. the NVRAM spoiled, the ROM loaded again with its .sav: the NVRAM back
  for (uint32_t a = 0; a < 8192; a++) cpu_write(a, 0);
  hold_seen = false; load_rom(file.size()); run(20000);
  int nb = 0; for (uint32_t a = 0; a < 8192; a++) if (cpu_read(a) != pat(a)) nb++;
  printf("3. reload: held %d, NVRAM words wrong %d\n", hold_seen, nb); bad += nb || !hold_seen;
  printf(bad ? "FAILED\n" : "ALL GOOD\n");
  return bad != 0;
}
