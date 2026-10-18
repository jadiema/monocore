// MONOCORE-1 full-system testbench (Verilator): mc_system (core, SDRAM controller, loader, scan-out)
// with a model of the SDRAM chip. The ROM is loaded through the download port like hps_io does,
// pictures are taken from the video output (as the framework's scaler would receive them).
//
// usage: Vmc_system rom.mnc outdir [script]     (script syntax of tools/run.js: frame:p1=hex, frame:shot=name)
// frame:load=path loads a second ROM while the first one runs, like choosing another file in the OSD.
// env: RDLATE=1 selects the later read capture; SDLAT=n adds n cycles to the chip's read latency
//
// SDRAM model: 4 banks x 8192 rows x 512 columns x 16 bits, CAS latency 2, bursts of 2 for reads,
// single writes. A command is the state of the pins after a clk2 edge; for a READ seen after edge k,
// the controller must find the first halfword in its input register at edge k+3 and the second at
// k+4 (the half-cycle shift of the real SDRAM_CLK). The model checks tRCD, tRP, tRAS, tRC, tRRD, the
// refresh interval, reads of closed or wrong rows, and the byte masks of boards where DQM follows A11/A12.
#include "Vmc_system.h"
#include "verilated.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <map>
#include <sstream>
#include <string>
#include <vector>

static std::vector<uint8_t> load(const char *path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); }
    return std::vector<uint8_t>((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
}

struct Sdram {
    std::vector<uint16_t> mem = std::vector<uint16_t>(16u << 20, 0xA5A5);   // 32 MB
    bool open[4] = {}; int row[4] = {};
    int64_t t_act[4], t_pre[4], t_col[4], last_act = -100, last_ref = -1, init_ref = 0;
    bool mode_set = false;
    int errors = 0;
    std::map<int64_t, uint16_t> drive;     // value on DQ before edge k (low halfword, high halfword)
    int extra = 0;
    int64_t max_ref_gap = 0;
    Sdram() { for (int b = 0; b < 4; b++) { t_act[b] = -100; t_pre[b] = -100; t_col[b] = -100; } }
    void err(int64_t k, const char *what, int b) {
        if (errors < 20) fprintf(stderr, "SDRAM error at clk2 cycle %lld: %s (bank %d)\n", (long long)k, what, b);
        errors++;
    }
    uint32_t idx(int b, int r, int c) { return ((uint32_t)r << 11) | ((uint32_t)b << 9) | (uint32_t)c; }
    // pins after edge k
    void edge(int64_t k, int nras, int ncas, int nwe, int a, int ba, int dq_o, int dq_oe, int dqm, int prev_dqm) {
        int cmd = (nras << 2) | (ncas << 1) | nwe;
        switch (cmd) {
        case 3: // ACTIVE
            if (open[ba]) err(k, "ACTIVE on an open bank", ba);
            if (k - t_pre[ba] < 2) err(k, "tRP", ba);
            if (k - t_act[ba] < 7) err(k, "tRC", ba);
            if (k - last_act < 2) err(k, "tRRD", ba);
            open[ba] = true; row[ba] = a & 0x1FFF; t_act[ba] = k; last_act = k;
            break;
        case 2: // PRECHARGE
            for (int b = 0; b < 4; b++) {
                if (!((a >> 10) & 1) && b != ba) continue;
                if (open[b] && k - t_act[b] < 5) err(k, "tRAS", b);
                if (open[b] && k - t_col[b] < 2) err(k, "precharge too close to READ/WRITE", b);
                open[b] = false; t_pre[b] = k;
            }
            break;
        case 1: // REFRESH
            for (int b = 0; b < 4; b++) {
                if (open[b]) err(k, "REFRESH with an open bank", b);
                if (k - t_pre[b] < 2) err(k, "tRP before REFRESH", b);
            }
            if (mode_set && last_ref >= 0 && k - last_ref > max_ref_gap) max_ref_gap = k - last_ref;
            last_ref = k;
            break;
        case 0: mode_set = true; break;
        case 5: // READ
        case 4: { // WRITE
            if (!mode_set) { err(k, "access before LOAD MODE", ba); break; }
            if (!open[ba]) { err(k, "access to a closed bank", ba); break; }
            if (k - t_act[ba] < 2) err(k, "tRCD", ba);
            t_col[ba] = k;
            int c = a & 0x1FF;
            if (cmd == 5) {
                if (dqm != 0) err(k, "DQM set on READ", ba);
                uint16_t lo = mem[idx(ba, row[ba], c)], hi = mem[idx(ba, row[ba], c ^ 1)];
                drive[k + 3 + extra] = lo;
                drive[k + 4 + extra] = hi;
                pending_hi_mask_check = k + 1;
            } else {
                if (!dq_oe) err(k, "WRITE without data", ba);
                uint16_t &m = mem[idx(ba, row[ba], c)];
                if (!(dqm & 1)) m = (m & 0xFF00) | (dq_o & 0x00FF);
                if (!(dqm & 2)) m = (m & 0x00FF) | (dq_o & 0xFF00);
            }
            break;
        }
        default: break;
        }
        // DQM at the cycle after a READ masks its second halfword (read DQM latency 2)
        if (pending_hi_mask_check == k) {
            if (dqm != 0) err(k, "DQM high one cycle after READ (second halfword masked on A11/A12 boards)", ba);
            pending_hi_mask_check = -1;
        }
        (void)prev_dqm;
    }
    int64_t pending_hi_mask_check = -1;
    // value the controller's input register samples at edge k
    uint16_t sample(int64_t k) {
        auto it = drive.find(k);
        if (it == drive.end()) return 0xDEAD;
        uint16_t v = it->second; drive.erase(it); return v;
    }
};

int main(int argc, char **argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s rom.mnc outdir [script]\n", argv[0]); return 1; }
    VerilatedContext *ctx = new VerilatedContext;
    ctx->commandArgs(argc, argv);
    Vmc_system *top = new Vmc_system(ctx);
    std::vector<uint8_t> rom = load(argv[1]);
    if (rom.size() & 1) rom.push_back(0);
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
    Sdram sd;
    if (getenv("SDLAT")) sd.extra = atoi(getenv("SDLAT"));

    // console input layout (bit 0 up, 1 down, 2 left, 3 right) to the MiSTer joystick layout
    auto joy = [](uint32_t c) { return (c & 0xFF0) | ((c & 1) << 3) | ((c & 2) << 1) | ((c & 4) >> 1) | ((c & 8) >> 3); };
    std::string next_rom;
    auto apply = [&](int f) {
        auto r = events.equal_range(f);
        for (auto it = r.first; it != r.second; ++it) {
            const std::string &a = it->second;
            if (a.rfind("load=", 0) == 0) next_rom = a.substr(5);
            if (a.rfind("p1=", 0) == 0) top->joy1 = joy(strtoul(a.c_str() + 3, 0, 16));
            else if (a.rfind("p2=", 0) == 0) top->joy2 = joy(strtoul(a.c_str() + 3, 0, 16));
            else if (a.rfind("sys=", 0) == 0) top->in_sys = strtoul(a.c_str() + 4, 0, 16);
        }
    };
    // picture from the video output
    std::vector<uint32_t> fb(384 * 288, 0);
    int fb_w = 0, fb_h = 0, px = 0;
    bool prev_hb = true;
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
                    unsigned char p[3] = {(unsigned char)(c >> 16), (unsigned char)(c >> 8), (unsigned char)c};
                    o.write((const char *)p, 3);
                }
        }
    };

    top->clk = 0; top->clk2 = 0; top->reset = 1; top->dl_active = 0; top->dl_wr = 0;
    top->sd_rd_late = getenv("RDLATE") ? 1 : 0; top->dip_osd = 0; top->joy1 = 0; top->joy2 = 0; top->in_sys = 0;
    top->sd_dq_i = 0;
    top->eval();

    int64_t k = 0;               // clk2 edge index
    std::string dbg;
    // loader state (clk domain)
    size_t dl_pos = 0;
    int dl_state = 0, dl_gap = 0;   // 0 wait for SDRAM init, 1 streaming, 2 done
    int frame = 0, prev_vc = -1;
    bool peak_armed = true;       // false from a load request until the core is held in reset
    int audio_peak = 0;            // largest |sample| while the last loaded ROM runs
    bool started = false;
    int win_max = 0, win_frame_max = 0;
    std::vector<int> win_hist;
    uint64_t slow = 0;
    int64_t limit = (int64_t)(last + 2) * 1700000 + 4000000;
    while (k < limit) {
        // rising clk2 edge (and clk edge on even k)
        bool slow_edge = (k & 1) == 0;
        top->clk2 = 1;
        top->clk = slow_edge ? 1 : 0;
        top->sd_dq_i = sd.sample(k);
        top->eval();
        sd.edge(k, top->sd_nras, top->sd_ncas, top->sd_nwe, top->sd_a, top->sd_ba, top->sd_dq_o, top->sd_dq_oe,
                (top->sd_dqmh << 1) | top->sd_dqml, 0);
        if (slow_edge) {
            slow++;
            static bool hold = getenv("HOLD_RESET") != nullptr;   // reset held during the download, as MiSTer does for boot.rom
            if (!hold && slow == 20) top->reset = 0;
            if (hold && dl_state == 2 && top->reset) { static int w = 0; if (++w == 1000) top->reset = 0; }
            // loader: one 16-bit word per write, waiting for dl_wait
            top->dl_wr = 0;
            if (!next_rom.empty() && dl_state == 2) {
                rom = load(next_rom.c_str());
                if (rom.size() & 1) rom.push_back(0);
                fprintf(stderr, "loading %s at frame %d\n", next_rom.c_str(), frame);
                next_rom.clear(); audio_peak = 0; peak_armed = false; dl_pos = 0; dl_state = 1; top->dl_active = 1;
            }
            else if (dl_state == 0 && slow > 30000) { dl_state = 1; top->dl_active = 1; }
            else if (dl_state == 1) {
                if (dl_gap > 0) dl_gap--;
                else if (!top->dl_wait) {
                    if (dl_pos < rom.size()) {
                        top->dl_wr = 1; top->dl_addr = dl_pos; top->dl_data = rom[dl_pos] | (rom[dl_pos + 1] << 8);
                        dl_pos += 2; dl_gap = 3;
                    } else { dl_state = 2; top->dl_active = 0; }
                }
            }
            if (top->dbg_valid) dbg += (char)top->dbg_char;
            if (!top->running) peak_armed = true;
            if (top->running && peak_armed) { int a = abs((int16_t)top->audio_l), b = abs((int16_t)top->audio_r); if (a > audio_peak) audio_peak = a; if (b > audio_peak) audio_peak = b; }
            if (top->running && !started) { started = true; apply(0); fprintf(stderr, "core started at clk cycle %llu\n", (unsigned long long)slow); }
            // picture: pixels of the visible area, in order
            if (started && top->ce_pix) {
                bool hb = top->vga_hblank, vb = top->vga_vblank;
                if (!hb && prev_hb) px = 0;
                if (!hb && !vb) {
                    int yy = (int)top->dbg_vcount - 1, xx = px;
                    if (xx < 384 && yy >= 0 && yy < 288) {
                        fb[yy * 384 + xx] = (top->vga_r << 16) | (top->vga_g << 8) | top->vga_b;
                        if (xx + 1 > fb_w) fb_w = xx + 1;
                        if (yy + 1 > fb_h) fb_h = yy + 1;
                    }
                    px++;
                }
                prev_hb = hb;
            }
            if (started) {
                if ((int)top->dbg_window > win_frame_max) win_frame_max = top->dbg_window;
                int vc = top->dbg_vcount;
                if (vc == 0 && prev_vc > 0) {
                    shot(frame);
                    win_hist.push_back(win_frame_max);
                    if (win_frame_max > win_max) win_max = win_frame_max;
                    win_frame_max = 0;
                    fb_w = 0; fb_h = 0;
                    frame++;
                    if (frame > last) break;
                    apply(frame);
                }
                prev_vc = vc;
            }
        }
        // falling clk2 edge
        top->clk2 = 0;
        top->eval();
        k++;
    }
    std::string esc;
    for (char c : dbg) { if (c == '\n') esc += "\\n"; else esc += c; }
    printf("frames %d, clk cycles %llu, SDRAM errors %d, longest refresh gap %lld clk2 cycles, longest render window %d cycles\n",
           frame, (unsigned long long)slow, sd.errors, (long long)sd.max_ref_gap, win_max);
    {
        std::ofstream o(outdir + "/windows.txt");
        for (size_t i = 0; i < win_hist.size(); i++) o << i << " " << win_hist[i] << "\n";
    }
    printf("audio peak while the loaded ROM runs: %d\n", audio_peak);
    printf("debug: \"%s\"\n", esc.c_str());
    top->final();
    delete top;
    return sd.errors ? 2 : 0;
}
