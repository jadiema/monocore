// MONOCORE-1 tile layer pipeline: walks one line of a tile layer, reads the tilemap, fetches the
// graphics words (8 pixels in 4 bpp, 4 in 8 bpp) and delivers one pixel per cycle in screen order.
// Semantics: sim/core.js renderLine (per-pixel loop over px, tile entry, flips, 4/8 bpp).
module mc_layer (
	input  wire        clk,
	input  wire        rst_n,
	input  wire        start,        // one cycle, parameters valid
	// line parameters (latched by the caller for the whole line)
	input  wire        active,
	input  wire [31:0] base,
	input  wire        ts16,
	input  wire        b8,
	input  wire [1:0]  mwc,          // map width code: 32 << mwc tiles
	input  wire [1:0]  lprio,
	input  wire [10:0] px0,          // first map pixel column, already wrapped to the map width
	input  wire [10:0] py,           // map pixel row, already wrapped
	input  wire [9:0]  width,
	// tilemap read port (1 cycle latency, private to this layer)
	output wire        tm_rd,
	output wire [11:0] tm_addr,
	input  wire [31:0] tm_q,
	// graphics fetch: request held until granted; responses come back in order
	output wire        g_req,
	output wire [31:0] g_addr,
	input  wire        g_gnt,
	input  wire        r_push,
	input  wire [31:0] r_data,
	// pixel stream
	output wire        pix_valid,
	output wire [7:0]  pix_v,
	output wire [7:0]  pix_pal,
	output wire [2:0]  pix_p,
	input  wire        pix_take
);
	wire [2:0]  tsh   = ts16 ? 3'd4 : 3'd3;           // log2 tile size
	wire [1:0]  lppw  = b8 ? 2'd2 : 2'd3;             // log2 pixels per word (4 or 8) [3 fits in 2 bits]
	wire [3:0]  ppw   = b8 ? 4'd4 : 4'd8;
	wire [3:0]  tshift = 4'd5 + {2'd0, ts16, 1'b0} + {3'd0, b8};   // log2 bytes per tile
	wire [3:0]  rshift = 4'd2 + {3'd0, ts16} + {3'd0, b8};          // log2 bytes per tile row
	wire [3:0]  pwb   = 4'd5 + {2'd0, mwc} + {1'b0, tsh};           // log2 map width in pixels
	wire [10:0] pmask = (11'd1 << pwb) - 11'd1;
	wire [4:0]  tmask = ts16 ? 5'd15 : 5'd7;
	wire [2:0]  wmask = ((ts16 ? 3'd2 : 3'd1) << b8) - 3'd1;   // words per tile row - 1: 8x8 4bpp 0, 8x8 8bpp 1, 16x16 4bpp 1, 16x16 8bpp 3

	// ---------------------------------------------------------------- fetch
	// A: tilemap read issued -> B: entry on tm_q, copied to E -> E: graphics address computed into R
	// -> R: request held until granted. One word per cycle when granted every cycle.
	reg  [7:0]  j, nwords;            // words issued / needed (<= 162)
	reg  [10:0] col;                  // map column of the next word's first pixel
	reg  [10:0] b_col;
	reg         b_valid, b_first;
	reg         e_valid, e_first;
	reg  [15:0] e_tile;
	reg  [7:0]  e_pal;
	reg         e_hf, e_vf, e_pr;
	reg  [10:0] e_col;
	reg         r_valid;
	reg  [31:0] r_addr;
	reg  [21:0] r_meta;
	reg  [3:0]  inflight;             // words granted and not yet unpacked
	wire [3:0]  skip0 = {1'b0, px0[2:0]} & (ppw - 4'd1);

	wire [10:0] trow_full = py >> tsh;              // < map height
	wire [10:0] tcol_full = col >> tsh;             // < map width
	wire [13:0] tm_index  = ({7'd0, trow_full[6:0]} << (3'd5 + {1'b0, mwc})) | {7'd0, tcol_full[6:0]};
	assign tm_addr = tm_index[11:0];               // width x height <= 4096 for an active layer

	wire r_free = !r_valid || g_gnt;
	wire e_move = e_valid && r_free;
	wire e_free = !e_valid || e_move;
	wire b_move = b_valid && e_free;
	wire b_free = !b_valid || b_move;
	wire [3:0] occupancy = inflight + {3'd0, b_valid} + {3'd0, e_valid} + {3'd0, r_valid};
	wire can_issue_a = !start && active && j < nwords && b_free && occupancy < 4'd8;
	assign tm_rd = can_issue_a;

	// E stage: address of the graphics word
	wire [4:0]  tyy    = py[4:0] & tmask;
	wire [4:0]  ty     = e_vf ? (tmask - tyy) : tyy;
	wire [2:0]  wi     = (e_col[4:0] & tmask) >> lppw;
	wire [2:0]  wi2    = e_hf ? (wmask - wi) : wi;
	// tile, row in the tile and word in the row occupy disjoint bit fields: OR them, one addition left
	wire [31:0] e_off  = ({16'd0, e_tile} << tshift) | ({27'd0, ty} << rshift) | {27'd0, wi2, 2'b00};
	wire [31:0] e_addr = base + e_off;
	wire [21:0] e_meta = {e_first ? skip0[2:0] : 3'd0, e_hf, e_pr, e_pal, 9'd0};   // travels with the word

	assign g_req  = r_valid && !start;
	assign g_addr = r_addr;

	wire [21:0] meta_out;
	wire        meta_empty, resp_empty;
	wire [31:0] resp_out;
	wire        load;

	mc_fifo #(.W(22), .LOGD(3)) meta_fifo (.clk(clk), .rst_n(rst_n), .flush(start),
		.push(r_valid && g_gnt), .din(r_meta), .pop(load), .dout(meta_out), .empty(meta_empty), .full(), .count());
	mc_fifo #(.W(32), .LOGD(3)) resp_fifo (.clk(clk), .rst_n(rst_n), .flush(start),
		.push(r_push), .din(r_data), .pop(load), .dout(resp_out), .empty(resp_empty), .full(), .count());

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			j <= 0; nwords <= 0; col <= 0; b_col <= 0; b_valid <= 0; b_first <= 0; inflight <= 0;
			e_valid <= 0; e_first <= 0; e_tile <= 0; e_pal <= 0; e_hf <= 0; e_vf <= 0; e_pr <= 0; e_col <= 0;
			r_valid <= 0; r_addr <= 0; r_meta <= 0;
		end else if (start) begin
			j <= 0;
			nwords <= ({3'd0, skip0} + {1'd0, width} + {3'd0, ppw} - 11'd1) >> lppw;
			col <= px0 & ~{7'd0, ppw - 4'd1};
			b_valid <= 0; e_valid <= 0; r_valid <= 0; inflight <= 0;
		end else begin
			// R
			if (e_move) begin r_valid <= 1'b1; r_addr <= e_addr; r_meta <= e_meta; end
			else if (g_gnt) r_valid <= 1'b0;
			// E
			if (b_move) begin
				e_valid <= 1'b1; e_first <= b_first; e_col <= b_col;
				e_tile <= tm_q[15:0]; e_pal <= tm_q[23:16]; e_hf <= tm_q[24]; e_vf <= tm_q[25]; e_pr <= tm_q[26];
			end else if (e_move) e_valid <= 1'b0;
			// B
			if (b_move) b_valid <= 1'b0;
			if (can_issue_a) begin
				b_valid <= 1'b1;
				b_col <= col;
				b_first <= j == 0;
				j <= j + 8'd1;
				col <= (col + {7'd0, ppw}) & pmask;
			end
			inflight <= inflight + ((r_valid && g_gnt) ? 4'd1 : 4'd0) - (load ? 4'd1 : 4'd0);
		end
	end

	// ---------------------------------------------------------------- unpack
	reg        cur_valid;
	reg [31:0] cur_word;
	reg [2:0]  k;
	reg        cur_hf, cur_pr;
	reg [7:0]  cur_pal;
	wire [2:0] last_k = b8 ? 3'd3 : 3'd7;
	wire       need = !cur_valid || (pix_take && k == last_k);
	assign load = need && !meta_empty && !resp_empty;

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			cur_valid <= 0; cur_word <= 0; k <= 0; cur_hf <= 0; cur_pr <= 0; cur_pal <= 0;
		end else if (start) begin
			cur_valid <= 0;
		end else begin
			if (load) begin
				cur_valid <= 1'b1;
				cur_word <= resp_out;
				k <= meta_out[21:19];
				cur_hf <= meta_out[18];
				cur_pr <= meta_out[17];
				cur_pal <= meta_out[16:9];
			end else if (pix_take && cur_valid) begin
				if (k == last_k) cur_valid <= 1'b0;
				else k <= k + 3'd1;
			end
		end
	end

	wire [2:0] src = cur_hf ? (last_k - k) : k;
	assign pix_valid = !active || cur_valid;
	assign pix_v     = !active ? 8'd0 : b8 ? cur_word[8 * src[1:0] +: 8] : {4'd0, cur_word[4 * src +: 4]};
	assign pix_pal   = cur_pal;
	assign pix_p     = {lprio, cur_pr};
endmodule
