// Bit-exact test of the MONOCORE-1 sound engine alone (Verilator, top = mc_apu).
// usage: Vmc_apu rom.mnc log out.raw   (log format: tools/apu/apu_log.js)
// The writes stamped k are applied on the bus between sample k-1 and the tick of sample k.
#include "Vmc_apu.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

struct W { long k; unsigned o, size; uint32_t v; };

int main(int argc, char **argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s rom.mnc log out.raw\n", argv[0]); return 1; }
    VerilatedContext ctx;
    Vmc_apu top(&ctx);
    std::ifstream rf(argv[1], std::ios::binary);
    std::vector<uint8_t> rom((std::istreambuf_iterator<char>(rf)), std::istreambuf_iterator<char>());
    size_t rom_size = std::max<size_t>((rom.size() + 3) & ~size_t(3), 0x100);
    rom.resize(rom_size + 64, 0);
    std::vector<W> log;
    long total = 0;
    std::ifstream lf(argv[2]);
    std::string line;
    while (std::getline(lf, line)) {
        if (line.empty()) continue;
        if (line[0] == '#') { total = atol(line.c_str() + 9); continue; }
        std::istringstream ss(line);
        W w; ss >> w.k >> w.o >> w.size >> w.v;
        log.push_back(w);
    }
    FILE *out = fopen(argv[3], "wb");

    auto tick = [&]() {
        top.clk = 1; top.eval();
        top.clk = 0; top.eval();
    };
    top.clk = 0; top.rst_n = 0; top.rd = 0; top.wr = 0; top.f_valid = 0; top.rom_size = rom_size;
    top.eval(); tick(); tick();
    top.rst_n = 1;

    // ROM fill model: 4 cycles, then 4 beats on consecutive cycles (core at 50 MHz)
    int fill_wait = -1, fill_beat = 0;
    uint32_t fill_addr = 0;
    long n = 0;
    size_t wi = 0;
    bool need_writes = true;
    uint64_t cyc = 0, busy_max = 0, busy_start = 0;
    bool counting = false;
    while (n < total) {
        top.wr = 0;
        if (need_writes) {
            // all writes stamped <= n, one per cycle
            if (wi < log.size() && log[wi].k <= n) {
                const W &w = log[wi++];
                unsigned sh = (w.o & 3) * 8;
                uint32_t mask = w.size == 4 ? 0xFFFFFFFFu : w.size == 2 ? 0xFFFFu : 0xFFu;
                top.wr = 1;
                top.waddr = w.o & ~3u;
                top.wdata = (w.v & mask) << sh;
                top.be = (w.size == 4 ? 0xF : w.size == 2 ? 0x3 : 0x1) << (w.o & 3);
            } else need_writes = false;
        }
        top.f_valid = 0;
        if (fill_wait < 0 && top.f_req) { fill_wait = 4; fill_beat = 0; fill_addr = top.f_addr; }
        else if (fill_wait > 0) fill_wait--;
        else if (fill_wait == 0) {
            uint32_t a = fill_addr + fill_beat * 4, w = 0;
            memcpy(&w, &rom[a < rom.size() - 4 ? a : 0], 4);
            if (a >= rom.size() - 4) w = 0;
            top.f_valid = 1; top.f_data = w;
            if (++fill_beat == 4) fill_wait = -2; else fill_wait = 0;
        }
        top.eval();
        tick();
        if (fill_wait == -2 && !top.f_req) fill_wait = -1;
        cyc++;
        // longest stretch of engine work (writes held), against 1 041.67 cycles between samples
        if (!top.wr_ok) { if (!counting) { counting = true; busy_start = cyc; } }
        else if (counting) { counting = false; if (cyc - busy_start > busy_max) busy_max = cyc - busy_start; }
        if (top.smp_valid) {
            int16_t lr[2] = {(int16_t)top.smp_l, (int16_t)top.smp_r};
            fwrite(lr, 2, 2, out);
            n++;
            need_writes = true;
        }
    }
    fclose(out);
    printf("%ld samples, %llu cycles, longest computation %llu cycles\n", n, (unsigned long long)cyc, (unsigned long long)busy_max);
    return 0;
}
