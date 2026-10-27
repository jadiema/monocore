// MONOCORE-1 sprite engine for one line.
// 1. Evaluation (during the layer pass): scans the sprite table in index order and keeps the
//    sprites that cross the line, up to 64 sprites, 1280 displayed pixels and 512 graphics words (first overflow stops).
// 2. Drawing (after the layer pass): selected sprites from the last to the first, one pixel per
//    cycle, read-modify-write of the line buffer with the priority rule of sim/core.js.
// Zoom: source coordinates are floor(offset * 256 / zoom), computed exactly (dividers for the
// first pixel and the step, then an exact quotient/remainder accumulator).
module mc_sprites (
	input  wire         clk,
	input  wire         rst_n,
	input  wire         start_eval,
	input  wire         start_draw,
	input  wire         active,
	input  wire [9:0]   y,
	input  wire [9:0]   width,       // up to 640 (high resolution)
	input  wire [9:0]   count,       // entries to scan (0..512)
	input  wire [31:0]  base,
	output wire         eval_done,
	output wire         draw_done,
	output reg  [7:0]   nsel,
	output reg          overflow,
	// sprite table (4 x 32-bit banks, one entry per read, 1 cycle latency)
	output wire         spr_rd,
	output wire [8:0]   spr_addr,
	input  wire [127:0] spr_q,
	// graphics fetch
	output wire         g_req,
	output wire [31:0]  g_addr,
	input  wire         g_gnt,
	input  wire         r_push,
	input  wire [31:0]  r_data,
	// line buffer {pri[3:0], colour[11:0]}
	output wire         lb_rd,
	output wire [9:0]   lb_raddr,
	input  wire [15:0]  lb_q,
	output wire         lb_we,
	output wire [9:0]   lb_waddr,
	output wire [15:0]  lb_wdata
);
	// ---------------------------------------------------------------- entry fields
	// The table read (spr_q) is copied into q_r first; all arithmetic works on q_r.
	reg [127:0] q_r;
	always @(posedge clk) q_r <= spr_q;
	wire signed [15:0] f_x   = q_r[15:0];
	wire signed [15:0] f_y   = q_r[31:16];
	wire [15:0]        f_tile = q_r[47:32];
	wire [7:0]         f_pal  = q_r[55:48];
	wire [7:0]         f_attr = q_r[63:56];
	wire [7:0]         f_w    = q_r[71:64] == 0 ? 8'd1 : q_r[71:64];
	wire [7:0]         f_h    = q_r[79:72] == 0 ? 8'd1 : q_r[79:72];
	wire [15:0]        f_zx   = q_r[111:96] == 0 ? 16'd256 : q_r[111:96];
	wire [15:0]        f_zy   = q_r[127:112] == 0 ? 16'd256 : q_r[127:112];
	wire [26:0]        f_dh_full = {8'd0, f_h, 3'd0} * {11'd0, f_zy};
	wire [26:0]        f_dw_full = {8'd0, f_w, 3'd0} * {11'd0, f_zx};
	wire [18:0]        f_dh = f_dh_full[26:8];
	wire [18:0]        f_dw = f_dw_full[26:8];
	wire signed [17:0] f_dy = $signed({8'd0, y}) - $signed({{2{f_y[15]}}, f_y});
	wire [18:0]        f_wsrc = f_attr[5] ? {10'd0, f_w, 1'b0} : {11'd0, f_w};   // source words in a row

	// ---------------------------------------------------------------- evaluation
	// e1: entry on spr_q -> e2: entry in q_r, zoomed size (multipliers) -> e2b: hit test
	// -> e3: budget check, selection
	reg [8:0]  sel [0:63];
	reg        ev_run, ev_fin;
	reg [9:0]  ev_i;
	reg        e1, e2, e2b, e3;
	reg [8:0]  e1_i, e2_i, e2b_i, e3_i;
	reg        e2b_vis;
	reg        sw_en;
	reg [5:0]  sw_idx;
	reg [8:0]  sw_val;
	reg [17:0] e2b_dy;
	reg [18:0] e2b_dh, e2b_dw, e2b_wsrc;
	reg        e3_hit;
	reg [18:0] e3_dw, e3_words;
	reg [12:0] pix_left;              // pixel budget left: 1280 - pixels of the selected sprites
	reg [10:0] w_left;                // word budget left: 512 - graphics words (upper bound)
	wire ev_issue = ev_run && ev_i < count;
	wire e2b_hit = e2b_vis && !e2b_dy[17] && {1'b0, e2b_dy} < {1'b0, e2b_dh};
	// compared with what is left, so that no addition sits in front of the comparison
	wire ev_over = nsel >= 8'd64 || e3_dw > {6'd0, pix_left} || e3_words > {8'd0, w_left};

	// ---------------------------------------------------------------- drawing: setup
	// LOAD (entry on spr_q) -> C1 (fields, zoomed width) -> C2 (clipping, dividers start) -> WAIT
	// -> F1 (source row) -> F2 (row x width) -> F3 (parameter block)
	localparam SU_IDLE = 3'd0, SU_LOAD = 3'd1, SU_C1 = 3'd2, SU_C2 = 3'd3, SU_WAIT = 3'd4,
	           SU_F1 = 3'd5, SU_F2 = 3'd6, SU_F3 = 3'd7;
	reg [2:0]  su;
	reg        drawing;
	reg [7:0]  kk;                    // sprites left to set up
	reg        su_wait1;
	reg        p_valid;
	wire       su_issue = drawing && su == SU_IDLE && kk != 0 && !p_valid;
	wire [8:0] su_idx = sel[kk - 8'd1];
	assign spr_rd = ev_issue || su_issue;
	assign spr_addr = ev_issue ? ev_i[8:0] : su_idx;

	reg signed [15:0] s_x;
	reg [15:0] s_tile;
	reg [7:0]  s_pal, s_attr, s_w, s_h;
	reg [15:0] s_zx, s_zy;
	reg [18:0] s_dyv, s_dw;
	reg [9:0]  s_x0, s_x1;
	reg [18:0] s_i0;
	reg        s_zoomx, s_zoomy;
	reg [10:0] s_srcy;
	reg [15:0] s_rowmul;

	wire [10:0] dq_y, dq_x0, dq_s;
	wire [15:0] dr_x0, dr_s;
	wire        db_y, db_x0, db_s;
	reg         div_go;
	mc_div #(.NW(27), .DW(16), .QW(11)) div_y  (.clk(clk), .rst_n(rst_n), .start(div_go && s_zoomy),
		.n({s_dyv[18:0], 8'd0}), .d(s_zy), .q(dq_y), .r(), .busy(db_y));
	mc_div #(.NW(27), .DW(16), .QW(11)) div_x0 (.clk(clk), .rst_n(rst_n), .start(div_go && s_zoomx),
		.n({s_i0[18:0], 8'd0}), .d(s_zx), .q(dq_x0), .r(dr_x0), .busy(db_x0));
	mc_div #(.NW(27), .DW(16), .QW(11)) div_s  (.clk(clk), .rst_n(rst_n), .start(div_go && s_zoomx),
		.n(27'd256), .d(s_zx), .q(dq_s), .r(dr_s), .busy(db_s));

	// parameter block handed to the generator
	reg [9:0]  p_x0, p_x1;
	reg [10:0] p_q, p_qs;
	reg [15:0] p_r, p_rs;
	reg signed [16:0] p_rk;           // p_rs - zoom: the remainder step minus the divisor
	reg        p_hf, p_b8;
	reg [10:0] p_wpx1;                // width in pixels - 1
	reg [26:0] p_rowbase;             // cell index of the first cell of the row
	reg [2:0]  p_cy;
	reg [2:0]  p_prio;
	reg [7:0]  p_pal;

	wire [10:0] u_srcy0 = s_zoomy ? dq_y : s_dyv[10:0];
	wire [10:0] u_hpx1  = {s_h, 3'd0} - 11'd1;

	// ---------------------------------------------------------------- drawing: generator
	// G: source column and new-word flag -> A: cell and word address -> B: request / record
	reg        gen;
	reg [9:0]  gx, gx1;
	reg [10:0] gq, gqs;
	reg [15:0] gr, grs;
	reg signed [16:0] grk;
	reg        ghf, gb8, gfirst;
	reg [10:0] gwpx1;
	reg [31:0] gbase;                 // graphics address of the sprite's source row (base + rowbase * cell size)
	reg [2:0]  gcy, gprio;
	reg [7:0]  gpal;
	reg [8:0]  glastw;                // word id of the previous pixel of this sprite
	reg        a_valid, a_new, a_b8;
	reg [9:0]  a_x;
	reg [10:0] a_srcx;
	reg [31:0] a_base;
	reg [2:0]  a_cy, a_prio;
	reg [7:0]  a_pal;
	reg        b_valid, b_new, b_b8;
	reg [9:0]  b_x;
	reg [2:0]  b_sub, b_prio;
	reg [7:0]  b_pal;
	reg [31:0] b_waddr;
	reg [3:0]  wcount;                // words requested and not yet consumed

	wire [10:0] srcx  = ghf ? (gwpx1 - gq) : gq;
	wire [8:0]  wordid = gb8 ? srcx[10:2] : {1'b0, srcx[10:3]};
	// zoom step: r + rs, minus the divisor when it reaches it (both sums computed in parallel)
	wire [16:0] grsum = {1'b0, gr} + {1'b0, grs};
	wire signed [17:0] grdif = $signed({2'b0, gr}) + $signed({grk[16], grk});
	wire        gcarry = !grdif[17];
	// cell srcx/8 of the row, line cy of the cell, word srcx[2] (8 bpp): disjoint bit fields
	wire [31:0] a_off   = a_b8 ? {18'd0, a_srcx[10:3], a_cy, a_srcx[2], 2'b00} : {19'd0, a_srcx[10:3], a_cy, 2'b00};
	wire [31:0] a_waddr = a_base + a_off;
	wire        rec_full;
	assign g_req  = b_valid && b_new && !rec_full && wcount < 4'd8;
	assign g_addr = b_waddr;
	wire        b_push = b_valid && !rec_full && (!b_new || g_gnt);
	wire        b_free = !b_valid || b_push;
	wire        a_move = a_valid && b_free;
	wire        a_free = !a_valid || a_move;
	wire        g_step = gen && a_free;

	// record: {x 10, sub 3, newword, b8, prio 3, pal 8} = 26 bits
	wire [25:0] rec_in = {b_x, b_sub, b_new, b_b8, b_prio, b_pal};
	wire [25:0] rec;
	wire        rec_empty, resp_empty;
	wire [31:0] resp;
	wire        c_go;
	mc_fifo #(.W(26), .LOGD(4)) rec_fifo (.clk(clk), .rst_n(rst_n), .flush(start_eval),
		.push(b_push), .din(rec_in), .pop(c_go), .dout(rec), .empty(rec_empty), .full(rec_full), .count());
	wire rec_new = rec[12];
	mc_fifo #(.W(32), .LOGD(3)) resp_fifo (.clk(clk), .rst_n(rst_n), .flush(start_eval),
		.push(r_push), .din(r_data), .pop(c_go && rec_new), .dout(resp), .empty(resp_empty), .full(), .count());

	// ---------------------------------------------------------------- drawing: consumer (line buffer RMW)
	reg [31:0] cword;
	assign c_go = !rec_empty && (!rec_new || !resp_empty);
	wire [31:0] word = rec_new ? resp : cword;
	wire [9:0]  r_x    = rec[25:16];
	wire [2:0]  r_sub  = rec[15:13];
	wire        r_b8   = rec[11];
	wire [2:0]  r_prio = rec[10:8];
	wire [7:0]  r_pal  = rec[7:0];
	wire [7:0]  v = r_b8 ? word[8 * r_sub[1:0] +: 8] : {4'd0, word[4 * r_sub +: 4]};
	wire [11:0] r_col = {r_pal, 4'd0} + {4'd0, v};      // (palette * 16 + pixel) & 0xFFF
	wire [3:0]  r_pri = {1'b0, r_prio} + 4'd1;          // line buffer priority code: 0 = nothing
	assign lb_rd = c_go && v != 0;
	assign lb_raddr = r_x;

	reg        c2, w_prev_en;
	reg [9:0]  c2_x, w_prev_x;
	reg [15:0] c2_d, w_prev_d;
	wire [15:0] lbv = (w_prev_en && w_prev_x == c2_x) ? w_prev_d : lb_q;
	assign lb_we = c2 && c2_d[15:12] >= lbv[15:12];
	assign lb_waddr = c2_x;
	assign lb_wdata = c2_d;

	assign eval_done = ev_fin;
	assign draw_done = !drawing;

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			ev_run <= 0; ev_fin <= 1'b1; ev_i <= 0; e1 <= 0; e2 <= 0; e2b <= 0; e3 <= 0; e1_i <= 0; e2_i <= 0; e2b_i <= 0; e3_i <= 0;
			sw_en <= 0; sw_idx <= 0; sw_val <= 0; e2b_vis <= 0; e2b_dy <= 0; e2b_dh <= 0; e2b_dw <= 0; e2b_wsrc <= 0;
			e3_hit <= 0; e3_dw <= 0; e3_words <= 0; pix_left <= 13'd1280; w_left <= 11'd512; nsel <= 0; overflow <= 0;
			su <= SU_IDLE; drawing <= 0; kk <= 0; su_wait1 <= 0; div_go <= 0; p_valid <= 0;
			s_x <= 0; s_tile <= 0; s_pal <= 0; s_attr <= 0; s_w <= 0; s_h <= 0; s_zx <= 0; s_zy <= 0;
			s_dyv <= 0; s_dw <= 0; s_x0 <= 0; s_x1 <= 0; s_i0 <= 0; s_zoomx <= 0; s_zoomy <= 0; s_srcy <= 0; s_rowmul <= 0;
			p_x0 <= 0; p_x1 <= 0; p_q <= 0; p_qs <= 0; p_r <= 0; p_rs <= 0; p_rk <= 0;
			p_hf <= 0; p_b8 <= 0; p_wpx1 <= 0; p_rowbase <= 0; p_cy <= 0; p_prio <= 0; p_pal <= 0;
			gen <= 0; gx <= 0; gx1 <= 0; gq <= 0; gqs <= 0; gr <= 0; grs <= 0; grk <= 0; ghf <= 0; gb8 <= 0;
			gfirst <= 0; gwpx1 <= 0; gbase <= 0; gcy <= 0; gprio <= 0; gpal <= 0; glastw <= 0; wcount <= 0;
			a_valid <= 0; a_new <= 0; a_b8 <= 0; a_x <= 0; a_srcx <= 0; a_base <= 0; a_cy <= 0; a_prio <= 0; a_pal <= 0;
			b_valid <= 0; b_new <= 0; b_b8 <= 0; b_x <= 0; b_sub <= 0; b_prio <= 0; b_pal <= 0; b_waddr <= 0;
			cword <= 0; c2 <= 0; c2_x <= 0; c2_d <= 0; w_prev_en <= 0; w_prev_x <= 0; w_prev_d <= 0;
		end else if (start_eval) begin
			ev_run <= active; ev_fin <= !active; ev_i <= 0; e1 <= 0; e2 <= 0; e2b <= 0; e3 <= 0;
			pix_left <= 13'd1280; w_left <= 11'd512; nsel <= 0; overflow <= 0;
			su <= SU_IDLE; drawing <= 0; p_valid <= 0; gen <= 0; a_valid <= 0; b_valid <= 0;
			wcount <= 0; c2 <= 0; w_prev_en <= 0; div_go <= 0;
		end else begin
			div_go <= 1'b0;
			// ---- evaluation (the selection list is written one cycle after the decision)
			sw_en <= 1'b0;
			if (sw_en) sel[sw_idx] <= sw_val;
			e1 <= ev_issue; e1_i <= ev_i[8:0];
			if (ev_issue) ev_i <= ev_i + 10'd1;
			e2 <= e1 && ev_run; e2_i <= e1_i;
			e2b <= e2 && ev_run; e2b_i <= e2_i;
			e2b_vis <= f_attr[7]; e2b_dy <= f_dy; e2b_dh <= f_dh; e2b_dw <= f_dw; e2b_wsrc <= f_wsrc;
			e3 <= e2b && ev_run; e3_i <= e2b_i;
			e3_hit <= e2b_hit; e3_dw <= e2b_dw;
			e3_words <= e2b_dw < e2b_wsrc ? e2b_dw : e2b_wsrc;
			if (e3 && e3_hit && ev_run) begin
				if (ev_over) begin overflow <= 1'b1; ev_run <= 1'b0; ev_fin <= 1'b1; end
				else begin sw_en <= 1'b1; sw_idx <= nsel[5:0]; sw_val <= e3_i; nsel <= nsel + 8'd1; pix_left <= pix_left - e3_dw[12:0]; w_left <= w_left - e3_words[10:0]; end
			end
			if (ev_run && !ev_issue && !e1 && !e2 && !e2b && !e3) begin ev_run <= 1'b0; ev_fin <= 1'b1; end

			// ---- setup (one sprite at a time, from the last selected to the first)
			if (start_draw) begin drawing <= active && nsel != 0; kk <= nsel; end
			case (su)
			SU_IDLE: if (su_issue) su <= SU_LOAD;
			SU_LOAD: su <= SU_C1;                        // the entry is on spr_q, copied to q_r
			SU_C1: begin
				s_x <= f_x; s_tile <= f_tile; s_pal <= f_pal; s_attr <= f_attr; s_w <= f_w; s_h <= f_h;
				s_zx <= f_zx; s_zy <= f_zy; s_dyv <= f_dy[17:0]; s_dw <= f_dw;
				s_x0 <= f_x[15] ? 10'd0 : (f_x > $signed({6'd0, width}) ? width : f_x[9:0]);
				s_i0 <= f_x[15] ? 19'd0 - {{3{f_x[15]}}, f_x} : 19'd0;
				s_zoomx <= f_zx != 16'd256;
				s_zoomy <= f_zy != 16'd256;
				kk <= kk - 8'd1;
				su <= SU_C2;
			end
			SU_C2: begin
				begin : clip
					reg signed [20:0] xe;
					xe = $signed({{5{s_x[15]}}, s_x}) + $signed({2'd0, s_dw});
					s_x1 <= xe <= 0 ? 10'd0 : (xe > $signed({11'd0, width}) ? width : xe[9:0]);
				end
				div_go <= 1'b1; su_wait1 <= 1'b1;
				su <= SU_WAIT;
			end
			SU_WAIT: begin
				su_wait1 <= 1'b0;
				if (!su_wait1 && !db_y && !db_x0 && !db_s) su <= SU_F1;
			end
			SU_F1: begin
				s_srcy <= s_attr[1] ? (u_hpx1 - u_srcy0) : u_srcy0;
				su <= SU_F2;
			end
			SU_F2: begin
				s_rowmul <= s_srcy[10:3] * s_w;
				su <= SU_F3;
			end
			SU_F3: begin
				if (s_x0 < s_x1) begin
					p_valid <= 1'b1;
					p_x0 <= s_x0; p_x1 <= s_x1;
					p_q  <= s_zoomx ? dq_x0 : s_i0[10:0];
					p_r  <= s_zoomx ? dr_x0 : 16'd0;
					p_qs <= s_zoomx ? dq_s : 11'd1;
					p_rs <= s_zoomx ? dr_s : 16'd0;
					p_rk <= $signed({1'b0, s_zoomx ? dr_s : 16'd0}) - $signed({1'b0, s_zx});
					p_hf <= s_attr[0]; p_b8 <= s_attr[5];
					p_wpx1 <= {s_w, 3'd0} - 11'd1;
					p_rowbase <= {11'd0, s_tile} + {11'd0, s_rowmul};
					p_cy <= s_srcy[2:0];
					p_prio <= s_attr[4:2];
					p_pal <= s_pal;
				end
				su <= SU_IDLE;
			end
			default: su <= SU_IDLE;
			endcase

			// ---- generator, stage G: one pixel per cycle while stage A is free
			if (!gen && p_valid) begin
				gen <= 1'b1; p_valid <= 1'b0; gfirst <= 1'b1;
				gx <= p_x0; gx1 <= p_x1; gq <= p_q; gqs <= p_qs; gr <= p_r; grs <= p_rs; grk <= p_rk;
				ghf <= p_hf; gb8 <= p_b8; gwpx1 <= p_wpx1; gbase <= base + (p_b8 ? {p_rowbase[25:0], 6'd0} : {p_rowbase[26:0], 5'd0}); gcy <= p_cy;
				gprio <= p_prio; gpal <= p_pal;
			end else if (g_step) begin
				gfirst <= 1'b0;
				glastw <= wordid;
				gx <= gx + 10'd1;
				gq <= gq + gqs + (gcarry ? 11'd1 : 11'd0);
				gr <= gcarry ? grdif[15:0] : grsum[15:0];
				if (gx + 10'd1 == gx1) gen <= 1'b0;
			end
			// stage A
			if (g_step) begin
				a_valid <= 1'b1; a_x <= gx; a_srcx <= srcx; a_new <= gfirst || wordid != glastw;
				a_b8 <= gb8; a_base <= gbase; a_cy <= gcy; a_prio <= gprio; a_pal <= gpal;
			end else if (a_move) a_valid <= 1'b0;
			// stage B
			if (a_move) begin
				b_valid <= 1'b1; b_x <= a_x; b_new <= a_new; b_b8 <= a_b8; b_prio <= a_prio; b_pal <= a_pal;
				b_sub <= a_b8 ? {1'b0, a_srcx[1:0]} : a_srcx[2:0];
				b_waddr <= a_waddr;
			end else if (b_push) b_valid <= 1'b0;
			wcount <= wcount + ((g_req && g_gnt) ? 4'd1 : 4'd0) - ((c_go && rec_new) ? 4'd1 : 4'd0);

			// ---- consumer
			if (c_go && rec_new) cword <= resp;
			c2 <= lb_rd;
			c2_x <= r_x;
			c2_d <= {r_pri, r_col};
			w_prev_en <= lb_we; w_prev_x <= lb_waddr; w_prev_d <= lb_wdata;

			// ---- end of drawing
			if (drawing && kk == 0 && su == SU_IDLE && !p_valid && !gen && !a_valid && !b_valid && rec_empty && !c2 && !start_draw)
				drawing <= 1'b0;
		end
	end
`ifdef VERILATOR
	reg [15:0] dbg_n;
	always @(posedge clk) begin
		dbg_n <= drawing ? dbg_n + 16'd1 : 16'd0;
		if (dbg_n == 16'd6000)
			$display("sprites stuck: su=%0d kk=%0d p_valid=%b gen=%b gx=%0d gx1=%0d a=%b b=%b rec_empty=%b c2=%b wcount=%0d",
				su, kk, p_valid, gen, gx, gx1, a_valid, b_valid, rec_empty, c2, wcount);
	end
`endif
endmodule
