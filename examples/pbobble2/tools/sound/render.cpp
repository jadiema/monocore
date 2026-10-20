// Offline sound renderer: runs the game's own Z80 sound driver (m1.bin) with a YM2610 (ymfm) and
// writes what one sound command produces. Used to prepare the sounds of the port; nothing of this
// runs on the console. Build with tools/sound/build.sh (fetches ymfm, BSD-3, and a Z80 core, MIT).
//
// usage: render M1 V OUT SECONDS CMD...     one WAV per command: OUT_xx.wav (hex command)
//        render M1 V OUT SECONDS -trace FILE  plays a trace "frame command" (60 frames per second)
// Neo Geo sound board: Z80 at 4 MHz, YM2610 at 8 MHz (output 8 MHz / 144 = 55555 Hz), 68000 to Z80
// through a latch and the NMI, Z80 memory 0000-7FFF fixed, 8000/C000/E000/F000 banked by reads of
// ports 0B/0A/09/08 with the bank number on the high byte of the address bus, RAM F800-FFFF.
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <string>
#include "ymfm_opn.h"
extern "C" {
#include "z80.h"
extern uint8_t z80_io_high;              // high byte of the I/O address (patch in build.sh)
}

static std::vector<uint8_t> m1, vrom;
static uint8_t ram[0x800];
static uint32_t bank[4] = {0x1E, 0x0E, 0x06, 0x02};      // F000 2K, E000 4K, C000 8K, 8000 16K
static uint8_t latch, reply; static bool nmi_on;

struct intf : public ymfm::ymfm_interface {
  int32_t timer[2] = {-1, -1}; bool irq = false;
  uint8_t ymfm_external_read(ymfm::access_class t, uint32_t a) override {
    (void)t; return vrom.empty() ? 0 : vrom[a % vrom.size()];
  }
  void ymfm_set_timer(uint32_t n, int32_t d) override { timer[n] = d; }
  void ymfm_update_irq(bool a) override { irq = a; }
  void tick(int32_t clocks) {
    for (int n = 0; n < 2; n++)
      if (timer[n] >= 0 && (timer[n] -= clocks) <= 0) { timer[n] = -1; m_engine->engine_timer_expired(n); }
  }
};
static intf yi; static ymfm::ym2610 *ym;

static uint8_t rd(void *, uint16_t a) {
  if (a < 0x8000) return m1[a];
  if (a >= 0xF800) return ram[a & 0x7FF];
  if (a >= 0xF000) return m1[(bank[0] * 0x800 + (a & 0x7FF)) % m1.size()];
  if (a >= 0xE000) return m1[(bank[1] * 0x1000 + (a & 0xFFF)) % m1.size()];
  if (a >= 0xC000) return m1[(bank[2] * 0x2000 + (a & 0x1FFF)) % m1.size()];
  return m1[(bank[3] * 0x4000 + (a & 0x3FFF)) % m1.size()];
}
static void wr(void *, uint16_t a, uint8_t v) { if (a >= 0xF800) ram[a & 0x7FF] = v; }
static uint8_t in(z80 *, uint8_t p) {
  switch (p & 0x0F) {
  case 0x00: return latch;
  case 0x04: case 0x05: case 0x06: case 0x07: return ym->read(p & 3);
  case 0x08: case 0x09: case 0x0A: case 0x0B: bank[p & 3] = z80_io_high; return 0;
  }
  return 0;
}
static void out(z80 *, uint8_t p, uint8_t v) {
  if ((p & 0x1F) == 0x08) nmi_on = true;
  else if ((p & 0x1F) == 0x18) nmi_on = false;
  else if ((p & 0x0C) == 0x04) ym->write(p & 3, v);
  else if ((p & 0x0F) == 0x0C) reply = v;
}

static z80 cpu;
static void reset() {
  memset(ram, 0, sizeof ram); bank[0] = 0x1E; bank[1] = 0x0E; bank[2] = 0x06; bank[3] = 0x02;
  latch = reply = 0; nmi_on = false;
  z80_init(&cpu); cpu.read_byte = rd; cpu.write_byte = wr; cpu.port_in = in; cpu.port_out = out;
  ym->reset(); yi.timer[0] = yi.timer[1] = -1; yi.irq = false;
}
static void command(uint8_t c) { latch = c; if (nmi_on) z80_gen_nmi(&cpu); }

// one output sample: 72 Z80 cycles, 144 YM2610 clocks
static void sample(int16_t *lr) {
  unsigned long target = cpu.cyc + 72;
  while (cpu.cyc < target) {
    cpu.int_pending = yi.irq; cpu.int_data = 0xFF;
    z80_step(&cpu);
  }
  yi.tick(144);
  ymfm::ym2610::output_data o; ym->generate(&o, 1);
  for (int k = 0; k < 2; k++) {
    int32_t v = o.data[k] + o.data[2] / 2;       // FM + ADPCM, then SSG (mono) at half level
    lr[k] = (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
  }
}

static void wav(const std::string &path, const std::vector<int16_t> &s) {
  FILE *f = fopen(path.c_str(), "wb");
  uint32_t rate = 55555, n = s.size() * 2, v;
  fwrite("RIFF", 1, 4, f); v = 36 + n; fwrite(&v, 4, 1, f); fwrite("WAVEfmt ", 1, 8, f);
  v = 16; fwrite(&v, 4, 1, f); uint16_t h[2] = {1, 2}; fwrite(h, 2, 2, f); fwrite(&rate, 4, 1, f);
  v = rate * 4; fwrite(&v, 4, 1, f); h[0] = 4; h[1] = 16; fwrite(h, 2, 2, f);
  fwrite("data", 1, 4, f); fwrite(&n, 4, 1, f); fwrite(s.data(), 2, s.size(), f); fclose(f);
}

static std::vector<uint8_t> load(const char *p) {
  FILE *f = fopen(p, "rb"); if (!f) { fprintf(stderr, "missing %s\n", p); exit(1); }
  std::vector<uint8_t> d; int c; while ((c = fgetc(f)) != EOF) d.push_back((uint8_t)c); fclose(f); return d;
}

int main(int argc, char **argv) {
  if (argc < 6) { fprintf(stderr, "usage: render M1 V OUT SECONDS CMD... | -trace FILE\n"); return 1; }
  m1 = load(argv[1]); vrom = load(argv[2]);
  ym = new ymfm::ym2610(yi);
  std::string base = argv[3]; double seconds = atof(argv[4]);
  const uint32_t rate = 55555, boot = rate / 2;
  if (!strcmp(argv[5], "-trace")) {           // the commands of a game session, at their frames
    FILE *f = fopen(argv[6], "r"); unsigned fr, c; std::vector<std::pair<unsigned, unsigned>> tr;
    while (f && fscanf(f, "%u %x", &fr, &c) == 2) tr.push_back({fr, c});
    reset(); std::vector<int16_t> s; size_t k = 0; int16_t lr[2];
    for (uint32_t i = 0; i < (uint32_t)(seconds * rate); i++) {
      while (k < tr.size() && tr[k].first * rate / 60 <= i) command((uint8_t)tr[k++].second);
      sample(lr); s.push_back(lr[0]); s.push_back(lr[1]);
    }
    wav(base + ".wav", s); return 0;
  }
  for (int a = 5; a < argc; a++) {
    unsigned c = strtoul(argv[a], 0, 16);
    reset(); int16_t lr[2]; std::vector<int16_t> s;
    for (uint32_t i = 0; i < boot; i++) sample(lr);                 // driver start-up
    command(0x03); for (uint32_t i = 0; i < rate / 10; i++) sample(lr);
    command((uint8_t)c);
    for (uint32_t i = 0; i < (uint32_t)(seconds * rate); i++) { sample(lr); s.push_back(lr[0]); s.push_back(lr[1]); }
    char name[16]; snprintf(name, sizeof name, "_%02X.wav", c); wav(base + name, s);
    int32_t peak = 0; for (int16_t v : s) peak = abs(v) > peak ? abs(v) : peak;
    printf("%02X: peak %d, reply %02X\n", c, peak, reply);
  }
  return 0;
}
