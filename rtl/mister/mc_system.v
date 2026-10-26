// MONOCORE-1 system: the core with its SDRAM controller, ROM loader, scan-out and I/O, everything
// below the MiSTer framework (emu, in monocore.sv, only adds hps_io, the PLL and the pins).
// It is also the top level of the full-system simulation (tb/tb_system.cpp).
module mc_system #(
	parameter CART_CHIP_BITS = 23     // size of one cartridge Flash chip (8 MB); smaller in the tests
) (
	input  wire        clk,            // 50 MHz
	input  wire        clk2,           // 100 MHz, aligned with clk
	input  wire        reset,          // clk domain, active high (power-up, OSD reset)
	// ROM download (hps_io ioctl, 16-bit wide mode)
	input  wire        dl_active,
	input  wire        dl_wr,
	input  wire [26:0] dl_addr,
	input  wire [15:0] dl_data,
	output wire        dl_wait,
	// options
	input  wire        sd_rd_late,     // SDRAM read capture one cycle later
	input  wire [31:0] dip_osd,        // XORed with the ROM's default DIP switches
	// controls, MiSTer joystick layout: 0 right, 1 left, 2 down, 3 up, 4-9 buttons 1-6, 10 start, 11 coin
	input  wire [11:0] joy1,
	input  wire [11:0] joy2,
	input  wire [11:0] joy3,
	input  wire [11:0] joy4,
	input  wire [2:0]  in_sys,
	// analog inputs of pads 1-4 (hps_io): sticks X [7:0] Y [15:8] signed, paddle 0-255,
	// spinner [7:0] signed movement with [8] toggling at each update
	input  wire [63:0] joy_l_analog,
	input  wire [63:0] joy_r_analog,
	input  wire [31:0] paddle,
	input  wire [35:0] spinner,
	// keyboard (hps_io ps2_key): [10] toggles at each event, [9] pressed, [8] extended, [7:0] code
	input  wire [10:0] ps2_key,
	// mouse (hps_io ps2_mouse): [24] toggles at each packet, [23:16] Y, [15:8] X, [5] Y sign, [4] X sign,
	// [2:0] middle, right, left buttons; ps2_mouse_ext[7:0]: wheel move of the packet, positive when the
	// wheel is turned towards the user (checked on the DE10-Nano): negated for MOUSE_BTN (+ = away)
	input  wire [24:0] ps2_mouse,
	input  wire [15:0] ps2_mouse_ext,
	// cartridge on the USER port (see mc_cart.v): 0 CLK, 1 DI, 2 /CS0, 3 detection (grounded by
	// the cartridge), 4 DO, 5 /CS1, 6 unused. Open drain: user_out = 1 releases the line.
	input  wire [6:0]  user_in,
	output wire [6:0]  user_out,
	input  wire        cart_load,      // OSD: read the cartridge now
	input  wire        cart_auto,      // read it at start-up and when it is plugged in
	input  wire [1:0]  cart_speed,     // SPI clock: 3.125, 6.25, 12.5 MHz
	input  wire        cart_single,    // single-line reads (0x03) instead of dual
	output wire        cart_present,
	output wire        cart_busy,
	output wire        cart_error,
	// video
	output wire        ce_pix,
	output wire [7:0]  vga_r,
	output wire [7:0]  vga_g,
	output wire [7:0]  vga_b,
	output wire        vga_hs,
	output wire        vga_vs,
	output wire        vga_hblank,
	output wire        vga_vblank,
	output wire [1:0]  rotation,
	output wire [8:0]  width,
	output wire [9:0]  height,
	// sound
	output reg  [15:0] audio_l,
	output reg  [15:0] audio_r,
	// the NVRAM's copy to and from the SD card (mister/mc_nvsave.sv): its second port, and the core
	// kept in reset while it is loaded
	input  wire        nv_hold,
	input  wire        nv_b_en,
	input  wire [3:0]  nv_b_we,
	input  wire [12:0] nv_b_addr,
	input  wire [31:0] nv_b_wdata,
	output wire [31:0] nv_b_rdata,
	output wire        nv_written,
	// status
	output wire        running,        // ROM loaded and core out of reset
	output wire        dbg_valid,
	output wire [7:0]  dbg_char,
	output wire [9:0]  dbg_vcount,     // line period in progress (simulation)
	output wire [15:0] dbg_window,     // length of the last render window (simulation)
	// SDRAM
	output wire [12:0] sd_a,
	output wire [1:0]  sd_ba,
	output wire [15:0] sd_dq_o,
	output wire        sd_dq_oe,
	input  wire [15:0] sd_dq_i,
	output wire        sd_dqml,
	output wire        sd_dqmh,
	output wire        sd_ncs,
	output wire        sd_nras,
	output wire        sd_ncas,
	output wire        sd_nwe,
	output wire        sd_cke
);
	// ---------------------------------------------------------------- cartridge
	reg  [1:0]  det_s;
	reg  [19:0] det_cnt;
	reg         present;
	always @(posedge clk) begin                     // detection line, debounced (about 20 ms)
		det_s <= {det_s[0], !user_in[3]};
		if (det_s[1] == present) det_cnt <= 20'd0;
		else if (&det_cnt) present <= det_s[1];
		else det_cnt <= det_cnt + 20'd1;
	end
	initial begin det_s = 0; det_cnt = 0; present = 0; end
	assign cart_present = present;

	// when to read it: at start-up once the SD card has been idle (boot.rom download included) for
	// about 1.3 s, when it is plugged in, or from the OSD
	reg  [25:0] boot_cnt;
	reg         boot_done, present_d;
	wire        cart_start = cart_load || (cart_auto && present &&
	                         ((!boot_done && &boot_cnt) || (boot_done && !present_d)));
	always @(posedge clk) begin
		present_d <= present;
		if (dl_active) boot_cnt <= 26'd0;
		else if (!(&boot_cnt)) boot_cnt <= boot_cnt + 26'd1;
		if (&boot_cnt) boot_done <= 1'b1;
	end
	initial begin boot_cnt = 0; boot_done = 0; present_d = 0; end

	wire        c_sck, c_mosi, c_cs0_n, c_cs1_n, c_active, c_wr;
	wire [26:0] c_addr;
	wire [15:0] c_data;
	mc_cart #(.CHIP_BITS(CART_CHIP_BITS)) cart (
		.clk(clk), .start(cart_start), .speed(cart_speed), .single(cart_single), .hps_active(dl_active),
		.sck(c_sck), .mosi(c_mosi), .mosi_in(user_in[1]), .miso_in(user_in[4]), .cs0_n(c_cs0_n), .cs1_n(c_cs1_n),
		.dl_active(c_active), .dl_wr(c_wr), .dl_addr(c_addr), .dl_data(c_data), .dl_wait(w_pend),
		.busy(cart_busy), .error(cart_error));
	assign user_out = {1'b1, c_cs1_n, 1'b1, 1'b1, c_cs0_n, c_mosi, c_sck};

	// ---------------------------------------------------------------- ROM loader
	// Each 16-bit word of the download (SD card or cartridge) is written to the SDRAM (dl_wait holds
	// the stream meanwhile). The header gives the ROM size (0x0C) and the default DIP switches (0x14).
	wire        l_active = dl_active | c_active;
	wire        l_wr     = dl_active ? dl_wr : c_wr;
	wire [26:0] l_addr   = dl_active ? dl_addr : c_addr;
	wire [15:0] l_data   = dl_active ? dl_data : c_data;
	reg        loaded;
	reg [31:0] rom_size, dip_rom;
	reg        w_pend;
	reg [25:0] w_addr;
	reg [15:0] w_data;
	wire       sd_ready, w_ack;
	assign dl_wait = w_pend;
	always @(posedge clk) begin
		if (l_wr && l_active) begin
			w_pend <= 1'b1; w_addr <= l_addr[25:0]; w_data <= l_data;
			case (l_addr[26:0])
			27'h0C: rom_size[15:0]  <= l_data;
			27'h0E: rom_size[31:16] <= l_data;
			27'h14: dip_rom[15:0]   <= l_data;
			27'h16: dip_rom[31:16]  <= l_data;
			default: ;
			endcase
		end else if (w_ack) w_pend <= 1'b0;
		if (l_active) loaded <= 1'b0;
		else if (!w_pend && rom_size != 0) loaded <= 1'b1;
	end
	initial begin loaded = 0; rom_size = 0; dip_rom = 0; w_pend = 0; end

	// the core runs once a ROM is in the SDRAM and the controller is ready
	reg [3:0] rst_cnt;
	reg       core_rst_n;
	always @(posedge clk) begin
		if (reset || l_active || !loaded || !sd_ready || nv_hold) begin rst_cnt <= 0; core_rst_n <= 1'b0; end
		else if (rst_cnt != 4'd15) rst_cnt <= rst_cnt + 4'd1;
		else core_rst_n <= 1'b1;
	end
	initial begin rst_cnt = 0; core_rst_n = 0; end
	assign running = core_rst_n;

	// ---------------------------------------------------------------- core
	wire        rom_req, rom_ack, rom_valid, gfx_req, gfx_rdy, gfx_valid;
	wire [25:0] rom_addr, gfx_addr;
	wire [31:0] rom_data, gfx_data;
	wire [9:0]  vcount;
	assign dbg_vcount = vcount;
	wire [15:0] lcount;
	wire        px_valid;
	wire [8:0]  px_x;
	wire [9:0]  px_y;
	wire [14:0] px_rgb;
	wire        audio_valid;
	wire [15:0] a_l, a_r;
	// joystick: MiSTer order (right, left, down, up) to the console's (up, down, left, right)
	wire [11:0] p1 = {joy1[11:4], joy1[0], joy1[1], joy1[2], joy1[3]};
	wire [11:0] p2 = {joy2[11:4], joy2[0], joy2[1], joy2[2], joy2[3]};
	wire [11:0] p3 = {joy3[11:4], joy3[0], joy3[1], joy3[2], joy3[3]};
	wire [11:0] p4 = {joy4[11:4], joy4[0], joy4[1], joy4[2], joy4[3]};

	// spinners: the console sees a 16-bit position, the sum of the movements hps_io reports
	reg  [15:0] spin_pos [0:3];
	reg  [3:0]  spin_tog;
	integer si;
	always @(posedge clk) begin
		if (reset) begin
			for (si = 0; si < 4; si = si + 1) spin_pos[si] <= 16'd0;
			spin_tog <= {spinner[35], spinner[26], spinner[17], spinner[8]};
		end else begin
			for (si = 0; si < 4; si = si + 1)
				if (spinner[si * 9 + 8] != spin_tog[si]) begin
					spin_tog[si] <= spinner[si * 9 + 8];
					spin_pos[si] <= spin_pos[si] + {{8{spinner[si * 9 + 7]}}, spinner[si * 9 +: 8]};
				end
		end
	end
	wire [127:0] analog, paddles;
	genvar gi;
	generate for (gi = 0; gi < 4; gi = gi + 1) begin : pads
		assign analog[gi * 32 +: 32]  = {joy_r_analog[gi * 16 +: 16], joy_l_analog[gi * 16 +: 16]};
		assign paddles[gi * 32 +: 32] = {spin_pos[gi], 8'd0, paddle[gi * 8 +: 8]};
	end endgenerate

	// keyboard: one pulse per change of the ps2_key toggle bit
	reg kb_tog;
	always @(posedge clk) kb_tog <= ps2_key[10];
	wire kb_stb = ps2_key[10] != kb_tog && running;
	// mouse: one pulse per change of the ps2_mouse toggle bit
	reg ms_tog;
	always @(posedge clk) ms_tog <= ps2_mouse[24];
	wire ms_stb = ps2_mouse[24] != ms_tog && running;

	mc_core core (
		.clk(clk), .rst_n(core_rst_n),
		.rom_req(rom_req), .rom_addr(rom_addr), .rom_ack(rom_ack), .rom_valid(rom_valid), .rom_data(rom_data),
		.rom_size(rom_size),
		.gfx_req(gfx_req), .gfx_addr(gfx_addr), .gfx_rdy(gfx_rdy), .gfx_valid(gfx_valid), .gfx_data(gfx_data),
		.in_p1(p1), .in_p2(p2), .in_p3(p3), .in_p4(p4), .in_sys(in_sys), .dip(dip_rom ^ dip_osd),
		.in_analog(analog), .in_paddle(paddles),
		.kb_stb(kb_stb), .kb_down(ps2_key[9]), .kb_ext(ps2_key[8]), .kb_code(ps2_key[7:0]),
		.ms_stb(ms_stb), .ms_dx({ps2_mouse[4], ps2_mouse[15:8]}), .ms_dy({ps2_mouse[5], ps2_mouse[23:16]}),
		.ms_btn({ps2_mouse[2], ps2_mouse[1], ps2_mouse[0]}), .ms_wheel(8'd0 - ps2_mouse_ext[7:0]),
		.nv_b_en(nv_b_en), .nv_b_we(nv_b_we), .nv_b_addr(nv_b_addr), .nv_b_wdata(nv_b_wdata), .nv_b_rdata(nv_b_rdata),
		.nv_written(nv_written),
		.dbg_valid(dbg_valid), .dbg_char(dbg_char),
		.vcount(vcount), .vblank_pulse(), .line_start(),
		.px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_rgb(px_rgb), .window_cycles(dbg_window),
		.lcount(lcount), .height(height), .width(width), .rotation(rotation),
		.audio_valid(audio_valid), .audio_l(a_l), .audio_r(a_r),
		.dbg_apu_wr(), .dbg_apu_addr(), .dbg_apu_data(), .dbg_apu_be());

	// silent while the core is held in reset (ROM loading, OSD reset) and for 512 samples (10.7 ms)
	// after it starts: the previous game's sound registers keep playing until the start-up code
	// (sdk/crt0.S) clears them, a few microseconds later
	reg [9:0] mute;
	always @(posedge clk)
		if (!core_rst_n) begin audio_l <= 16'd0; audio_r <= 16'd0; mute <= 10'd512; end
		else if (audio_valid) begin
			if (mute != 0) begin mute <= mute - 10'd1; audio_l <= 16'd0; audio_r <= 16'd0; end
			else begin audio_l <= a_l; audio_r <= a_r; end
		end

	// ---------------------------------------------------------------- SDRAM
	mc_sdram sdram (
		.clk(clk), .clk2(clk2), .rst_n(core_rst_n || l_active || w_pend), .rd_late(sd_rd_late),
		.rom_req(rom_req), .rom_addr(rom_addr), .rom_ack(rom_ack), .rom_valid(rom_valid), .rom_data(rom_data),
		.gfx_req(gfx_req), .gfx_addr(gfx_addr), .gfx_rdy(gfx_rdy), .gfx_valid(gfx_valid), .gfx_data(gfx_data),
		.wr_req(w_pend), .wr_addr(w_addr), .wr_data(w_data), .wr_be(2'b11), .wr_ack(w_ack),
		.ready(sd_ready),
		.sd_a(sd_a), .sd_ba(sd_ba), .sd_dq_o(sd_dq_o), .sd_dq_oe(sd_dq_oe), .sd_dq_i(sd_dq_i),
		.sd_dqml(sd_dqml), .sd_dqmh(sd_dqmh), .sd_ncs(sd_ncs), .sd_nras(sd_nras), .sd_ncas(sd_ncas),
		.sd_nwe(sd_nwe), .sd_cke(sd_cke));

	// ---------------------------------------------------------------- video
	mc_scanout scanout (
		.clk(clk), .rst_n(core_rst_n),
		.lcount(lcount), .vcount(vcount), .height(height), .width(width),
		.px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_rgb(px_rgb),
		.ce_pix(ce_pix), .r(vga_r), .g(vga_g), .b(vga_b), .hs(vga_hs), .vs(vga_vs),
		.hblank(vga_hblank), .vblank(vga_vblank));
endmodule
