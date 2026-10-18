// MONOCORE-1 full-system testbench (Verilator): mc_system (core, SDRAM controller, loader, scan-out)
// with a model of the SDRAM chip. The ROM is loaded through the download port like hps_io does,
// pictures are taken from the video output (as the framework's scaler would receive them).
//
// usage: Vmc_system rom.mnc outdir [script]     (script syntax of tools/run.js: frame:p1=hex, frame:shot=name)
//   also p3= p4=, a1=..a4= (ANALOG_Pn: right stick 31:16, left 15:0), d1=..d4= (paddle), s1=..s4= (spinner
//   movement, signed 8 bits), kd= / ku= (key down / up through ps2_key, one event per frame)
//   cart=1 / cart=0 (cartridge plugged / removed), cartload (OSD "Lire la cartouche").
//   The cartridge holds the file named by $CART: two SPI Flash chips of 8 MB ($CART_CHIP_BITS to change it),
//   modelled below (Read Data 0x03 and Fast Read Dual Output 0x3B). $CART_SPEED = 0-2, $CART_SINGLE = 1.
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
#include <deque>
#include <cstring>

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
    std::deque<uint32_t> keys;
    // ---------------------------------------------------------------- cartridge: two SPI Flash chips
    std::vector<uint8_t> cart;
    if (getenv("CART")) cart = load(getenv("CART"));
    const int chip_bits = getenv("CART_CHIP_BITS") ? atoi(getenv("CART_CHIP_BITS")) : 23;
    struct Flash { int bits = 0, op = 0, out_bits = 0; uint32_t addr = 0, shift = 0; bool drive = false; int do_v = 1, di_v = 1; uint8_t cur = 0; int nib = 0; };
    Flash fl[2];
    bool cart_in = false; int cart_load_cycles = 0;
    int prev_sck = 0, prev_cs[2] = {1, 1};
    auto flash_byte = [&](int c, uint32_t a) -> uint8_t {
        size_t o = ((size_t)c << chip_bits) + (a & ((1u << chip_bits) - 1));
        return o < cart.size() ? cart[o] : 0xFF;
    };
    // one clk cycle: the lines as the FPGA leaves them (open drain: 1 = released, pulled up)
    auto flash_step = [&]() {
        uint32_t uo = top->user_out;
        int sck = uo & 1, di = (uo >> 1) & 1, cs[2] = {(int)((uo >> 2) & 1), (int)((uo >> 5) & 1)};
        for (int c = 0; c < 2; c++) {
            Flash &f = fl[c];
            if (!cart_in) { f.drive = false; prev_cs[c] = cs[c]; continue; }
            if (cs[c]) { f.drive = false; f.bits = 0; prev_cs[c] = 1; continue; }
            if (prev_cs[c]) { f.bits = 0; f.op = 0; f.shift = 0; f.drive = false; }
            prev_cs[c] = 0;
            int line_di = f.drive && f.op == 0x3B ? f.di_v : di;
            if (sck && !prev_sck) {                       // rising edge: command, address, dummy
                if (f.bits < 32) { f.shift = (f.shift << 1) | line_di; if (++f.bits == 8) f.op = f.shift & 0xFF; if (f.bits == 32) f.addr = f.shift & 0xFFFFFF; }
                else f.bits++;
            }
            if (!sck && prev_sck) {                       // falling edge: data out
                int start = f.op == 0x3B ? 40 : 32;
                if (f.bits >= start && (f.op == 0x03 || f.op == 0x3B)) {
                    if (f.op == 0x03) {
                        int k = (f.bits - 32) & 7;
                        if (k == 0) { f.cur = flash_byte(c, f.addr); f.addr++; }
                        f.do_v = (f.cur >> (7 - k)) & 1; f.drive = true;
                    } else {
                        int k = (f.bits - 40) & 3;
                        if (k == 0) { f.cur = flash_byte(c, f.addr); f.addr++; }
                        f.do_v = (f.cur >> (7 - 2 * k)) & 1; f.di_v = (f.cur >> (6 - 2 * k)) & 1; f.drive = true;
                    }
                }
            }
        }
        prev_sck = sck;
        uint32_t ui = uo | 0x7F;                          // released lines read 1
        ui &= ~(uint32_t)0x02; ui |= di << 1;
        for (int c = 0; c < 2; c++) if (fl[c].drive) {
            ui = (ui & ~0x10u) | (fl[c].do_v << 4);
            if (fl[c].op == 0x3B) ui = (ui & ~0x02u) | ((fl[c].di_v & di) << 1);
        }
        if (cart_in) ui &= ~0x08u;                        // detection: grounded by the cartridge
        top->user_in = ui & 0x7F;
        top->cart_load = cart_load_cycles > 0; if (cart_load_cycles > 0) cart_load_cycles--;
    };
    top->cart_auto = 1;
    top->cart_speed = getenv("CART_SPEED") ? atoi(getenv("CART_SPEED")) : 0;
    top->cart_single = getenv("CART_SINGLE") ? 1 : 0;
    top->user_in = 0x7F;
    auto apply = [&](int f) {
        if (!keys.empty()) {                       // one key event per frame, as ps2_key holds one
            uint32_t k = keys.front(); keys.pop_front();
            top->ps2_key = ((~top->ps2_key) & 0x400) | (k & 0x3FF);
        }
        auto r = events.equal_range(f);
        for (auto it = r.first; it != r.second; ++it) {
            const std::string &a = it->second;
            if (a.rfind("load=", 0) == 0) next_rom = a.substr(5);
            if (a.rfind("p1=", 0) == 0) top->joy1 = joy(strtoul(a.c_str() + 3, 0, 16));
            else if (a.rfind("p2=", 0) == 0) top->joy2 = joy(strtoul(a.c_str() + 3, 0, 16));
            else if (a.rfind("sys=", 0) == 0) top->in_sys = strtoul(a.c_str() + 4, 0, 16);
            else if (a.rfind("p3=", 0) == 0) top->joy3 = joy(strtoul(a.c_str() + 3, 0, 16));
            else if (a.rfind("p4=", 0) == 0) top->joy4 = joy(strtoul(a.c_str() + 3, 0, 16));
            else if (a.size() > 3 && a[2] == '=' && a[1] >= '1' && a[1] <= '4' && strchr("adsk", a[0])) {
                int n = a[1] - '1'; uint64_t v = strtoul(a.c_str() + 3, 0, 16);
                if (a[0] == 'a') {
                    top->joy_l_analog = (top->joy_l_analog & ~(0xFFFFull << (16 * n))) | ((v & 0xFFFF) << (16 * n));
                    top->joy_r_analog = (top->joy_r_analog & ~(0xFFFFull << (16 * n))) | (((v >> 16) & 0xFFFF) << (16 * n));
                } else if (a[0] == 'd') top->paddle = (top->paddle & ~(0xFFu << (8 * n))) | ((uint32_t)(v & 0xFF) << (8 * n));
                else if (a[0] == 's') {
                    uint64_t sp = top->spinner, t = (~sp >> (9 * n + 8)) & 1;
                    sp = (sp & ~(0x1FFull << (9 * n))) | ((t << 8 | (v & 0xFF)) << (9 * n));
                    top->spinner = sp;
                }
            }
            else if (a == "cart=1") { cart_in = true; fprintf(stderr, "cartridge plugged at frame %d\n", f); }
            else if (a == "cart=0") cart_in = false;
            else if (a == "cartload") cart_load_cycles = 1;
            else if (a.rfind("kd=", 0) == 0) keys.push_back(0x200 | (strtoul(a.c_str() + 3, 0, 16) & 0x1FF));
            else if (a.rfind("ku=", 0) == 0) keys.push_back(strtoul(a.c_str() + 3, 0, 16) & 0x1FF);
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
            flash_step();
            { static int pb = 0; if (top->cart_busy != pb) { fprintf(stderr, "cartridge %s at clk cycle %llu%s\n", top->cart_busy ? "read starts" : "read ends", (unsigned long long)slow, (!top->cart_busy && top->cart_error) ? " (error)" : ""); pb = top->cart_busy; } }
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
