// MONOCORE-1 video block (0x2000_0000): registers, video memories and the line renderer.
// Every memory is a dual-port block RAM: port A for the CPU bus, port B for the renderer.
// CPU writes are held off (wr_ok = 0) while a line is being rendered. Slave protocol: mc_rom_cache.v.
module mc_gpu (
	input  wire        clk,
	input  wire        rst_n,
	// bus (offsets within 0x2000_0000 .. 0x200F_FFFF)
	input  wire        rd,
	input  wire [19:0] raddr,
	input  wire        wr,
	input  wire [19:0] waddr,
	input  wire [31:0] wdata,
	input  wire [3:0]  be,
	output wire [31:0] rdata,
	output wire        ready,
	output wire        wr_ok,
	// timing
	input  wire [9:0]  vcount,
	input  wire [31:0] frame,
	input  wire        in_vblank,
	input  wire        line_start,
	input  wire [9:0]  new_line,
	input  wire [9:0]  new_pline,       // picture line rendered in that line period
	input  wire [9:0]  new_height,
	input  wire [9:0]  new_width,
	input  wire        field,           // parity of the current field (STATUS bit 2)
	output wire        ilace_reg,       // DISP_CTRL bit 10: interlaced
	input  wire        vblank_pulse,
	input  wire [31:0] rom_size,
	// registers used by the timing generator
	output reg  [31:0] screen_w,
	output reg  [31:0] screen_h,
	output reg  [31:0] line_cmp,
	// graphics reads from the ROM (SDRAM)
	output wire        g_req,
	output wire [25:0] g_addr,
	input  wire        g_rdy,
	input  wire        g_valid,
	input  wire [31:0] g_data,
	// rendered pixels
	output wire        px_valid,
	output wire [9:0]  px_x,
	output wire [9:0]  px_y,
	output wire [14:0] px_rgb,
	output wire [15:0] window_cycles,
	output wire [1:0]  rotation        // DISP_CTRL bits 8-9, for the output scaler
);
	reg [31:0] disp_ctrl, bg_color, spr_chr, spr_count;
	assign rotation = disp_ctrl[9:8];
	assign ilace_reg = disp_ctrl[10];
	reg [31:0] l_ctrl [0:3], l_chr [0:3], l_sx [0:3], l_sy [0:3];
	wire [127:0] layer_ctrl, layer_chr, layer_scrollx, layer_scrolly;
	genvar g;
	generate for (g = 0; g < 4; g = g + 1) begin: lo
		assign layer_ctrl[32 * g +: 32]    = l_ctrl[g];
		assign layer_chr[32 * g +: 32]     = l_chr[g];
		assign layer_scrollx[32 * g +: 32] = l_sx[g];
		assign layer_scrolly[32 * g +: 32] = l_sy[g];
	end endgenerate

	wire busy;
	wire spr_overflow;
	wire [7:0] spr_peak;
	assign ready = 1'b1;
	assign wr_ok = !busy;
	wire [31:0] bm = {{8{be[3]}}, {8{be[2]}}, {8{be[1]}}, {8{be[0]}}};

	// ---------------------------------------------------------------- memory decode
	// 0 regs, 1 palette, 2 sprites, 3 tilemaps, 4 line tables, 5 CHR RAM, 7 unmapped
	function [2:0] region(input [19:0] a);
		if (a < 20'h01000)                     region = 3'd0;
		else if (a[19:13] == 7'b0001_000)      region = 3'd1;   // 0x10000-0x11FFF
		else if (a[19:13] == 7'b0010_000)      region = 3'd2;   // 0x20000-0x21FFF
		else if (a[19:16] == 4'h4)             region = 3'd3;   // 0x40000-0x4FFFF
		else if (a[19:13] == 7'b1000_000)      region = 3'd4;   // 0x80000-0x81FFF
		else if (a[19:17] == 3'b110)           region = 3'd5;   // 0xC0000-0xDFFFF
		else                                   region = 3'd7;
	endfunction
	wire [2:0] rreg = region(raddr);
	wire [2:0] wreg = region(waddr);
	wire [19:0] ma = wr ? waddr : raddr;          // the bus never reads and writes this slave together
	function sel(input [2:0] reg_id);
		sel = (rd && rreg == reg_id) || (wr && wreg == reg_id);
	endfunction
	function [3:0] we(input [2:0] reg_id);
		we = (wr && wreg == reg_id) ? be : 4'd0;
	endfunction

	// renderer ports
	wire [3:0]   tm_rd;
	wire [47:0]  tm_addr;
	wire [127:0] tm_q;
	wire         lt_rd, spr_rd, pal_rd, chr_rd;
	wire [10:0]  lt_addr, pal_addr;
	wire [8:0]   spr_addr;
	wire [14:0]  chr_addr;
	wire [31:0]  lt_q, pal_q, chr_q;
	wire [127:0] spr_q;

	wire [31:0] pal_a, lt_a, chr_a;
	mc_ram32_dp #(.AW(11)) palette (.clk(clk),
		.a_en(sel(3'd1)), .a_we(we(3'd1)), .a_addr(ma[12:2]), .a_wdata(wdata), .a_rdata(pal_a),
		.b_en(pal_rd), .b_addr(pal_addr), .b_rdata(pal_q));
	mc_ram32_dp #(.AW(11)) linetab (.clk(clk),
		.a_en(sel(3'd4)), .a_we(we(3'd4)), .a_addr(ma[12:2]), .a_wdata(wdata), .a_rdata(lt_a),
		.b_en(lt_rd), .b_addr(lt_addr), .b_rdata(lt_q));
	mc_ram32_dp #(.AW(15)) chrram (.clk(clk),
		.a_en(sel(3'd5)), .a_we(we(3'd5)), .a_addr(ma[16:2]), .a_wdata(wdata), .a_rdata(chr_a),
		.b_en(chr_rd), .b_addr(chr_addr), .b_rdata(chr_q));

	// sprite table: 4 banks (one per 32-bit word of an entry) so that a whole entry is read at once
	wire [31:0] spr_a [0:3];
	// tilemaps: one RAM per layer (16 KB each) so that the four layers are read in parallel
	wire [31:0] tm_a [0:3];
	generate for (g = 0; g < 4; g = g + 1) begin: banks
		wire spr_hit = ma[3:2] == g;
		wire [3:0] spr_we = spr_hit ? we(3'd2) : 4'd0;
		genvar k;
		for (k = 0; k < 4; k = k + 1) begin: lane
			if (g == 2 && k >= 2) begin: cpu_only
				// bytes 10-11 of an entry (reserved) are never read by the renderer: single port
				mc_ram8 #(.AW(9)) u (.clk(clk), .en(sel(3'd2) && spr_hit), .we(spr_we[k]), .addr(ma[12:4]),
					.wdata(wdata[8 * k +: 8]), .rdata(spr_a[g][8 * k +: 8]));
				assign spr_q[32 * g + 8 * k +: 8] = 8'd0;
			end else begin: dual
				mc_ram8_dp #(.AW(9)) u (.clk(clk), .a_en(sel(3'd2) && spr_hit), .a_we(spr_we[k]), .a_addr(ma[12:4]),
					.a_wdata(wdata[8 * k +: 8]), .a_rdata(spr_a[g][8 * k +: 8]),
					.b_en(spr_rd), .b_addr(spr_addr), .b_rdata(spr_q[32 * g + 8 * k +: 8]));
			end
		end
		wire tm_hit = ma[15:14] == g;
		mc_ram32_dp #(.AW(12)) tilemap (.clk(clk),
			.a_en(sel(3'd3) && tm_hit), .a_we(tm_hit ? we(3'd3) : 4'd0), .a_addr(ma[13:2]), .a_wdata(wdata),
			.a_rdata(tm_a[g]), .b_en(tm_rd[g]), .b_addr(tm_addr[12 * g +: 12]), .b_rdata(tm_q[32 * g +: 32]));
	end endgenerate

	// ---------------------------------------------------------------- registers
	integer i;
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			disp_ctrl <= 32'd0; screen_w <= 32'd320; screen_h <= 32'd240; bg_color <= 32'd0;
			line_cmp <= 32'd0; spr_chr <= 32'd0; spr_count <= 32'd0;
			for (i = 0; i < 4; i = i + 1) begin l_ctrl[i] <= 32'd0; l_chr[i] <= 32'd0; l_sx[i] <= 32'd0; l_sy[i] <= 32'd0; end
		end else if (wr && wreg == 3'd0) begin
			case (waddr[11:2])
			10'h000: disp_ctrl <= (disp_ctrl & ~bm) | (wdata & bm);
			10'h001: screen_w  <= (screen_w  & ~bm) | (wdata & bm);
			10'h002: screen_h  <= (screen_h  & ~bm) | (wdata & bm);
			10'h003: bg_color  <= (bg_color  & ~bm) | (wdata & bm);
			10'h004: line_cmp  <= (line_cmp  & ~bm) | (wdata & bm);
			10'h008: spr_chr   <= (spr_chr   & ~bm) | (wdata & bm);
			10'h009: spr_count <= (spr_count & ~bm) | (wdata & bm);
			default: if (waddr[11:7] == 5'b00010) begin   // 0x100-0x17F: layers
				case (waddr[3:2])
				2'd0: l_ctrl[waddr[6:5]] <= (l_ctrl[waddr[6:5]] & ~bm) | (wdata & bm);
				2'd1: l_chr[waddr[6:5]]  <= (l_chr[waddr[6:5]]  & ~bm) | (wdata & bm);
				2'd2: l_sx[waddr[6:5]]   <= (l_sx[waddr[6:5]]   & ~bm) | (wdata & bm);
				2'd3: l_sy[waddr[6:5]]   <= (l_sy[waddr[6:5]]   & ~bm) | (wdata & bm);
				endcase
			end
			endcase
		end
	end

	reg [31:0] reg_q;
	reg [2:0]  rsel;
	reg [1:0]  rbank;
	always @(posedge clk) if (rd) begin
		rsel <= rreg;
		rbank <= rreg == 3'd2 ? raddr[3:2] : raddr[15:14];
		case (raddr[11:2])
		10'h000: reg_q <= disp_ctrl;
		10'h001: reg_q <= screen_w;
		10'h002: reg_q <= screen_h;
		10'h003: reg_q <= bg_color;
		10'h004: reg_q <= line_cmp;
		10'h005: reg_q <= {22'd0, vcount};
		10'h006: reg_q <= frame;
		10'h007: reg_q <= {29'd0, field, spr_overflow, in_vblank};
		10'h008: reg_q <= spr_chr;
		10'h009: reg_q <= spr_count;
		10'h00A: reg_q <= {24'd0, spr_peak};
		default: if (raddr[11:7] == 5'b00010)
			case (raddr[3:2])
			2'd0: reg_q <= l_ctrl[raddr[6:5]];
			2'd1: reg_q <= l_chr[raddr[6:5]];
			2'd2: reg_q <= l_sx[raddr[6:5]];
			2'd3: reg_q <= l_sy[raddr[6:5]];
			endcase
		else reg_q <= 32'd0;
		endcase
	end

	assign rdata = rsel == 3'd0 ? reg_q :
	               rsel == 3'd1 ? pal_a :
	               rsel == 3'd2 ? spr_a[rbank] :
	               rsel == 3'd3 ? tm_a[rbank]  :
	               rsel == 3'd4 ? lt_a :
	               rsel == 3'd5 ? chr_a : 32'd0;

	// ---------------------------------------------------------------- renderer
	mc_render render (
		.clk(clk), .rst_n(rst_n),
		.line_start(line_start), .new_line(new_line), .new_pline(new_pline), .new_height(new_height), .new_width(new_width),
		.vblank_pulse(vblank_pulse),
		.disp_ctrl(disp_ctrl), .bg_color(bg_color), .spr_chr(spr_chr), .spr_count(spr_count),
		.layer_ctrl(layer_ctrl), .layer_chr(layer_chr), .layer_scrollx(layer_scrollx), .layer_scrolly(layer_scrolly),
		.rom_size(rom_size),
		.tm_rd(tm_rd), .tm_addr(tm_addr), .tm_q(tm_q), .lt_rd(lt_rd), .lt_addr(lt_addr), .lt_q(lt_q),
		.spr_rd(spr_rd), .spr_addr(spr_addr), .spr_q(spr_q), .pal_rd(pal_rd), .pal_addr(pal_addr), .pal_q(pal_q),
		.chr_rd(chr_rd), .chr_addr(chr_addr), .chr_q(chr_q),
		.g_req(g_req), .g_addr(g_addr), .g_rdy(g_rdy), .g_valid(g_valid), .g_data(g_data),
		.busy(busy), .spr_overflow(spr_overflow), .spr_peak(spr_peak), .window_cycles(window_cycles),
		.px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_rgb(px_rgb));
endmodule
