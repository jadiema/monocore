// MONOCORE-1 line renderer.
// Each visible line y is rendered at the start of its own line period (the "render window"),
// then shown during the next line. While the window is open, CPU writes to video memory and
// registers are held off (see mc_gpu: wr_ok), so the line is computed from the state at the
// start of the line, exactly like sim/core.js which renders a line before the CPU runs it.
//
// Window: line tables -> 4 layers in parallel (one pixel per cycle, composed into the line
// buffer) while the sprites are evaluated -> sprites drawn into the line buffer -> palette.
module mc_render (
	input  wire         clk,
	input  wire         rst_n,
	// timing
	input  wire         line_start,
	input  wire [9:0]   new_line,
	input  wire [9:0]   new_height,
	input  wire [8:0]   new_width,
	input  wire         vblank_pulse,
	// registers
	input  wire [31:0]  disp_ctrl,
	input  wire [31:0]  bg_color,
	input  wire [31:0]  spr_chr,
	input  wire [31:0]  spr_count,
	input  wire [127:0] layer_ctrl,
	input  wire [127:0] layer_chr,
	input  wire [127:0] layer_scrollx,
	input  wire [127:0] layer_scrolly,
	input  wire [31:0]  rom_size,
	// video memories (render ports, 1 cycle latency)
	output wire [3:0]   tm_rd,
	output wire [47:0]  tm_addr,
	input  wire [127:0] tm_q,
	output wire         lt_rd,
	output wire [10:0]  lt_addr,
	input  wire [31:0]  lt_q,
	output wire         spr_rd,
	output wire [8:0]   spr_addr,
	input  wire [127:0] spr_q,
	output wire         pal_rd,
	output wire [10:0]  pal_addr,
	input  wire [31:0]  pal_q,
	output wire         chr_rd,
	output wire [14:0]  chr_addr,
	input  wire [31:0]  chr_q,
	// graphics read port to the SDRAM (ROM): in-order, pipelined
	output wire         g_req,
	output wire [25:0]  g_addr,
	input  wire         g_rdy,
	input  wire         g_valid,
	input  wire [31:0]  g_data,
	// status
	output wire         busy,
	output reg          spr_overflow,
	output reg  [7:0]   spr_peak,
	output reg  [15:0]  window_cycles,   // length of the last render window (statistics)
	// rendered pixels (to the scan-out line buffers; also captured by the testbench)
	output reg          px_valid,
	output reg  [8:0]   px_x,
	output reg  [9:0]   px_y,
	output reg  [14:0]  px_rgb
);
	localparam P_IDLE = 3'd0, P_LT = 3'd1, P_PARAM = 3'd2, P_LAYERS = 3'd3, P_SPR = 3'd4, P_RES = 3'd5;
	reg [2:0] phase;
	reg [9:0] y;
	reg [8:0] W;
	reg [31:0] r_disp, r_bg, r_sprchr, r_sprcount;
	reg [127:0] r_lctrl, r_lchr, r_lsx, r_lsy;
	reg [2:0]  lt_i;               // line table reads issued
	reg [15:0] lt_dx [0:3], lt_dy [0:3];
	reg        lt_pend;
	reg [1:0]  lt_pidx;
	reg [15:0] wcount;
	reg [7:0]  spr_peak_cur;
	reg        spr_ovf_cur;
	wire       tagq_full, tagq_empty;
	wire [3:0] tag_out;

	wire start_line = line_start && new_line < new_height;
	// CPU and DMA writes to video wait while busy: from the first cycle of every line (line_start comes
	// almost straight from a register) to the end of the window (busy_r). A write in the first cycle
	// would otherwise miss the registers latched at that edge while reaching the memories read later.
	reg busy_r;
	assign busy = busy_r || line_start;
	wire disp_on = r_disp[0];

	// ---------------------------------------------------------------- region checks
	function chr_region(input [31:0] a);
		chr_region = a >= 32'h200C0000 && a < 32'h200E0000;
	endfunction
	function rom_region(input [31:0] a, input [31:0] size);
		rom_region = a < size;
	endfunction

	// ---------------------------------------------------------------- layer parameters
	reg        l_start;
	reg  [3:0] lp_active, lp_src;          // src: 1 = CHR RAM, 0 = ROM
	reg  [31:0] lp_base [0:3];
	reg  [3:0] lp_ts16, lp_b8;
	reg  [1:0] lp_mwc [0:3], lp_prio [0:3];
	reg [10:0] lp_px0 [0:3], lp_py [0:3];

	integer l;
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			l_start <= 1'b0; lp_active <= 0; lp_src <= 0; lp_ts16 <= 0; lp_b8 <= 0;
			for (l = 0; l < 4; l = l + 1) begin
				lp_base[l] <= 0; lp_mwc[l] <= 0; lp_prio[l] <= 0; lp_px0[l] <= 0; lp_py[l] <= 0;
			end
		end else begin
			l_start <= 1'b0;
			if (phase == P_PARAM) begin
				l_start <= 1'b1;
				for (l = 0; l < 4; l = l + 1) begin : lpar
					reg [31:0] lc, base;
					reg [1:0]  mwc, mhc;
					reg [3:0]  pwb, phb;
					reg signed [17:0] sx, sy;
					reg [17:0] pyy;
					lc   = r_lctrl[32 * l +: 32];
					base = r_lchr[32 * l +: 32];
					mwc  = lc[3:2] > 2'd2 ? 2'd2 : lc[3:2];
					mhc  = lc[5:4] > 2'd2 ? 2'd2 : lc[5:4];
					pwb  = 4'd5 + {2'd0, mwc} + (lc[0] ? 4'd4 : 4'd3);
					phb  = 4'd5 + {2'd0, mhc} + (lc[0] ? 4'd4 : 4'd3);
					sx   = $signed({{2{r_lsx[32 * l + 15]}}, r_lsx[32 * l +: 16]}) + (lc[6] ? $signed({{2{lt_dx[l][15]}}, lt_dx[l]}) : 18'sd0);
					sy   = $signed({{2{r_lsy[32 * l + 15]}}, r_lsy[32 * l +: 16]}) + (lc[6] ? $signed({{2{lt_dy[l][15]}}, lt_dy[l]}) : 18'sd0);
					pyy  = {8'd0, y} + sy;
					lp_active[l] <= r_disp[1 + l] && ({1'b0, mwc} + {1'b0, mhc}) <= 3'd2 &&
					                (chr_region(base) || rom_region(base, rom_size));
					lp_src[l]  <= chr_region(base);
					lp_base[l] <= base;
					lp_ts16[l] <= lc[0];
					lp_b8[l]   <= lc[1];
					lp_mwc[l]  <= mwc;
					lp_prio[l] <= lc[9:8];
					lp_px0[l]  <= sx[10:0] & ((11'd1 << pwb) - 11'd1);
					lp_py[l]   <= pyy[10:0] & ((11'd1 << phb) - 11'd1);
				end
			end
		end
	end

	// ---------------------------------------------------------------- layers
	wire [3:0]  pv, ltake;
	wire [31:0] pvv, ppal;
	wire [11:0] pp;
	wire [4:0]  rq, gnt, rpush;
	wire [159:0] rqaddr;
	reg  [31:0] rdata_mux [0:4];
	wire [31:0] r_data_l [0:4];
	genvar g;
	generate for (g = 0; g < 4; g = g + 1) begin: lay
		mc_layer u (
			.clk(clk), .rst_n(rst_n), .start(l_start),
			.active(lp_active[g]), .base(lp_base[g]), .ts16(lp_ts16[g]), .b8(lp_b8[g]), .mwc(lp_mwc[g]),
			.lprio(lp_prio[g]), .px0(lp_px0[g]), .py(lp_py[g]), .width(W),
			.tm_rd(tm_rd[g]), .tm_addr(tm_addr[12 * g +: 12]), .tm_q(tm_q[32 * g +: 32]),
			.g_req(rq[g]), .g_addr(rqaddr[32 * g +: 32]), .g_gnt(gnt[g]), .r_push(rpush[g]), .r_data(r_data_l[g]),
			.pix_valid(pv[g]), .pix_v(pvv[8 * g +: 8]), .pix_pal(ppal[8 * g +: 8]), .pix_p(pp[3 * g +: 3]),
			.pix_take(ltake[g]));
	end endgenerate

	// ---------------------------------------------------------------- sprites
	wire        s_active = r_disp[0] && r_disp[5] && (chr_region(r_sprchr) || rom_region(r_sprchr, rom_size));
	reg         s_src;                 // 1 = CHR RAM
	reg         s_start_draw;
	wire        s_eval_done, s_draw_done;
	wire [7:0]  s_nsel;
	wire        s_ovf;
	wire        s_lb_rd, s_lb_we;
	wire [8:0]  s_lb_raddr, s_lb_waddr;
	wire [15:0] s_lb_wdata, lb_q;
	mc_sprites spr (
		.clk(clk), .rst_n(rst_n), .start_eval(l_start), .start_draw(s_start_draw), .active(s_active),
		.y(y), .width(W), .count(r_sprcount > 32'd512 ? 10'd512 : r_sprcount[9:0]), .base(r_sprchr),
		.eval_done(s_eval_done), .draw_done(s_draw_done), .nsel(s_nsel), .overflow(s_ovf),
		.spr_rd(spr_rd), .spr_addr(spr_addr), .spr_q(spr_q),
		.g_req(rq[4]), .g_addr(rqaddr[128 +: 32]), .g_gnt(gnt[4]), .r_push(rpush[4]), .r_data(r_data_l[4]),
		.lb_rd(s_lb_rd), .lb_raddr(s_lb_raddr), .lb_q(lb_q),
		.lb_we(s_lb_we), .lb_waddr(s_lb_waddr), .lb_wdata(s_lb_wdata));

	// ---------------------------------------------------------------- graphics fetch servers
	// Each requester reads from one memory for the whole line (decided by its base address),
	// so responses stay in order per requester. Out-of-range words read as zero, like the simulator.
	wire [4:0] req_src = {s_src, lp_src};
	reg  [4:0] chr_gnt, sd_gnt;
	integer r;
	always @(*) begin
		chr_gnt = 5'd0; sd_gnt = 5'd0;
		for (r = 4; r >= 0; r = r - 1) begin
			if (rq[r] && req_src[r]) chr_gnt = 5'd1 << r;
		end
		for (r = 4; r >= 0; r = r - 1) begin
			if (rq[r] && !req_src[r] && g_rdy && !tagq_full) sd_gnt = 5'd1 << r;
		end
	end
	assign gnt = chr_gnt | sd_gnt;
	function [2:0] enc(input [4:0] oh);
		enc = oh[0] ? 3'd0 : oh[1] ? 3'd1 : oh[2] ? 3'd2 : oh[3] ? 3'd3 : 3'd4;
	endfunction
	// out-of-range tests on each requester's address, in parallel, before the grant selects one
	// (subtractions, so that they use carry chains)
	reg  [4:0] rq_oor, rq_nochr;
	integer q;
	always @(*) begin
		for (q = 0; q < 5; q = q + 1) begin : oor
			reg [32:0] d0, d1, d2;
			d0 = {1'b0, rqaddr[32 * q +: 32]} - {1'b0, rom_size};
			d1 = {1'b0, rqaddr[32 * q +: 32]} - 33'h0200C0000;
			d2 = {1'b0, rqaddr[32 * q +: 32]} - 33'h0200E0000;
			rq_oor[q]   = !d0[32];                  // address >= ROM size
			rq_nochr[q] = d1[32] || !d2[32];        // outside 0x200C0000 - 0x200DFFFF
		end
	end
	wire [31:0] chr_a = rqaddr[32 * enc(chr_gnt) +: 32];
	wire [31:0] sd_a  = rqaddr[32 * enc(sd_gnt) +: 32];
	assign chr_rd   = |chr_gnt;
	assign chr_addr = chr_a[16:2];
	reg        chr_resp, chr_zero;
	reg [2:0]  chr_tag;
	assign g_req  = |sd_gnt;
	assign g_addr = sd_a[25:0];

	// SDRAM response tags: {zero, requester}
	mc_fifo #(.W(4), .LOGD(4)) tagq (.clk(clk), .rst_n(rst_n), .flush(1'b0),
		.push(|sd_gnt), .din({|(rq_oor & sd_gnt), enc(sd_gnt)}), .pop(g_valid), .dout(tag_out),
		.empty(tagq_empty), .full(tagq_full), .count());

	// CHR RAM read data goes through a register (the 128 KB RAM has a wide output multiplexer)
	reg        chr_resp1, chr_zero1;
	reg [2:0]  chr_tag1;
	reg [31:0] chr_q_r;
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin chr_resp1 <= 0; chr_tag1 <= 0; chr_zero1 <= 0; chr_resp <= 0; chr_tag <= 0; chr_zero <= 0; chr_q_r <= 0; end
		else begin
			chr_resp1 <= |chr_gnt;
			chr_tag1  <= enc(chr_gnt);
			chr_zero1 <= |(rq_nochr & chr_gnt);
			chr_resp  <= chr_resp1; chr_tag <= chr_tag1; chr_zero <= chr_zero1;
			chr_q_r   <= chr_q;
		end
	end
	generate for (g = 0; g < 5; g = g + 1) begin: resp
		wire from_chr = chr_resp && chr_tag == g;
		wire from_sd  = g_valid && tag_out[2:0] == g;
		assign rpush[g] = from_chr || from_sd;
		assign r_data_l[g] = from_chr ? (chr_zero ? 32'd0 : chr_q_r) : (tag_out[3] ? 32'd0 : g_data);
	end endgenerate

	// ---------------------------------------------------------------- composition (layers -> line buffer)
	reg  [8:0]  cx;
	wire        comp_go = phase == P_LAYERS && !l_start && cx < W && &pv;
	assign ltake = {4{comp_go}};
	// three stages: C1 registers the four layer pixels, C2 the winner, C3 its colour, written to the line buffer.
	// The winner is the non-transparent layer of highest priority, the lowest layer number on a tie;
	// the background colour (priority code 0) when all four are transparent.
	reg         c1_v, c2_v, cw_v;
	reg  [8:0]  c1_x, c2_x, cw_x;
	reg         c2_any;
	reg  [7:0]  c2_pal, c2_pix;
	reg  [2:0]  c2_p;
	reg  [31:0] c1_pvv, c1_ppal;
	reg  [11:0] c1_pp;
	reg  [15:0] cw_d;
	wire [3:0]  c1_nz = {c1_pvv[31:24] != 0, c1_pvv[23:16] != 0, c1_pvv[15:8] != 0, c1_pvv[7:0] != 0};
	wire        w32 = c1_nz[2] && (!c1_nz[3] || c1_pp[8:6] >= c1_pp[11:9]);     // 1: layer 2, 0: layer 3
	wire        w10 = c1_nz[0] && (!c1_nz[1] || c1_pp[2:0] >= c1_pp[5:3]);      // 1: layer 0, 0: layer 1
	wire [1:0]  k32 = w32 ? 2'd2 : 2'd3, k10 = w10 ? 2'd0 : 2'd1;
	wire        n32 = c1_nz[3] || c1_nz[2], n10 = c1_nz[1] || c1_nz[0];
	wire [2:0]  p32 = c1_pp[3 * k32 +: 3], p10 = c1_pp[3 * k10 +: 3];
	wire [1:0]  kw  = (n10 && (!n32 || p10 >= p32)) ? k10 : k32;
	wire        c_any = n32 || n10;
	wire [11:0] c_col = c2_any ? {c2_pal, 4'd0} + {4'd0, c2_pix} : r_bg[11:0];
	wire [3:0]  c_pri = c2_any ? {1'b0, c2_p} + 4'd1 : 4'd0;
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			c1_v <= 0; cw_v <= 0; c1_x <= 0; cw_x <= 0; c1_pvv <= 0; c1_ppal <= 0; c1_pp <= 0; cw_d <= 0;
			c2_v <= 0; c2_x <= 0; c2_any <= 0; c2_pal <= 0; c2_pix <= 0; c2_p <= 0;
		end else begin
			c1_v <= comp_go; c1_x <= cx; c1_pvv <= pvv; c1_ppal <= ppal; c1_pp <= pp;
			c2_v <= c1_v; c2_x <= c1_x; c2_any <= c_any;
			c2_pal <= c1_ppal[8 * kw +: 8]; c2_pix <= c1_pvv[8 * kw +: 8]; c2_p <= c1_pp[3 * kw +: 3];
			cw_v <= c2_v; cw_x <= c2_x; cw_d <= {c_pri, c_col};
		end
	end
	wire        comp_busy = c1_v || c2_v || cw_v;

	// ---------------------------------------------------------------- line buffer
	reg  [8:0]  rx;                 // resolve read index
	wire        res_rd = phase == P_RES && rx < W;
	wire        lb_we = cw_v || s_lb_we;
	wire [8:0]  lb_wa = cw_v ? cw_x : s_lb_waddr;
	wire [15:0] lb_wd = cw_v ? cw_d : s_lb_wdata;
	wire        lb_re = res_rd || s_lb_rd;
	wire [8:0]  lb_ra = res_rd ? rx : s_lb_raddr;
	// same-cycle read and write of one address is covered by the sprite bypass (w_prev_*)
	(* ramstyle = "M10K, no_rw_check" *) reg  [15:0] lbuf [0:511];
	reg  [15:0] lb_q_r;
	always @(posedge clk) begin
		if (lb_we) lbuf[lb_wa] <= lb_wd;
		if (lb_re) lb_q_r <= lbuf[lb_ra];
	end
	assign lb_q = lb_q_r;

	// ---------------------------------------------------------------- resolve (palette)
	reg        r1, r2;
	reg [8:0]  r1_x, r2_x;
	reg        r2_hi;
	assign pal_rd   = r1;
	assign pal_addr = lb_q_r[11:1];
	wire [15:0] pal_entry = r2_hi ? pal_q[31:16] : pal_q[15:0];

	// ---------------------------------------------------------------- sequencing
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			phase <= P_IDLE; y <= 0; W <= 0; r_disp <= 0; r_bg <= 0; r_sprchr <= 0; r_sprcount <= 0;
			r_lctrl <= 0; r_lchr <= 0; r_lsx <= 0; r_lsy <= 0; lt_i <= 0; lt_pend <= 0; lt_pidx <= 0;
			for (l = 0; l < 4; l = l + 1) begin lt_dx[l] <= 0; lt_dy[l] <= 0; end
			cx <= 0; rx <= 0; r1 <= 0; r2 <= 0; r1_x <= 0; r2_x <= 0; r2_hi <= 0; s_start_draw <= 0; s_src <= 0;
			px_valid <= 0; px_x <= 0; px_y <= 0; px_rgb <= 0; busy_r <= 0;
			spr_overflow <= 0; spr_peak <= 0; window_cycles <= 0; wcount <= 0; spr_peak_cur <= 0; spr_ovf_cur <= 0;
		end else begin
			s_start_draw <= 1'b0;
			px_valid <= 1'b0;
			if (phase != P_IDLE) wcount <= wcount + 16'd1;
			case (phase)
			P_IDLE: if (start_line) begin
				busy_r <= 1'b1;
				y <= new_line; W <= new_width;
				r_disp <= disp_ctrl; r_bg <= bg_color; r_sprchr <= spr_chr; r_sprcount <= spr_count;
				r_lctrl <= layer_ctrl; r_lchr <= layer_chr; r_lsx <= layer_scrollx; r_lsy <= layer_scrolly;
				s_src <= chr_region(spr_chr);
				lt_i <= 0; lt_pend <= 0; wcount <= 16'd1;
				phase <= disp_ctrl[0] ? P_LT : P_RES;
				rx <= 0; cx <= 0;
			end
			P_LT: begin
				lt_pend <= lt_i < 3'd4;
				lt_pidx <= lt_i[1:0];
				if (lt_i < 3'd4) lt_i <= lt_i + 3'd1;
				if (lt_pend) begin lt_dx[lt_pidx] <= lt_q[15:0]; lt_dy[lt_pidx] <= lt_q[31:16]; end
				if (lt_pend && lt_pidx == 2'd3) phase <= P_PARAM;
			end
			P_PARAM: phase <= P_LAYERS;
			P_LAYERS: begin
				if (comp_go) cx <= cx + 9'd1;
				if (cx == W && !comp_busy && s_eval_done && !l_start) begin
					s_start_draw <= 1'b1;
					phase <= P_SPR;
				end
			end
			P_SPR: if (!s_start_draw && s_draw_done) phase <= P_RES;
			P_RES: begin
				if (res_rd) rx <= rx + 9'd1;
				if (rx == W && !r1 && !r2) begin
					phase <= P_IDLE;
					busy_r <= 1'b0;
					window_cycles <= wcount;
				end
			end
			default: phase <= P_IDLE;
			endcase

			// resolve pipeline: line buffer -> palette -> pixel
			r1 <= res_rd; r1_x <= rx;
			r2 <= r1; r2_x <= r1_x; r2_hi <= lb_q_r[0];
			if (r2) begin
				px_valid <= 1'b1; px_x <= r2_x; px_y <= y;
				px_rgb <= r_disp[0] ? pal_entry[14:0] : 15'd0;
			end

			// sprite statistics, latched at the start of the vertical blank
			if (phase == P_LAYERS && cx == W && !comp_busy && s_eval_done && !l_start && s_active) begin
				if (s_nsel > spr_peak_cur) spr_peak_cur <= s_nsel;
				if (s_ovf) spr_ovf_cur <= 1'b1;
			end
			if (line_start && new_line == 10'd0) begin spr_peak_cur <= 0; spr_ovf_cur <= 0; end
			if (vblank_pulse) begin spr_peak <= spr_peak_cur; spr_overflow <= spr_ovf_cur; end
		end
	end
`ifdef VERILATOR
	always @(posedge clk) if (l_start && y == 10'd0 && $test$plusargs("lparams"))
		$display("y0 layers: dx %0d %0d %0d %0d dy %0d %0d %0d %0d px0 %0d %0d %0d %0d py %0d %0d %0d %0d act %b",
			$signed(lt_dx[0]), $signed(lt_dx[1]), $signed(lt_dx[2]), $signed(lt_dx[3]),
			$signed(lt_dy[0]), $signed(lt_dy[1]), $signed(lt_dy[2]), $signed(lt_dy[3]),
			lp_px0[0], lp_px0[1], lp_px0[2], lp_px0[3], lp_py[0], lp_py[1], lp_py[2], lp_py[3], lp_active);
	// simulation watchdog: report a render window that does not end
	always @(posedge clk) if (wcount == 16'd7000)
		$display("render stuck: y=%0d phase=%0d cx=%0d W=%0d pv=%b eval_done=%b draw_done=%b nsel=%0d act=%b src=%b rq=%b tagq_empty=%b",
			y, phase, cx, W, pv, s_eval_done, s_draw_done, s_nsel, lp_active, lp_src, rq, tagq_empty);
`endif
	assign lt_rd   = phase == P_LT && lt_i < 3'd4;
	assign lt_addr = {lt_i[1:0], y[8:0]};
endmodule
