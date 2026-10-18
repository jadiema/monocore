// MONOCORE-1 SDRAM controller for the MiSTer SDRAM board (32 MB, 16-bit, 4 banks, 8192 rows of 512
// columns), clocked at 100 MHz (clk2) next to a 50 MHz core (clk). Both clocks come from the same PLL
// with aligned rising edges: every other clk2 edge is a clk edge.
//
// Ports, all on the clk (50 MHz) side, as the core expects them (rtl/mc_core.v):
//   ROM   : rom_req held with rom_addr (16-byte aligned) until rom_ack, then 4 beats rom_valid/rom_data
//   GFX   : one 32-bit read accepted when gfx_req && gfx_rdy, words returned in order (gfx_valid)
//   write : wr_req held with wr_addr/wr_data/wr_be (one 16-bit halfword) until wr_ack (ROM loading)
//
// Inside, the clk2 logic samples the clk-side signals and updates the clk-side outputs only on the clk2
// edges that coincide with clk edges (s_en), so the interface behaves like clk registers.
//
// Scheduling: one request at a time, in arrival order per port, commands pipelined (the next request's
// ACTIVE or PRECHARGE goes out while the previous reads return). Rows stay open (one per bank) until a
// different row of the same bank or a refresh needs the bank. Reads use bursts of 2 (one 32-bit word);
// a ROM line is 4 reads of the same row, back to back. Priority: refresh, writes, graphics, ROM; after
// 8 graphics words in a row, a waiting ROM line goes first, so that the CPU and sound are never starved.
//
// Address mapping (byte address): column a[9:1], bank a[11:10], row a[24:12]. Rows 0-2047 (the first
// 8 MB) have row bits 12-11 at zero, which matters on boards where DQML/DQMH follow A11/A12 (see below).
module mc_sdram (
	input  wire        clk,          // 50 MHz
	input  wire        clk2,         // 100 MHz, rising edges aligned with clk
	input  wire        rst_n,        // clk domain
	input  wire        rd_late,      // read capture one clk2 cycle later (board timing option)
	// ROM line port
	input  wire        rom_req,
	input  wire [25:0] rom_addr,
	output reg         rom_ack,
	output reg         rom_valid,
	output reg  [31:0] rom_data,
	// graphics port
	input  wire        gfx_req,
	input  wire [25:0] gfx_addr,
	output reg         gfx_rdy,
	output reg         gfx_valid,
	output reg  [31:0] gfx_data,
	// write port (16-bit)
	input  wire        wr_req,
	input  wire [25:0] wr_addr,
	input  wire [15:0] wr_data,
	input  wire [1:0]  wr_be,
	output reg         wr_ack,
	output reg         ready,        // initialisation done
	// SDRAM pins (DQ split for the top level: dq_o/dq_oe out, dq_i in)
	output reg  [12:0] sd_a,
	output reg  [1:0]  sd_ba,
	output reg  [15:0] sd_dq_o,
	output reg         sd_dq_oe,
	input  wire [15:0] sd_dq_i,
	output wire        sd_dqml,
	output wire        sd_dqmh,
	output wire        sd_ncs,
	output wire        sd_nras,
	output wire        sd_ncas,
	output wire        sd_nwe,
	output wire        sd_cke
);
	// ---------------------------------------------------------------- clk / clk2 phase
	reg s_tog;
	always @(posedge clk) s_tog <= !s_tog;
	reg f1, f2;
	always @(posedge clk2) begin f1 <= s_tog; f2 <= f1; end
	wire s_en = f1 ^ f2;          // the clk2 edge ending this cycle is also a clk edge

	reg rst_f1, rst_f;            // reset, sampled in the clk2 domain
	always @(posedge clk2) begin rst_f1 <= !rst_n; rst_f <= rst_f1; end

	// ---------------------------------------------------------------- commands
	localparam C_NOP = 3'b111, C_ACT = 3'b011, C_READ = 3'b101, C_WRITE = 3'b100,
	           C_PRE = 3'b010, C_REF = 3'b001, C_MODE = 3'b000;
	// burst 2, sequential, CAS latency 2, single-location writes
	localparam [12:0] MODE = {3'b000, 1'b1, 2'b00, 3'b010, 1'b0, 3'b001};
	reg [2:0] cmd;
	assign {sd_nras, sd_ncas, sd_nwe} = cmd;
	assign sd_ncs = 1'b0;
	assign sd_cke = 1'b1;
	// byte masks: carried on A12/A11 during column commands (both wirings of the board work)
	assign {sd_dqmh, sd_dqml} = sd_a[12:11];

	// ---------------------------------------------------------------- clk-side requests (sampled on s_en)
	// graphics address queue
	reg  [24:0] gq [0:7];
	reg  [2:0]  gq_wp, gq_rp;
	reg  [3:0]  gq_n;
	// pending ROM line and write
	reg         rp_v, wp_v;
	reg  [24:0] rp_a, wp_a;
	reg  [15:0] wp_d;
	reg  [1:0]  wp_be;
	// read data queues back to the clk side
	reg  [31:0] gd [0:3];
	reg  [1:0]  gd_wp, gd_rp;
	reg  [2:0]  gd_n;
	reg  [31:0] rd [0:3];
	reg  [1:0]  rd_wp, rd_rp;
	reg  [2:0]  rd_n;
	wire        gd_push, rd_push;     // from the read return pipeline
	wire [31:0] word_in;
	wire        gq_pop;               // the scheduler takes the head of the graphics queue
	wire        rp_take, wp_take;

	always @(posedge clk2) begin
		if (rst_f) begin
			gq_wp <= 0; gq_n <= 0; rp_v <= 0; wp_v <= 0;
			rom_ack <= 0; wr_ack <= 0; gfx_rdy <= 0; rom_valid <= 0; gfx_valid <= 0;
			gd_rp <= 0; rd_rp <= 0;
		end else begin
			if (s_en) begin
				// requests (an ack is never followed by a second accept of the same, still held request)
				if (gfx_req && gfx_rdy) begin gq[gq_wp] <= gfx_addr[24:0]; gq_wp <= gq_wp + 3'd1; end
				rom_ack <= 1'b0; wr_ack <= 1'b0;
				if (rom_req && !rom_ack && !rp_v) begin rp_v <= 1'b1; rp_a <= {rom_addr[24:4], 4'd0}; rom_ack <= 1'b1; end
				if (wr_req && !wr_ack && !wp_v) begin wp_v <= 1'b1; wp_a <= wr_addr[24:0]; wp_d <= wr_data; wp_be <= wr_be; wr_ack <= 1'b1; end
				// read data
				rom_valid <= rd_n != 0;
				if (rd_n != 0) begin rom_data <= rd[rd_rp]; rd_rp <= rd_rp + 2'd1; end
				gfx_valid <= gd_n != 0;
				if (gd_n != 0) begin gfx_data <= gd[gd_rp]; gd_rp <= gd_rp + 2'd1; end
				// room for one more request in the next clk cycle, whatever the scheduler does meanwhile
				gfx_rdy <= ready && (gq_n + ((gfx_req && gfx_rdy) ? 4'd1 : 4'd0)) <= 4'd6;
			end
			if (rp_take) rp_v <= 1'b0;
			if (wp_take) wp_v <= 1'b0;
			gq_n <= gq_n + ((s_en && gfx_req && gfx_rdy) ? 4'd1 : 4'd0) - (gq_pop ? 4'd1 : 4'd0);
		end
	end
	// data queues: written by the return pipeline, read on s_en
	always @(posedge clk2) begin
		if (rst_f) begin gd_wp <= 0; gd_n <= 0; rd_wp <= 0; rd_n <= 0; end
		else begin
			if (gd_push) begin gd[gd_wp] <= word_in; gd_wp <= gd_wp + 2'd1; end
			if (rd_push) begin rd[rd_wp] <= word_in; rd_wp <= rd_wp + 2'd1; end
			gd_n <= gd_n + (gd_push ? 3'd1 : 3'd0) - ((s_en && gd_n != 0) ? 3'd1 : 3'd0);
			rd_n <= rd_n + (rd_push ? 3'd1 : 3'd0) - ((s_en && rd_n != 0) ? 3'd1 : 3'd0);
		end
	end

	// ---------------------------------------------------------------- bank state and timers
	reg        bopen [0:3];
	reg [12:0] brow [0:3];
	reg [2:0]  t_ras [0:3];      // cycles before PRECHARGE is allowed (ACTIVE: tRAS 42 ns; READ/WRITE: 2)
	reg [1:0]  t_rcd [0:3];      // cycles before READ/WRITE is allowed after ACTIVE (tRCD 20 ns)
	reg [1:0]  t_rp  [0:3];      // cycles before ACTIVE is allowed after PRECHARGE (tRP 20 ns)
	reg [1:0]  t_rrd;            // ACTIVE to ACTIVE, different banks (tRRD 14 ns)
	reg [1:0]  t_col;            // READ to next READ (burst of 2 on the bus)
	reg [3:0]  t_rfc;            // after REFRESH (tRFC 66 ns) or LOAD MODE
	reg        prev_read;        // last command was a READ (its DQM window is still running)

	// ---------------------------------------------------------------- current request
	localparam K_GFX = 2'd0, K_ROM = 2'd1, K_WR = 2'd2;
	reg        cur_v;
	reg [1:0]  cur_k;
	reg [1:0]  cur_bank;
	reg [12:0] cur_row;
	reg [8:0]  cur_col;
	reg [1:0]  cur_left;         // reads still to issue - 1
	reg [15:0] cur_d;
	reg [1:0]  cur_be;
	reg [3:0]  gfx_run;          // graphics words served since the last ROM line

	// refresh: one every 750 cycles (8192 rows in 64 ms needs one every 781)
	reg [9:0]  rf_cnt;
	reg        rf_pend;

	// initialisation
	reg [14:0] init_cnt;
	reg        init_done;

	// read return pipeline: tag {valid, rom} per clk2 cycle after the READ command. The READ leaves its
	// output register at edge E0; the low halfword is sampled into dq_r (an input register in the pin)
	// at edge E3 (CAS latency 2 plus the half-cycle shift of SDRAM_CLK), the high one at E4.
	reg [1:0]  rt [1:6];
	reg [15:0] dq_r, lo_r;
	wire [1:0] cap = rd_late ? rt[5] : rt[4];     // tag of the read whose low halfword is in dq_r
	wire [1:0] cap2 = rd_late ? rt[6] : rt[5];    // same read, high halfword in dq_r
	always @(posedge clk2) begin
		dq_r <= sd_dq_i;
		if (cap[1]) lo_r <= dq_r;
	end
	assign word_in = {dq_r, lo_r};
	assign gd_push = cap2[1] && !cap2[0];
	assign rd_push = cap2[1] && cap2[0];

	// next request: refresh first, then write, graphics (unless ROM is owed a turn), ROM
	wire g_avail = gq_n != 0;
	wire take_wr  = !cur_v && !rf_pend && init_done && wp_v;
	wire take_g   = !cur_v && !rf_pend && init_done && !wp_v && g_avail && !(rp_v && gfx_run >= 4'd8);
	wire take_rom = !cur_v && !rf_pend && init_done && !wp_v && rp_v && !take_g;
	assign gq_pop  = take_g;
	assign rp_take = take_rom;
	assign wp_take = take_wr;
	wire [24:0] g_head = gq[gq_rp];

	// command for the current request
	wire       b_open = bopen[cur_bank];
	wire       b_hit  = b_open && brow[cur_bank] == cur_row;
	wire       can_act = !b_open && t_rp[cur_bank] == 0 && t_rrd == 0 && t_rfc == 0 && !(prev_read && cur_row[12:11] != 2'b00);
	wire       can_pre = b_open && !b_hit && t_ras[cur_bank] == 0;
	wire       can_col = b_hit && t_rcd[cur_bank] == 0 && t_col == 0 &&
	                     (cur_k != K_WR || (rt[1] == 0 && rt[2] == 0 && rt[3] == 0 && rt[4] == 0 && rt[5] == 0 && rt[6] == 0));
	// refresh: precharge all once every bank allows it, then REFRESH
	reg        any_open;
	reg        all_pre_ok;
	integer b;
	always @(*) begin
		any_open = 1'b0; all_pre_ok = 1'b1;
		for (b = 0; b < 4; b = b + 1) begin
			if (bopen[b]) any_open = 1'b1;
			if (bopen[b] && t_ras[b] != 0) all_pre_ok = 1'b0;
		end
	end
	wire rf_idle = t_rp[0] == 0 && t_rp[1] == 0 && t_rp[2] == 0 && t_rp[3] == 0 && t_rfc == 0;

	always @(posedge clk2) begin
		cmd <= C_NOP;
		sd_a <= 13'd0;
		sd_dq_oe <= 1'b0;
		prev_read <= 1'b0;
		for (b = 0; b < 4; b = b + 1) begin
			if (t_ras[b] != 0) t_ras[b] <= t_ras[b] - 3'd1;
			if (t_rcd[b] != 0) t_rcd[b] <= t_rcd[b] - 2'd1;
			if (t_rp[b] != 0)  t_rp[b]  <= t_rp[b] - 2'd1;
		end
		if (t_rrd != 0) t_rrd <= t_rrd - 2'd1;
		if (t_col != 0) t_col <= t_col - 2'd1;
		if (t_rfc != 0) t_rfc <= t_rfc - 4'd1;
		rt[1] <= 2'b00; rt[2] <= rt[1]; rt[3] <= rt[2]; rt[4] <= rt[3]; rt[5] <= rt[4]; rt[6] <= rt[5];
		if (!init_done) begin
			// 100 us of NOP, PRECHARGE ALL, 2 REFRESH, LOAD MODE
			init_cnt <= init_cnt + 15'd1;
			case (init_cnt)
			15'd10000: begin cmd <= C_PRE; sd_a[10] <= 1'b1; end
			15'd10004: cmd <= C_REF;
			15'd10012: cmd <= C_REF;
			15'd10020: begin cmd <= C_MODE; sd_ba <= 2'b00; sd_a <= MODE; end
			15'd10024: init_done <= 1'b1;
			default: ;
			endcase
			for (b = 0; b < 4; b = b + 1) bopen[b] <= 1'b0;
			cur_v <= 1'b0; rf_pend <= 1'b0; rf_cnt <= 0; gfx_run <= 0;
			t_rrd <= 0; t_col <= 0; t_rfc <= 0;
			for (b = 0; b < 4; b = b + 1) begin t_ras[b] <= 0; t_rcd[b] <= 0; t_rp[b] <= 0; end
		end else begin
			rf_cnt <= rf_cnt + 10'd1;
			if (rf_cnt == 10'd749) begin rf_cnt <= 0; rf_pend <= 1'b1; end
			// pick the next request
			if (take_wr) begin
				cur_v <= 1'b1; cur_k <= K_WR; cur_bank <= wp_a[11:10]; cur_row <= wp_a[24:12]; cur_col <= wp_a[9:1];
				cur_left <= 2'd0; cur_d <= wp_d; cur_be <= wp_be;
			end else if (take_g) begin
				cur_v <= 1'b1; cur_k <= K_GFX; cur_bank <= g_head[11:10]; cur_row <= g_head[24:12];
				cur_col <= {g_head[9:2], 1'b0}; cur_left <= 2'd0;
				gq_rp <= gq_rp + 3'd1;
				gfx_run <= gfx_run + ((gfx_run == 4'd15) ? 4'd0 : 4'd1);
			end else if (take_rom) begin
				cur_v <= 1'b1; cur_k <= K_ROM; cur_bank <= rp_a[11:10]; cur_row <= rp_a[24:12];
				cur_col <= {rp_a[9:4], 3'd0}; cur_left <= 2'd3;
				gfx_run <= 0;
			end
			// issue a command
			if (cur_v) begin
				if (can_col) begin
					sd_ba <= cur_bank;
					t_col <= 2'd1;                               // next column command two cycles later
					t_ras[cur_bank] <= (t_ras[cur_bank] > 3'd2) ? t_ras[cur_bank] - 3'd1 : 3'd2;   // READ/WRITE to PRECHARGE >= 2
					if (cur_k == K_WR) begin
						cmd <= C_WRITE; sd_a <= {~cur_be, 2'b00, cur_col};
						sd_dq_o <= cur_d; sd_dq_oe <= 1'b1;
						t_ras[cur_bank] <= 3'd3;                  // tWR
						cur_v <= 1'b0;
					end else begin
						cmd <= C_READ; sd_a <= {2'b00, 2'b00, cur_col};
						prev_read <= 1'b1;
						rt[1] <= {1'b1, cur_k == K_ROM};
						cur_col <= cur_col + 9'd2;
						cur_left <= cur_left - 2'd1;
						if (cur_left == 2'd0) cur_v <= 1'b0;
					end
				end else if (can_pre) begin
					cmd <= C_PRE; sd_ba <= cur_bank; sd_a <= 13'd0;
					bopen[cur_bank] <= 1'b0; t_rp[cur_bank] <= 2'd2;
				end else if (can_act) begin
					cmd <= C_ACT; sd_ba <= cur_bank; sd_a <= cur_row;
					bopen[cur_bank] <= 1'b1; brow[cur_bank] <= cur_row;
					t_rcd[cur_bank] <= 2'd2; t_ras[cur_bank] <= 3'd5; t_rrd <= 2'd2;
				end
			end else if (rf_pend && !take_wr && !take_g && !take_rom) begin
				// refresh, once the last reads have left the bus
				if (any_open) begin
					if (all_pre_ok && t_col == 0) begin
						cmd <= C_PRE; sd_a[10] <= 1'b1;
						for (b = 0; b < 4; b = b + 1) begin bopen[b] <= 1'b0; t_rp[b] <= 2'd2; end
					end
				end else if (rf_idle && !prev_read) begin
					cmd <= C_REF; t_rfc <= 4'd7; rf_pend <= 1'b0;
				end
			end
		end
		// a core reset abandons the current request and the reads in flight (the queues are emptied by
		// the same reset); the SDRAM itself, its open rows and its contents are kept
		if (rst_f) begin
			cur_v <= 1'b0; gq_rp <= 3'd0;
			rt[1] <= 0; rt[2] <= 0; rt[3] <= 0; rt[4] <= 0; rt[5] <= 0; rt[6] <= 0;
		end
	end

	always @(posedge clk2) ready <= init_done;
	initial begin
		init_cnt = 0; init_done = 0; s_tog = 0; f1 = 0; f2 = 0;
		gq_rp = 0; cur_v = 0; rt[1] = 0; rt[2] = 0; rt[3] = 0; rt[4] = 0; rt[5] = 0; rt[6] = 0;
	end
endmodule
