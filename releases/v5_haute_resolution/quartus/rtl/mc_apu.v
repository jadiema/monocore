// MONOCORE-1 sound block (0x3000_0000): registers and sound engine.
// Bit-exact implementation of SPEC.md section 5 (same arithmetic as sim/core.js mixSample).
// At each 48 kHz tick the engine walks the 8 PCM voices then the 8 PSG channels, sequentially,
// and produces one stereo 16-bit sample (see SPEC 5 for the time it takes, out of 1 041.67 cycles).
// Samples are read from the ROM through a one-line (16-byte) cache per voice.
module mc_apu (
	input  wire        clk,
	input  wire        rst_n,
	// bus (slave protocol: see mc_rom_cache.v)
	input  wire        rd,
	input  wire [12:0] raddr,
	input  wire        wr,
	input  wire [12:0] waddr,
	input  wire [31:0] wdata,
	input  wire [3:0]  be,
	output wire [31:0] rdata,
	output wire        ready,
	output wire        wr_ok,         // writes wait while a sample is being computed
	input  wire [31:0] rom_size,
	// ROM line fill (to mc_rom_arb)
	output wire        f_req,
	output wire [25:0] f_addr,
	input  wire        f_valid,
	input  wire [31:0] f_data,
	// output samples
	output reg         smp_valid,
	output reg  [15:0] smp_l,
	output reg  [15:0] smp_r
);
	// CPU reads take one extra cycle: the address is registered before the CTRL/POS selection
	reg rd1;
	assign ready = !rd1;

	// ---------------------------------------------------------------- register file
	reg  [10:0] e_raddr;
	wire        e_rd;
	wire [31:0] e_q, a_q;
	mc_ram32_dp #(.AW(11)) regs (.clk(clk),
		.a_en(rd || wr), .a_we(wr ? be : 4'd0), .a_addr(wr ? waddr[12:2] : raddr[12:2]), .a_wdata(wdata), .a_rdata(a_q),
		.b_en(e_rd), .b_addr(e_raddr), .b_rdata(e_q));

	// ---------------------------------------------------------------- voice state
	reg        playing [0:7];
	reg [31:0] pos [0:7];
	reg [15:0] frac [0:7];
	reg [5:0]  ad_idx [0:7];
	reg signed [11:0] ad_sig [0:7];
	reg [31:0] ad_next [0:7];
	reg        lc_valid [0:7];
	reg [21:0] lc_tag [0:7];
	reg [127:0] lc_data [0:7];
	// key on/off written by the CPU, applied by the engine before the voice's next sample
	reg [7:0]  kpend, kreset, kplay;
	// PSG state
	reg [31:0] ph [0:7];
	reg [14:0] lfsr [0:7];
	reg        noise [0:7];

	wire        key_wr = wr && waddr < 13'h100 && waddr[4:2] == 3'd5 && be[0];   // byte 0x14 of a voice
	wire [2:0]  key_v  = waddr[7:5];

	// CPU reads: CTRL and POS come from the engine (address registered first, data one cycle later)
	reg        r_special;
	reg [31:0] r_val;
	reg [12:0] ra_q;
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) rd1 <= 1'b0;
		else rd1 <= rd;
	end
	always @(posedge clk) begin
		if (rd) ra_q <= raddr;
		if (rd1) begin
			r_special <= (ra_q < 13'h100 && (ra_q[4:2] == 3'd5 || ra_q[4:2] == 3'd6)) || ra_q[12:2] == 11'h3C0;
			if (ra_q[12:2] == 11'h3C0)
				r_val <= {24'd0, master};
			else if (ra_q[4:2] == 3'd5)
				r_val <= {31'd0, kpend[ra_q[7:5]] ? kplay[ra_q[7:5]] : playing[ra_q[7:5]]};
			else
				r_val <= (kpend[ra_q[7:5]] && kreset[ra_q[7:5]]) ? 32'd0 : pos[ra_q[7:5]];
		end
	end
	assign rdata = r_special ? r_val : a_q;

	// ---------------------------------------------------------------- 48 kHz tick
	reg [26:0] tacc;
	reg        tick_pend;
	wire [27:0] tsum = {1'b0, tacc} + 28'd48000;
	wire        tick = tsum >= 28'd50000000;

	// ---------------------------------------------------------------- OKI ADPCM tables
	function [10:0] oki_step(input [5:0] i);
		case (i)
		0: oki_step = 16;   1: oki_step = 17;   2: oki_step = 19;   3: oki_step = 21;   4: oki_step = 23;
		5: oki_step = 25;   6: oki_step = 28;   7: oki_step = 31;   8: oki_step = 34;   9: oki_step = 37;
		10: oki_step = 41;  11: oki_step = 45;  12: oki_step = 50;  13: oki_step = 55;  14: oki_step = 60;
		15: oki_step = 66;  16: oki_step = 73;  17: oki_step = 80;  18: oki_step = 88;  19: oki_step = 97;
		20: oki_step = 107; 21: oki_step = 118; 22: oki_step = 130; 23: oki_step = 143; 24: oki_step = 157;
		25: oki_step = 173; 26: oki_step = 190; 27: oki_step = 209; 28: oki_step = 230; 29: oki_step = 253;
		30: oki_step = 279; 31: oki_step = 307; 32: oki_step = 337; 33: oki_step = 371; 34: oki_step = 408;
		35: oki_step = 449; 36: oki_step = 494; 37: oki_step = 544; 38: oki_step = 598; 39: oki_step = 658;
		40: oki_step = 724; 41: oki_step = 796; 42: oki_step = 876; 43: oki_step = 963; 44: oki_step = 1060;
		45: oki_step = 1166; 46: oki_step = 1282; 47: oki_step = 1411; default: oki_step = 1552;
		endcase
	endfunction
	function signed [4:0] oki_adj(input [2:0] n);
		oki_adj = n[2] ? (n == 3'd4 ? 5'sd2 : n == 3'd5 ? 5'sd4 : n == 3'd6 ? 5'sd6 : 5'sd8) : -5'sd1;
	endfunction

	// ---------------------------------------------------------------- engine
	// Each voice: read its 5 registers, load its state into working registers (no 16-way muxes in
	// the arithmetic), produce the sample, advance, write the state back. Every arithmetic step has
	// its own cycle so that no path holds more than one adder or one multiplier (margin on the 50 MHz clock).
	localparam E_IDLE = 5'd0,  E_VRD = 5'd1,  E_LOAD = 5'd2,  E_CHK = 5'd3,  E_FETCH = 5'd4,
	           E_FILL = 5'd5,  E_NIB2 = 5'd6, E_NIB3 = 5'd7,  E_ADPCM = 5'd8, E_MIX1 = 5'd9,
	           E_MIX2 = 5'd10, E_ADV1 = 5'd11, E_ADV2 = 5'd12, E_ADV3 = 5'd13, E_PRD = 5'd14,
	           E_P1 = 5'd15,   E_P2 = 5'd16,  E_P3 = 5'd17,   E_P4 = 5'd18,  E_P5 = 5'd19,
	           E_O1 = 5'd20,   E_O2 = 5'd21,  E_O3 = 5'd22;
	reg [4:0]  st;
	reg [3:0]  v;               // voice
	reg [2:0]  c;               // PSG channel
	reg [2:0]  rk;              // register word being read
	reg [31:0] r_start, r_len, r_loop, r_step, r_vol;   // r_vol = {.., FORMAT, VOL_R, VOL_L}
	reg signed [31:0] acc_l, acc_r;
	reg [31:0] fa;              // byte address being fetched
	reg        fphase;          // 16-bit format: 0 low byte, 1 high byte
	reg [7:0]  b_lo;
	reg signed [15:0] s;        // current sample value
	reg [4:0]  ncnt;            // ADPCM nibbles decoded for this sample
	reg [1:0]  fbeat;
	reg [127:0] fline;
	reg        st_adv;          // the voice produced a sample and must advance
	// working copy of the current voice
	reg        w_play, w_lcv;
	reg [31:0] w_pos, w_next;
	reg [15:0] w_frac;
	reg signed [11:0] w_sig;
	reg [5:0]  w_idx;
	reg [21:0] w_tag;
	reg [127:0] w_line;
	// pipeline registers
	reg [3:0]  nib_r;
	reg [13:0] odiff_r;
	reg signed [24:0] prod_l, prod_r;
	reg [32:0] adv_sum_r, adv_pos_r;
	reg [31:0] r_span;
	reg        loop_ok_r;
	reg [32:0] p_inc_r;
	reg [31:0] w_ph, ph_r;
	reg [14:0] w_lfsr;
	reg        w_noise, half_r, p_en_r;
	reg signed [16:0] ps_r;
	reg signed [25:0] pm_l, pm_r;
	reg signed [40:0] ml_l, ml_r;
	reg signed [47:0] ul_l, ul_r;

	wire [7:0] fmt = r_vol[23:16];
	wire       f_hit = w_lcv && w_tag == fa[25:4];
	wire       f_out = fa >= rom_size;
	wire [7:0] f_byte = f_out ? 8'd0 : w_line[8 * fa[3:0] +: 8];

	assign e_rd = (st == E_VRD && rk < 3'd5) || (st == E_PRD && rk < 3'd2);
	always @(*) begin
		if (st == E_PRD) e_raddr = 11'h100 + {5'd0, c, 2'd0} + {8'd0, rk};     // 0x400 + c * 0x10
		else             e_raddr = {3'd0, v, 3'd0} + {8'd0, rk};               // v * 0x20
	end
	assign f_req  = st == E_FILL;
	assign wr_ok  = st == E_IDLE && !tick_pend;
	assign f_addr = {fa[25:4], 4'h0};

	// ADPCM nibble decode, split over E_FETCH (nibble), E_NIB2 (product, index) and E_NIB3 (signal)
	wire [10:0] ostep = oki_step(w_idx);
	wire [15:0] oprod = {12'd0, nib_r[2:0], 1'b1} * {5'd0, ostep};       // (2n + 1) * step, up to 23 280
	wire [4:0]  oadj = oki_adj(nib_r[2:0]);
	wire signed [6:0]  oidx_n = $signed({1'b0, w_idx}) + $signed({{2{oadj[4]}}, oadj});
	wire [5:0]  oidx  = oidx_n < 0 ? 6'd0 : oidx_n > 7'sd48 ? 6'd48 : oidx_n[5:0];
	wire signed [13:0] onew = $signed({{2{w_sig[11]}}, w_sig}) + (nib_r[3] ? -$signed({1'b0, odiff_r[12:0]}) : $signed({1'b0, odiff_r[12:0]}));
	wire signed [11:0] osig = onew > 14'sd2047 ? 12'sd2047 : onew < -14'sd2048 ? 12'h800 : onew[11:0];   // 12'h800 = -2048

	// voice advance (E_ADV3)
	wire        adv_end  = adv_pos_r >= {1'b0, r_len};
	wire [32:0] loop_pos = adv_pos_r - {1'b0, r_span};

	// PSG
	wire [19:0] p_freq = r_start[19:0];
	wire [7:0]  p_wave = r_len[7:0], p_duty = r_len[15:8], p_vl = r_len[23:16], p_vr = r_len[31:24];
	wire [48:0] p_prod = {29'd0, p_freq} * 49'd366503875;
	wire [33:0] p_sum  = {2'd0, w_ph} + {1'b0, p_inc_r};
	wire [15:0] p_t    = ph_r[31:16];
	wire [7:0]  p_d    = p_duty == 8'd0 ? 8'd128 : p_duty;
	wire [14:0] p_lfsr = {w_lfsr[0] ^ w_lfsr[1], w_lfsr[14:1]};
	wire        p_step = p_wave == 8'd3 && half_r;                       // noise advances this sample
	wire        p_noise = p_step ? p_lfsr[0] : w_noise;
	reg signed [16:0] p_s;
	always @(*) begin
		case (p_wave)
		8'd0: p_s = ph_r < {p_d, 24'd0} ? 17'sd16384 : -17'sd16384;
		8'd1: p_s = p_t < 16'd32768 ? $signed({1'b0, p_t}) - 17'sd16384 : 17'sd49152 - $signed({1'b0, p_t});
		8'd2: p_s = $signed({2'b0, p_t[15:1]}) - 17'sd16384;
		8'd3: p_s = p_noise ? 17'sd16384 : -17'sd16384;
		default: p_s = 17'sd0;
		endcase
	end

	// master volume
	reg  [7:0] master;
	function [15:0] clamp16(input signed [31:0] x);
		clamp16 = x > 32'sd32767 ? 16'h7FFF : x < -32'sd32768 ? 16'h8000 : x[15:0];
	endfunction
	wire signed [47:0] o_l48 = ul_l >>> 16;
	wire signed [47:0] o_r48 = ul_r >>> 16;

	integer i;
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			for (i = 0; i < 8; i = i + 1) begin
				playing[i] <= 0; pos[i] <= 0; frac[i] <= 0; ad_idx[i] <= 0; ad_sig[i] <= 0; ad_next[i] <= 0;
				lc_valid[i] <= 0; lc_tag[i] <= 0; lc_data[i] <= 0;
			end
			for (i = 0; i < 8; i = i + 1) begin ph[i] <= 0; lfsr[i] <= 15'd1; noise[i] <= 1'b1; end
			kpend <= 0; kreset <= 0; kplay <= 0;
			tacc <= 0; tick_pend <= 0;
			st <= E_IDLE; v <= 0; c <= 0; rk <= 0;
			r_start <= 0; r_len <= 0; r_loop <= 0; r_step <= 0; r_vol <= 0;
			acc_l <= 0; acc_r <= 0; st_adv <= 0; fa <= 0; fphase <= 0; b_lo <= 0; s <= 0; ncnt <= 0; fbeat <= 0; fline <= 0;
			w_play <= 0; w_lcv <= 0; w_pos <= 0; w_next <= 0; w_frac <= 0; w_sig <= 0; w_idx <= 0; w_tag <= 0; w_line <= 0;
			nib_r <= 0; odiff_r <= 0; prod_l <= 0; prod_r <= 0; adv_sum_r <= 0; adv_pos_r <= 0; r_span <= 0; loop_ok_r <= 0;
			p_inc_r <= 0; w_ph <= 0; ph_r <= 0; w_lfsr <= 15'd1; w_noise <= 1'b1; half_r <= 0; p_en_r <= 0; ps_r <= 0;
			pm_l <= 0; pm_r <= 0; ml_l <= 0; ml_r <= 0; ul_l <= 0; ul_r <= 0;
			master <= 8'd255; smp_valid <= 0; smp_l <= 0; smp_r <= 0;
		end else begin
			smp_valid <= 1'b0;
			tacc <= tick ? tsum[26:0] - 27'd50000000 : tsum[26:0];
			if (tick) tick_pend <= 1'b1;
			// CPU writes (only while the engine is idle, see wr_ok): key on/off and the master volume
			if (key_wr) begin
				kpend[key_v] <= 1'b1;
				kplay[key_v] <= wdata[0];
				if (wdata[0]) kreset[key_v] <= 1'b1;
			end
			if (wr && waddr[12:2] == 11'h3C0 && be[0]) master <= wdata[7:0];

			case (st)
			E_IDLE: if (tick_pend) begin
				tick_pend <= 1'b0;
				acc_l <= 0; acc_r <= 0; v <= 0; rk <= 0;
				st <= E_VRD;
			end
			E_VRD: begin
				// read START, LENGTH, LOOP, STEP, volumes/format (one word per cycle, 1 cycle latency)
				if (rk != 0) case (rk - 3'd1)
					3'd0: r_start <= e_q;
					3'd1: r_len <= e_q;
					3'd2: r_loop <= e_q;
					3'd3: r_step <= e_q;
					default: r_vol <= e_q;
				endcase
				rk <= rk + 3'd1;
				if (rk == 3'd5) st <= E_LOAD;
			end
			E_LOAD: begin
				w_lcv <= lc_valid[v]; w_tag <= lc_tag[v]; w_line <= lc_data[v];
				if (kpend[v] && !(key_wr && key_v == v)) begin
					w_play <= kplay[v];
					if (kreset[v]) begin
						w_pos <= 0; w_frac <= 0; w_idx <= 0; w_sig <= 0; w_next <= 0;
					end else begin
						w_pos <= pos[v]; w_frac <= frac[v]; w_idx <= ad_idx[v]; w_sig <= ad_sig[v]; w_next <= ad_next[v];
					end
					kpend[v] <= 1'b0; kreset[v] <= 1'b0;
				end else begin
					w_play <= playing[v];
					w_pos <= pos[v]; w_frac <= frac[v]; w_idx <= ad_idx[v]; w_sig <= ad_sig[v]; w_next <= ad_next[v];
				end
				st <= E_CHK;
			end
			E_CHK: begin
				ncnt <= 0; fphase <= 1'b0;
				st_adv <= 1'b1;
				r_span <= r_len - r_loop;
				loop_ok_r <= fmt < 8'd2 && r_loop != 32'hFFFFFFFF && r_loop < r_len;
				if (!w_play) begin st_adv <= 1'b0; st <= E_ADV3; end
				else if (w_pos >= r_len) begin w_play <= 1'b0; st_adv <= 1'b0; st <= E_ADV3; end
				else if (fmt == 8'd0) begin fa <= r_start + w_pos; st <= E_FETCH; end
				else if (fmt == 8'd1) begin fa <= r_start + {w_pos[30:0], 1'b0}; st <= E_FETCH; end
				else if (w_next <= w_pos) begin fa <= r_start + {1'b0, w_next[31:1]}; st <= E_FETCH; end
				else begin s <= {w_sig, 4'd0}; st <= E_MIX1; end
			end
			E_FETCH: begin
				if (!f_out && !f_hit) begin st <= E_FILL; fbeat <= 0; end
				else if (fmt == 8'd0) begin s <= {f_byte, 8'd0}; st <= E_MIX1; end
				else if (fmt == 8'd1) begin
					if (!fphase) begin b_lo <= f_byte; fa <= fa + 32'd1; fphase <= 1'b1; end
					else begin s <= {f_byte, b_lo}; st <= E_MIX1; end
				end else begin
					nib_r <= w_next[0] ? f_byte[3:0] : f_byte[7:4];
					st <= E_NIB2;
				end
			end
			E_FILL: if (f_valid) begin
				fline[32 * fbeat +: 32] <= f_data;
				fbeat <= fbeat + 2'd1;
				if (fbeat == 2'd3) begin
					w_lcv <= 1'b1; w_tag <= fa[25:4];
					w_line <= {f_data, fline[95:0]};
					st <= E_FETCH;
				end
			end
			E_NIB2: begin
				odiff_r <= oprod[15:3];               // (2n + 1) * step / 8
				w_idx <= oidx;
				st <= E_NIB3;
			end
			E_NIB3: begin
				w_sig <= osig;
				w_next <= w_next + 32'd1;
				ncnt <= ncnt + 5'd1;
				st <= E_ADPCM;
			end
			E_ADPCM: begin
				if (ncnt < 5'd16 && w_next <= w_pos) begin fa <= r_start + {1'b0, w_next[31:1]}; st <= E_FETCH; end
				else begin s <= {w_sig, 4'd0}; st <= E_MIX1; end
			end
			E_MIX1: begin
				prod_l <= s * $signed({1'b0, r_vol[7:0]});
				prod_r <= s * $signed({1'b0, r_vol[15:8]});
				adv_sum_r <= {17'd0, w_frac} + {1'b0, r_step};
				st <= E_MIX2;
			end
			E_MIX2: begin
				acc_l <= acc_l + ($signed({{7{prod_l[24]}}, prod_l}) >>> 8);   // (s * vol) >> 8, arithmetic
				acc_r <= acc_r + ($signed({{7{prod_r[24]}}, prod_r}) >>> 8);
				adv_pos_r <= {1'b0, w_pos} + {16'd0, adv_sum_r[32:16]};
				w_frac <= adv_sum_r[15:0];
				st <= E_ADV3;
			end
			E_ADV3: begin
				// advance the voice that produced a sample, then write its state back
				if (st_adv) begin
					if (adv_end) begin
						if (loop_ok_r) begin
							pos[v] <= loop_pos[31:0];
							playing[v] <= loop_pos < {1'b0, r_len};
						end else begin
							pos[v] <= adv_pos_r[31:0];
							playing[v] <= 1'b0;
						end
					end else begin
						pos[v] <= adv_pos_r[31:0];
						playing[v] <= w_play;
					end
				end else begin
					pos[v] <= w_pos;
					playing[v] <= w_play;
				end
				frac[v] <= w_frac; ad_idx[v] <= w_idx; ad_sig[v] <= w_sig; ad_next[v] <= w_next;
				lc_valid[v] <= w_lcv; lc_tag[v] <= w_tag; lc_data[v] <= w_line;
				rk <= 0;
				if (v == 4'd7) begin c <= 0; st <= E_PRD; end
				else begin v <= v + 4'd1; st <= E_VRD; end
			end
			E_PRD: begin
				if (rk == 3'd1) r_start <= e_q;            // FREQ
				if (rk == 3'd2) r_len <= e_q;              // WAVE, DUTY, VOL_L, VOL_R
				rk <= rk + 3'd1;
				if (rk == 3'd2) st <= E_P1;
			end
			E_P1: begin
				p_inc_r <= p_prod[48:16];
				p_en_r <= (p_vl != 0 || p_vr != 0) && p_freq != 0;
				w_ph <= ph[c]; w_lfsr <= lfsr[c]; w_noise <= noise[c];
				st <= E_P2;
			end
			E_P2: begin
				ph_r <= p_sum[31:0];
				half_r <= p_sum[33:31] != {2'd0, w_ph[31]};
				st <= E_P3;
			end
			E_P3: begin
				ps_r <= p_s;
				if (p_step) begin w_lfsr <= p_lfsr; w_noise <= p_lfsr[0]; end
				st <= E_P4;
			end
			E_P4: begin
				pm_l <= ps_r * $signed({1'b0, p_vl});
				pm_r <= ps_r * $signed({1'b0, p_vr});
				st <= E_P5;
			end
			E_P5: begin
				if (p_en_r) begin
					ph[c] <= ph_r; lfsr[c] <= w_lfsr; noise[c] <= w_noise;
					acc_l <= acc_l + ($signed({{6{pm_l[25]}}, pm_l}) >>> 8);
					acc_r <= acc_r + ($signed({{6{pm_r[25]}}, pm_r}) >>> 8);
				end
				rk <= 0;
				if (c == 3'd7) st <= E_O1;
				else begin c <= c + 3'd1; st <= E_PRD; end
			end
			E_O1: begin
				ml_l <= acc_l * $signed({1'b0, master});
				ml_r <= acc_r * $signed({1'b0, master});
				st <= E_O2;
			end
			E_O2: begin
				ul_l <= $signed({{7{ml_l[40]}}, ml_l}) * 48'sd77;
				ul_r <= $signed({{7{ml_r[40]}}, ml_r}) * 48'sd77;
				st <= E_O3;
			end
			E_O3: begin
				smp_valid <= 1'b1;
				smp_l <= clamp16(o_l48[31:0]);
				smp_r <= clamp16(o_r48[31:0]);
				st <= E_IDLE;
			end
			default: st <= E_IDLE;
			endcase
		end
	end
endmodule
