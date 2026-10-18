// MONOCORE-1 system: the core with its SDRAM controller, ROM loader, scan-out and I/O, everything
// below the MiSTer framework (emu, in monocore.sv, only adds hps_io, the PLL and the pins).
// It is also the top level of the full-system simulation (tb/tb_system.cpp).
module mc_system (
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
	input  wire [2:0]  in_sys,
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
	// ---------------------------------------------------------------- ROM loader
	// Each 16-bit word of the download is written to the SDRAM (dl_wait holds the stream meanwhile).
	// The header gives the ROM size (0x0C) and the default DIP switches (0x14).
	reg        loaded;
	reg [31:0] rom_size, dip_rom;
	reg        w_pend;
	reg [25:0] w_addr;
	reg [15:0] w_data;
	wire       sd_ready, w_ack;
	assign dl_wait = w_pend;
	always @(posedge clk) begin
		if (dl_wr && dl_active) begin
			w_pend <= 1'b1; w_addr <= dl_addr[25:0]; w_data <= dl_data;
			case (dl_addr[26:0])
			27'h0C: rom_size[15:0]  <= dl_data;
			27'h0E: rom_size[31:16] <= dl_data;
			27'h14: dip_rom[15:0]   <= dl_data;
			27'h16: dip_rom[31:16]  <= dl_data;
			default: ;
			endcase
		end else if (w_ack) w_pend <= 1'b0;
		if (dl_active) loaded <= 1'b0;
		else if (!w_pend && rom_size != 0) loaded <= 1'b1;
	end
	initial begin loaded = 0; rom_size = 0; dip_rom = 0; w_pend = 0; end

	// the core runs once a ROM is in the SDRAM and the controller is ready
	reg [3:0] rst_cnt;
	reg       core_rst_n;
	always @(posedge clk) begin
		if (reset || dl_active || !loaded || !sd_ready) begin rst_cnt <= 0; core_rst_n <= 1'b0; end
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

	mc_core core (
		.clk(clk), .rst_n(core_rst_n),
		.rom_req(rom_req), .rom_addr(rom_addr), .rom_ack(rom_ack), .rom_valid(rom_valid), .rom_data(rom_data),
		.rom_size(rom_size),
		.gfx_req(gfx_req), .gfx_addr(gfx_addr), .gfx_rdy(gfx_rdy), .gfx_valid(gfx_valid), .gfx_data(gfx_data),
		.in_p1(p1), .in_p2(p2), .in_sys(in_sys), .dip(dip_rom ^ dip_osd),
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
		.clk(clk), .clk2(clk2), .rst_n(core_rst_n || dl_active || w_pend), .rd_late(sd_rd_late),
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
