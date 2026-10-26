// MONOCORE-1 RTL testbench (Verilator).
// usage: Vmc_core rom.mnc outdir "frame:action ..."   (same script syntax as tools/run.js)
//   actions: p1=<hex> p2=<hex> p3=<hex> p4=<hex> sys=<hex> (inputs from the start of that frame),
//            a1=<hex> .. a4=<hex> (ANALOG_Pn), d1=<hex> .. d4=<hex> (PADDLE_Pn),
//            kd=<hex> ku=<hex> (key down / up, PS/2 code, 0x1xx = extended),
//            ms=<dx>,<dy>,<buttons>,<wheel> (one mouse packet, decimal; dy positive downwards,
//            buttons: bit 0 left, 1 right, 2 middle), mi=hh,hh,... (MIDI bytes, hexadecimal),
//            dump=<name> (video/sound memories at the end of that frame, same layout as run.js)
// The SDRAM is modelled behind the ROM port: first beat 4 cycles after the request is accepted,
// then one 32-bit beat per cycle (16-bit SDRAM at 100 MHz, core at 50 MHz).
#include "Vmc_core.h"
#include "verilated.h"
#include "verilated_syms.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <deque>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include <algorithm>

static std::vector<uint8_t> load(const char *path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

static VerilatedContext *ctx;

// pointer to the bytes of a 4-lane RAM (lane k holds byte k of every word)
static uint8_t *lane(const std::string &inst, int k) {
    std::string name = "TOP.mc_core." + inst + ".lane[" + std::to_string(k) + "].u";
    const VerilatedScope *s = ctx->scopeFind(name.c_str());
    if (!s) {
        fprintf(stderr, "scope %s not found; known scopes:\n", name.c_str());
        for (const auto &it : *ctx->scopeNameMap()) if (strstr(it.first, "palette")) fprintf(stderr, "  %s\n", it.first);
        exit(1);
    }
    const VerilatedVar *v = s->varFind("mem");
    if (!v) { fprintf(stderr, "mem not found in %s\n", name.c_str()); exit(1); }
    return (uint8_t *)v->datap();
}
static void read_ram(const std::string &inst, size_t bytes, std::vector<uint8_t> &out) {
    uint8_t *l[4];
    for (int k = 0; k < 4; k++) l[k] = lane(inst, k);
    for (size_t i = 0; i < bytes; i++) out.push_back(l[i & 3][i >> 2]);
}

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s rom.mnc outdir [script]\n", argv[0]); return 1; }
    ctx = new VerilatedContext;
    ctx->commandArgs(argc, argv);
    Vmc_core *top = new Vmc_core(ctx);
    std::vector<uint8_t> rom = load(argv[1]);
    rom.resize((rom.size() + 15) & ~size_t(15), 0);
    std::string outdir = argv[2];
    std::multimap<int, std::string> events;
    int last = 0;
    if (argc > 3) {
        std::istringstream ss(argv[3]);
        std::string tok;
        while (ss >> tok) {
            size_t c = tok.find(':');
            int f = atoi(tok.substr(0, c).c_str());
            events.insert({f, tok.substr(c + 1)});
            if (f > last) last = f;
        }
    }

    uint32_t p1 = 0, p2 = 0, p3 = 0, p4 = 0, sys = 0, an[4] = {0, 0, 0, 0}, pd[4] = {0, 0, 0, 0};
    std::deque<uint32_t> keys;   // pending key events: bit 9 down, bit 8 extended, code
    struct Mouse { int dx, dy, b, w; };
    std::deque<Mouse> mice;      // pending mouse packets
    std::deque<uint8_t> midi;    // pending MIDI bytes
    auto apply = [&](int f) {
        auto r = events.equal_range(f);
        for (auto it = r.first; it != r.second; ++it) {
            const std::string &a = it->second;
            uint32_t v = a.size() > 3 ? strtoul(a.c_str() + 3, 0, 16) : 0;
            if (a.rfind("p1=", 0) == 0) p1 = v;
            else if (a.rfind("p2=", 0) == 0) p2 = v;
            else if (a.rfind("p3=", 0) == 0) p3 = v;
            else if (a.rfind("p4=", 0) == 0) p4 = v;
            else if (a.size() > 3 && a[0] == 'a' && a[1] >= '1' && a[1] <= '4' && a[2] == '=') an[a[1] - '1'] = v;
            else if (a.size() > 3 && a[0] == 'd' && a[1] >= '1' && a[1] <= '4' && a[2] == '=') pd[a[1] - '1'] = v;
            else if (a.rfind("kd=", 0) == 0) keys.push_back(0x200 | (v & 0x1FF));
            else if (a.rfind("ku=", 0) == 0) keys.push_back(v & 0x1FF);
            else if (a.rfind("mi=", 0) == 0) {
                std::stringstream ms(a.substr(3)); std::string b;
                while (std::getline(ms, b, ',')) midi.push_back((uint8_t)strtoul(b.c_str(), 0, 16));
            }
            else if (a.rfind("ms=", 0) == 0) {
                Mouse ms = {0, 0, 0, 0};
                sscanf(a.c_str() + 3, "%d,%d,%d,%d", &ms.dx, &ms.dy, &ms.b, &ms.w);
                mice.push_back(ms);
            }
            else if (a.rfind("sys=", 0) == 0) sys = strtoul(a.c_str() + 4, 0, 16);
        }
        top->in_p1 = p1; top->in_p2 = p2; top->in_p3 = p3; top->in_p4 = p4; top->in_sys = sys;
        for (int i = 0; i < 4; i++) { top->in_analog[i] = an[i]; top->in_paddle[i] = pd[i]; }
    };
    // frame capture (pixels come from the renderer's palette stage)
    std::vector<uint32_t> fb(384 * 288, 0);
    int fb_w = 0, fb_h = 0;
    auto shot = [&](int f) {
        auto r = events.equal_range(f);
        for (auto it = r.first; it != r.second; ++it) {
            const std::string &a = it->second;
            if (a.rfind("shot", 0) != 0) continue;
            std::string name = a.size() > 5 ? a.substr(5) : ("f" + std::to_string(f));
            std::ofstream o(outdir + "/" + name + ".ppm", std::ios::binary);
            o << "P6\n" << fb_w << " " << fb_h << "\n255\n";
            for (int yy = 0; yy < fb_h; yy++)
                for (int xx = 0; xx < fb_w; xx++) {
                    uint32_t c = fb[yy * 384 + xx];
                    int rr = (c >> 10) & 31, gg = (c >> 5) & 31, bb = c & 31;
                    unsigned char px[3] = {(unsigned char)((rr << 3) | (rr >> 2)), (unsigned char)((gg << 3) | (gg >> 2)), (unsigned char)((bb << 3) | (bb >> 2))};
                    o.write((const char *)px, 3);
                }
        }
    };
    auto dump = [&](int f) {
        auto r = events.equal_range(f);
        for (auto it = r.first; it != r.second; ++it) {
            const std::string &a = it->second;
            if (a.rfind("dump=", 0) != 0) continue;
            std::vector<uint8_t> m;
            read_ram("gpu.palette", 0x2000, m);
            read_ram("gpu.sprites", 0x2000, m);
            read_ram("gpu.tilemaps", 0x10000, m);
            read_ram("gpu.linetab", 0x2000, m);
            read_ram("gpu.chrram", 0x20000, m);
            read_ram("apu.regs", 0x2000, m);
            std::ofstream o(outdir + "/" + a.substr(5) + ".mem", std::ios::binary);
            o.write((const char *)m.data(), m.size());
        }
    };

    // reset
    top->clk = 0; top->rst_n = 0; top->dip = 0; top->rom_ack = 0; top->rom_valid = 0;
    top->rom_size = std::max<size_t>((load(argv[1]).size() + 3) & ~size_t(3), 0x100);
    if (getenv("DIP")) top->dip = strtoul(getenv("DIP"), 0, 0); else if (rom.size() >= 0x18) memcpy(&top->dip, &rom[0x14], 4);
    top->gfx_rdy = 0; top->gfx_valid = 0;
    apply(0);
    for (int i = 0; i < 4; i++) { top->clk = !top->clk; top->eval(); }
    top->rst_n = 1;

    std::string dbg;
    std::vector<int16_t> audio;
    std::ofstream apulog(outdir + "/apu.log");   // sound register writes, stamped with the next sample index
    int frame = 0;
    uint64_t cycle = 0;
    // SDRAM model state
    bool sd_busy = false;
    uint32_t sd_addr = 0;
    int sd_wait = 0, sd_beat = 0;
    int prev_vcount = 0;
    uint64_t limit = (uint64_t)(last + 2) * 850000;
    // graphics SDRAM model: one 32-bit read accepted per cycle, data 6 cycles later
    struct Pend { uint64_t due; uint32_t data; };
    std::deque<Pend> gq;
    uint64_t last_accept = 0;
    int win_max = 0, win_frame_max = 0;
    std::vector<int> win_hist;

    while (cycle < limit) {
        // drive inputs for this cycle (combinational responses computed from the current state)
        top->kb_stb = 0;
        if (!keys.empty() && (cycle & 15) == 0) {
            uint32_t k = keys.front(); keys.pop_front();
            top->kb_stb = 1; top->kb_down = (k >> 9) & 1; top->kb_ext = (k >> 8) & 1; top->kb_code = k & 0xFF;
        }
        top->ms_stb = 0;
        top->midi_stb = 0;
        if (!midi.empty() && (cycle & 1023) == 4) { top->midi_stb = 1; top->midi_byte = midi.front(); midi.pop_front(); }
        if (!mice.empty() && (cycle & 15) == 8) {   // as hps_io: PS/2 moves, Y positive upwards
            Mouse ms = mice.front(); mice.pop_front();
            auto clip = [](int v) { return v < -256 ? -256 : v > 255 ? 255 : v; };
            top->ms_stb = 1; top->ms_dx = clip(ms.dx) & 0x1FF; top->ms_dy = clip(-ms.dy) & 0x1FF;
            top->ms_btn = ms.b & 7; top->ms_wheel = ms.w & 0xFF;
        }
        top->rom_ack = 0; top->rom_valid = 0;
        if (!sd_busy && top->rom_req) {
            top->rom_ack = 1;
            sd_busy = true; sd_addr = top->rom_addr; sd_wait = 4; sd_beat = 0;
        } else if (sd_busy) {
            if (sd_wait > 0) sd_wait--;
            if (sd_wait == 0) {
                uint32_t a = sd_addr + sd_beat * 4, w = 0;
                if (a + 3 < rom.size()) memcpy(&w, &rom[a], 4);
                top->rom_valid = 1; top->rom_data = w;
                if (++sd_beat == 4) sd_busy = false;
                else sd_wait = 1;
            }
        }
        top->gfx_valid = 0;
        if (!gq.empty() && gq.front().due <= cycle) { top->gfx_valid = 1; top->gfx_data = gq.front().data; }
        top->gfx_rdy = (cycle - last_accept >= 1) && gq.size() < 16;
        top->eval();   // settle combinational request lines before sampling them
        bool accepted = top->gfx_req && top->gfx_rdy;
        uint32_t gaddr = top->gfx_addr;
        top->clk = 1; top->eval();
        if (top->dbg_valid) dbg += (char)top->dbg_char;
        if (top->audio_valid) { audio.push_back((int16_t)top->audio_l); audio.push_back((int16_t)top->audio_r); }
        if (top->dbg_apu_wr) {
            unsigned be = top->dbg_apu_be, lo = 0, n = 0;
            while (!((be >> lo) & 1)) lo++;
            while ((be >> (lo + n)) & 1) n++;
            uint32_t v = (uint32_t)(((uint64_t)top->dbg_apu_data >> (8 * lo)) & ((n == 4) ? 0xFFFFFFFFull : ((1ull << (8 * n)) - 1)));
            apulog << audio.size() / 2 << " " << ((top->dbg_apu_addr & ~3u) + lo) << " " << n << " " << v << "\n";
        }

        if (top->gfx_valid) gq.pop_front();
        if (accepted) {
            uint32_t w = 0, a = gaddr & ~3u;
            if (a + 3 < rom.size()) memcpy(&w, &rom[a], 4);
            gq.push_back({cycle + 6, w});
            last_accept = cycle;
        }
        static uint64_t npx = 0;
        if (getenv("TBDEBUG") && top->line_start && (cycle % 100000) < 7000) fprintf(stderr, "cycle %llu vcount %d px so far %llu win %d\n", (unsigned long long)cycle, (int)top->vcount, (unsigned long long)npx, (int)top->window_cycles);
        if (top->px_valid) npx++;
        if (top->px_valid) {
            int xx = top->px_x, yy = top->px_y;
            if (xx < 384 && yy < 288) { fb[yy * 384 + xx] = top->px_rgb; if (xx + 1 > fb_w) fb_w = xx + 1; if (yy + 1 > fb_h) fb_h = yy + 1; }
        }
        {
            static int prev_wc = -1;
            int wc = top->window_cycles;
            if (wc != prev_wc) { if (wc > win_frame_max) win_frame_max = wc; prev_wc = wc; }
        }
        top->clk = 0; top->eval();
        cycle++;
        int vc = top->vcount;
        if (vc == 0 && prev_vcount != 0) {
            // frame `frame` is over
            dump(frame);
            shot(frame);
            if (win_frame_max > win_max) win_max = win_frame_max;
            win_hist.push_back(win_frame_max);
            win_frame_max = 0; fb_w = 0; fb_h = 0;
            frame++;
            if (frame > last) break;
            apply(frame);
        }
        prev_vcount = vc;
    }
    printf("frames %d, cycles %llu, longest render window %d cycles\n", frame, (unsigned long long)cycle, win_max);
    {
        std::ofstream o(outdir + "/windows.txt");
        for (size_t i = 0; i < win_hist.size(); i++) o << i << " " << win_hist[i] << "\n";
    }
    {
        // 16-bit stereo WAV, like tools/run.js
        std::ofstream o(outdir + "/audio.wav", std::ios::binary);
        uint32_t bytes = audio.size() * 2, v32;
        uint16_t v16;
        o.write("RIFF", 4); v32 = 36 + bytes; o.write((char *)&v32, 4); o.write("WAVEfmt ", 8);
        v32 = 16; o.write((char *)&v32, 4); v16 = 1; o.write((char *)&v16, 2); v16 = 2; o.write((char *)&v16, 2);
        v32 = 48000; o.write((char *)&v32, 4); v32 = 192000; o.write((char *)&v32, 4); v16 = 4; o.write((char *)&v16, 2);
        v16 = 16; o.write((char *)&v16, 2); o.write("data", 4); o.write((char *)&bytes, 4);
        o.write((const char *)audio.data(), bytes);
    }
    apulog << "#samples " << audio.size() / 2 << "\n";
    apulog.close();
    std::string esc;
    for (char c : dbg) { if (c == '\n') esc += "\\n"; else esc += c; }
    printf("debug: \"%s\"\n", esc.c_str());
    top->final();
    delete top;
    return 0;
}
