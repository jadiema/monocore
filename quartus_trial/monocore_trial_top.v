// MONOCORE-1 synthesis trial: wraps mc_core so that Quartus can compile it for the Cyclone V of
// the DE10-Nano and report resources and maximum frequency. This bitstream does nothing useful
// on the board: do not load it. All core inputs come from a shift register fed by one pin, and all
// core outputs are folded into 8 registered output pins, so that no logic is optimized away.
module monocore_trial_top (
	input  wire       clk,         // constrained at 50 MHz in monocore_trial.sdc
	input  wire       rst_n_pin,
	input  wire       sin,
	output reg  [7:0] sout
);
	// reset synchronizer
	reg [1:0] rs;
	always @(posedge clk or negedge rst_n_pin)
		if (!rst_n_pin) rs <= 2'b00; else rs <= {rs[0], 1'b1};
	wire rst_n = rs[1];

	// pseudo inputs
	reg [159:0] sh;
	always @(posedge clk) sh <= {sh[158:0], sin ^ sh[159] ^ sh[120]};

	wire        rom_req, gfx_req, px_valid, audio_valid, dbg_valid, vblank_pulse, line_start, dbg_apu_wr;
	wire [25:0] rom_addr, gfx_addr;
	wire [8:0]  px_x;
	wire [9:0]  px_y, vcount;
	wire [14:0] px_rgb;
	wire [15:0] window_cycles, audio_l, audio_r;
	wire [7:0]  dbg_char;
	wire [12:0] dbg_apu_addr;
	wire [31:0] dbg_apu_data;
	wire [3:0]  dbg_apu_be;

	mc_core core (
		.clk(clk), .rst_n(rst_n),
		.rom_req(rom_req), .rom_addr(rom_addr), .rom_ack(rom_req & sh[64]), .rom_valid(sh[65]),
		.rom_data(sh[127:96]), .rom_size({6'd0, sh[153:128]}),
		.gfx_req(gfx_req), .gfx_addr(gfx_addr), .gfx_rdy(sh[66]), .gfx_valid(sh[67]), .gfx_data(sh[95:64]),
		.in_p1(sh[11:0]), .in_p2(sh[23:12]), .in_sys(sh[26:24]), .dip(sh[63:32]),
		.dbg_valid(dbg_valid), .dbg_char(dbg_char),
		.vcount(vcount), .vblank_pulse(vblank_pulse), .line_start(line_start),
		.px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_rgb(px_rgb), .window_cycles(window_cycles),
		.audio_valid(audio_valid), .audio_l(audio_l), .audio_r(audio_r),
		.dbg_apu_wr(dbg_apu_wr), .dbg_apu_addr(dbg_apu_addr), .dbg_apu_data(dbg_apu_data), .dbg_apu_be(dbg_apu_be));

	// fold every output into 8 bits
	wire [255:0] all = {rom_req, rom_addr, gfx_req, gfx_addr, px_valid, px_x, px_y, px_rgb, window_cycles,
	                    audio_valid, audio_l, audio_r, dbg_valid, dbg_char, vcount, vblank_pulse, line_start,
	                    dbg_apu_wr, dbg_apu_addr, dbg_apu_data, dbg_apu_be, 34'd0};
	reg [255:0] all_r;
	integer i;
	reg [7:0] f;
	always @(*) begin
		f = 8'd0;
		for (i = 0; i < 32; i = i + 1) f = f ^ all_r[8 * i +: 8];
	end
	always @(posedge clk) begin
		all_r <= all;
		sout <= f;
	end
endmodule
