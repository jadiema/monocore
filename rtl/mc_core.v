// MONOCORE-1 core: Hazard3 RV32IM CPU, instruction and data buses, memories, I/O, DMA,
// frame timing. Everything runs on one 50 MHz clock.
//
// Memory map (SPEC.md section 3):
//   0x0000_0000 ROM (SDRAM, through the instruction and data caches)
//   0x1000_0000 WRAM 128 KB      0x1800_0000 NVRAM 32 KB
//   0x2000_0000 video            0x3000_0000 sound          0x4000_0000 I/O
module mc_core (
	input  wire        clk,
	input  wire        rst_n,
	// ROM port (SDRAM controller): see mc_rom_arb.v
	output wire        rom_req,
	output wire [25:0] rom_addr,
	input  wire        rom_ack,
	input  wire        rom_valid,
	input  wire [31:0] rom_data,
	input  wire [31:0] rom_size,      // ROM length in bytes (from the loader)
	// graphics read port (SDRAM, pipelined, in order): see mc_render.v
	output wire        gfx_req,
	output wire [25:0] gfx_addr,
	input  wire        gfx_rdy,
	input  wire        gfx_valid,
	input  wire [31:0] gfx_data,
	// inputs
	input  wire [11:0] in_p1,
	input  wire [11:0] in_p2,
	input  wire [11:0] in_p3,
	input  wire [11:0] in_p4,
	input  wire [2:0]  in_sys,
	input  wire [31:0] dip,
	input  wire [127:0] in_analog,
	input  wire [127:0] in_paddle,
	input  wire        kb_stb,
	input  wire        kb_down,
	input  wire        kb_ext,
	input  wire [7:0]  kb_code,
	input  wire        ms_stb,          // mouse: see mc_io.v
	input  wire [8:0]  ms_dx,
	input  wire [8:0]  ms_dy,
	input  wire [2:0]  ms_btn,
	input  wire [7:0]  ms_wheel,
	input  wire        midi_stb,        // MIDI: one pulse per byte received
	input  wire [7:0]  midi_byte,
	// the NVRAM's second port: its copy to and from the SD card (mister/mc_nvsave.sv)
	input  wire        nv_b_en,
	input  wire [3:0]  nv_b_we,
	input  wire [12:0] nv_b_addr,
	input  wire [31:0] nv_b_wdata,
	output wire [31:0] nv_b_rdata,
	output wire        nv_written,    // the CPU wrote to the NVRAM (this cycle)
	// debug output (simulation)
	output wire        dbg_valid,
	output wire [7:0]  dbg_char,
	// timing (for the testbench and, later, the video output)
	output wire [9:0]  vcount,
	output wire        vblank_pulse,
	output wire        line_start,
	// rendered pixels (scan-out comes with the MiSTer integration)
	output wire        px_valid,
	output wire [8:0]  px_x,
	output wire [9:0]  px_y,
	output wire [14:0] px_rgb,
	output wire [15:0] window_cycles,
	// video timing for the scan-out (rtl/mister/mc_scanout.v)
	output wire [15:0] lcount,        // cycle within the line
	output wire [9:0]  height,        // visible lines of this frame
	output wire [8:0]  width,         // visible pixels of this frame
	output wire [1:0]  rotation,
	// sound output, one stereo sample at 48 kHz
	output wire        audio_valid,
	output wire [15:0] audio_l,
	output wire [15:0] audio_r,
	// sound register writes, for the testbench log (not needed on the FPGA)
	output wire        dbg_apu_wr,
	output wire [12:0] dbg_apu_addr,
	output wire [31:0] dbg_apu_data,
	output wire [3:0]  dbg_apu_be
);
	// ================================================================ CPU
	wire [31:0] i_haddr, d_haddr, i_hwdata, d_hwdata;
	wire [1:0]  i_htrans, d_htrans;
	wire [2:0]  i_hsize, d_hsize;
	wire        i_hwrite, d_hwrite;
	wire        i_hready, d_hready;
	wire [31:0] i_hrdata, d_hrdata;
	wire        cpu_irq;
	wire        pwrup_req, unblock_out;

	hazard3_cpu_2port #(
		.RESET_VECTOR     (32'h00000100),
		.MTVEC_INIT       (32'h00000000),
		.EXTENSION_A      (0),
		.EXTENSION_C      (0),
		.EXTENSION_M      (1),
		.CSR_COUNTER      (1),
		.NUM_IRQS         (1),
		.MUL_FAST         (1),
		.MULH_FAST        (1),
		.MULDIV_UNROLL    (2),
		.FAST_BRANCHCMP   (1),
		.BRANCH_PREDICTOR (1)
	) cpu (
		.clk(clk), .clk_always_on(clk), .rst_n(rst_n),
		.pwrup_req(pwrup_req), .pwrup_ack(pwrup_req), .clk_en(), .unblock_out(unblock_out), .unblock_in(unblock_out),
		.i_haddr(i_haddr), .i_hwrite(i_hwrite), .i_htrans(i_htrans), .i_hsize(i_hsize), .i_hburst(), .i_hprot(),
		.i_hmastlock(), .i_hmaster(), .i_hready(i_hready), .i_hresp(1'b0), .i_hwdata(i_hwdata), .i_hrdata(i_hrdata),
		.d_haddr(d_haddr), .d_hwrite(d_hwrite), .d_htrans(d_htrans), .d_hsize(d_hsize), .d_hburst(), .d_hprot(),
		.d_hmastlock(), .d_hmaster(), .d_hexcl(), .d_hready(d_hready), .d_hresp(1'b0), .d_hexokay(1'b1),
		.d_hwdata(d_hwdata), .d_hrdata(d_hrdata),
		.fence_i_vld(), .fence_d_vld(), .fence_rdy(1'b1),
		.dbg_req_halt(1'b0), .dbg_req_halt_on_reset(1'b0), .dbg_req_resume(1'b0), .dbg_halted(), .dbg_running(),
		.dbg_data0_rdata(32'd0), .dbg_data0_wdata(), .dbg_data0_wen(),
		.dbg_instr_data(32'd0), .dbg_instr_data_vld(1'b0), .dbg_instr_data_rdy(),
		.dbg_instr_caught_exception(), .dbg_instr_caught_ebreak(),
		.dbg_sbus_addr(32'd0), .dbg_sbus_write(1'b0), .dbg_sbus_size(2'd0), .dbg_sbus_vld(1'b0),
		.dbg_sbus_rdy(), .dbg_sbus_err(), .dbg_sbus_wdata(32'd0), .dbg_sbus_rdata(),
		.mhartid_val(32'd0), .eco_version(4'd0),
		.irq(cpu_irq), .soft_irq(1'b0), .timer_irq(1'b0)
	);

	// ================================================================ data bus slaves
	localparam S_ROM = 3'd0, S_WRAM = 3'd1, S_NVRAM = 3'd2, S_GPU = 3'd3, S_APU = 3'd4, S_IO = 3'd5, S_NONE = 3'd6;
	function [2:0] dsel(input [31:0] a);
		case (a[31:28])
		4'h0: dsel = a[27:26] == 2'd0 ? S_ROM : S_NONE;
		4'h1: dsel = a[27:17] == 11'h000 ? S_WRAM :
		             a[27:15] == 13'h1000 ? S_NVRAM : S_NONE;   // 0x1800_0000 + 32 KB
		4'h2: dsel = a[27:20] == 8'h00 ? S_GPU : S_NONE;
		4'h3: dsel = a[27:13] == 15'h0000 ? S_APU : S_NONE;
		4'h4: dsel = a[27:12] == 16'h0000 ? S_IO : S_NONE;
		default: dsel = S_NONE;
		endcase
	endfunction

	// shared slave request lines (driven by the CPU data port or by the DMA engine)
	reg  [6:0]  s_rd, s_wr;
	reg  [31:0] s_raddr, s_waddr, s_wdata;
	reg  [3:0]  s_be;
	wire [6:0]  s_ready;
	wire [6:0]  s_wrok;             // slave accepts a write this cycle (video: not while rendering)
	wire [31:0] s_rdata [0:6];

	// ---------------------------------------------------------------- AHB-Lite data port
	reg        dph_valid, dph_write, dph_wdone;
	reg [31:0] dph_addr;
	reg [2:0]  dph_size, dph_sel;
	wire [2:0] aph_sel = dsel(d_haddr);
	wire wr_go = dph_valid && dph_write && !dph_wdone && s_wrok[dph_sel];
	// a block RAM port cannot serve a read address phase and a write data phase in the same
	// cycle: the write goes first and the read waits one cycle
	wire conflict = d_htrans[1] && !d_hwrite && wr_go && aph_sel == dph_sel && aph_sel != S_NONE;
	assign d_hready = !dph_valid ? 1'b1 :
	                  dph_write ? ((dph_wdone || wr_go) && s_ready[dph_sel] && !conflict) :
	                  s_ready[dph_sel];
	wire aph = d_htrans[1] && d_hready;
	// d_hready without the read-after-write conflict term, which depends on the CPU's late d_htrans
	wire hready_nc = !dph_valid ? 1'b1 : dph_write ? ((dph_wdone || wr_go) && s_ready[dph_sel]) : s_ready[dph_sel];
	assign d_hrdata = s_rdata[dph_sel];

	function [3:0] bytes(input [2:0] size, input [1:0] a);
		case (size)
		3'd0: bytes = 4'b0001 << a;
		3'd1: bytes = a[1] ? 4'b1100 : 4'b0011;
		default: bytes = 4'b1111;
		endcase
	endfunction

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			dph_valid <= 1'b0; dph_write <= 1'b0; dph_wdone <= 1'b0;
			dph_addr <= 32'd0; dph_size <= 3'd0; dph_sel <= S_NONE;
		end else begin
			if (wr_go && !d_hready) dph_wdone <= 1'b1;
			if (d_hready) begin
				dph_valid <= aph;
				dph_write <= d_hwrite;
				dph_addr  <= d_haddr;
				dph_size  <= d_hsize;
				dph_sel   <= aph ? aph_sel : S_NONE;
				dph_wdone <= 1'b0;
			end
		end
	end

	// ---------------------------------------------------------------- DMA engine
	// Started by a write to DMA_CTRL; the CPU stays stalled on that write until the end.
	wire        dma_start, dma_fill;
	wire [31:0] dma_src, dma_dst, dma_len;
	localparam D_IDLE = 2'd0, D_RD = 2'd1, D_WAIT = 2'd2, D_WR = 2'd3;
	reg [1:0]  dstate;
	reg        dfill, dword;          // fill mode; current transfer is a whole word
	reg [31:0] dsrc, ddst, dleft, dpat, ddata;
	wire       dma_busy = dstate != D_IDLE;
	reg        dma_done;
	// slave selects of the current addresses, kept in registers: each is computed with the address
	// it belongs to (start address, or address + 4 / + 1), so the select is ready with the address
	reg  [2:0] dsrc_sel, ddst_sel;
	wire       dma_word_ok = dleft >= 32'd4 && ddst[1:0] == 2'd0 && (dfill || dsrc[1:0] == 2'd0);
	wire [31:0] dsrc_n = dsrc + ((dfill ? dma_word_ok : dword) ? 32'd4 : 32'd1);
	wire [31:0] ddst_n = ddst + ((dfill ? dma_word_ok : dword) ? 32'd4 : 32'd1);

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			dstate <= D_IDLE; dfill <= 1'b0; dword <= 1'b0; dma_done <= 1'b0;
			dsrc <= 32'd0; ddst <= 32'd0; dleft <= 32'd0; dpat <= 32'd0; ddata <= 32'd0;
			dsrc_sel <= S_ROM; ddst_sel <= S_ROM;
		end else begin
			dma_done <= 1'b0;
			case (dstate)
			D_IDLE: if (dma_start) begin
				dfill <= dma_fill; dsrc <= dma_src; ddst <= dma_dst; dleft <= dma_len; dpat <= dma_src;
				dsrc_sel <= dsel(dma_src); ddst_sel <= dsel(dma_dst);
				dstate <= dma_len == 32'd0 ? D_IDLE : dma_fill ? D_WR : D_RD;
				if (dma_len == 32'd0) dma_done <= 1'b1;
			end
			D_RD: begin dword <= dma_word_ok; dstate <= D_WAIT; end
			D_WAIT: if (s_ready[dsrc_sel]) begin ddata <= s_rdata[dsrc_sel]; dstate <= D_WR; end
			D_WR: if (ddst_sel == S_IO || s_wrok[ddst_sel]) begin
				// one word, or one byte when unaligned or at the tail
				ddst <= ddst_n; ddst_sel <= dsel(ddst_n);
				if (!dfill) begin dsrc <= dsrc_n; dsrc_sel <= dsel(dsrc_n); end
				dleft <= dleft - ((dfill ? dma_word_ok : dword) ? 32'd4 : 32'd1);
				if ((dfill ? dma_word_ok : dword) ? dleft == 32'd4 : dleft == 32'd1) begin
					dstate <= D_IDLE; dma_done <= 1'b1;
				end else dstate <= dfill ? D_WR : D_RD;
			end
			endcase
		end
	end
	// byte to write in byte mode: fill pattern byte (index = bytes already written) or source byte
	wire [31:0] dfill_done = dma_len - dleft;
	wire [7:0]  dbyte = dfill ? dpat[8 * dfill_done[1:0] +: 8] : ddata[8 * dsrc[1:0] +: 8];
	wire        dwrite_word = dfill ? dma_word_ok : dword;

	// ---------------------------------------------------------------- request multiplexer
	always @(*) begin
		s_rd = 7'd0; s_wr = 7'd0;
		s_raddr = d_haddr; s_waddr = dph_addr; s_wdata = d_hwdata; s_be = bytes(dph_size, dph_addr[1:0]);
		if (dma_busy) begin
			s_raddr = {dsrc[31:2], 2'b00};
			s_waddr = dwrite_word ? ddst : {ddst[31:2], 2'b00};
			s_wdata = dwrite_word ? (dfill ? dpat : ddata) : {4{dbyte}};
			s_be    = dwrite_word ? 4'b1111 : (4'b0001 << ddst[1:0]);
			if (dstate == D_RD) s_rd[dsrc_sel] = 1'b1;
			if (dstate == D_WR && ddst_sel != S_IO && s_wrok[ddst_sel]) s_wr[ddst_sel] = 1'b1;
		end else begin
			if (aph && !d_hwrite) s_rd[aph_sel] = 1'b1;
			// block RAM slaves (video, WRAM, NVRAM) read the presented address whenever the bus can take
			// an address phase, transfer or not: their reads have no side effect and complete in one
			// cycle, and the read enable no longer waits for d_htrans, the CPU's latest signal (timing)
			if (hready_nc && (aph_sel == S_GPU || aph_sel == S_WRAM || aph_sel == S_NVRAM)) s_rd[aph_sel] = 1'b1;
			if (wr_go) s_wr[dph_sel] = 1'b1;
		end
	end

	// ================================================================ instruction bus
	// ROM through the instruction cache, or WRAM (port B)
	reg        iph_valid;
	reg        iph_wram;
	wire       i_rom_ready, i_aph = i_htrans[1] && i_hready;
	wire       i_is_wram = i_haddr[31:17] == 15'h0800;   // 0x1000_0000 - 0x1001_FFFF
	wire [31:0] i_rom_q, i_wram_q;
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin iph_valid <= 1'b0; iph_wram <= 1'b0; end
		else if (i_hready) begin iph_valid <= i_aph; iph_wram <= i_is_wram; end
	end
	assign i_hready = !iph_valid || iph_wram || i_rom_ready;
	assign i_hrdata = iph_wram ? i_wram_q : i_rom_q;

	// ================================================================ memories and devices
	wire        ic_freq, dc_freq;
	wire [25:0] ic_faddr, dc_faddr;
	wire [2:0]  f_valid;
	wire        apu_freq;
	wire [25:0] apu_faddr;
	wire [31:0] f_data;

	mc_rom_cache #(.IDXW(9)) icache (
		.clk(clk), .rst_n(rst_n), .rd(i_aph && !i_is_wram), .raddr(i_haddr[25:0]),
		.ready(i_rom_ready), .rdata(i_rom_q),
		.f_req(ic_freq), .f_addr(ic_faddr), .f_valid(f_valid[1]), .f_data(f_data));

	mc_rom_cache #(.IDXW(8)) dcache (
		.clk(clk), .rst_n(rst_n), .rd(s_rd[S_ROM]), .raddr(s_raddr[25:0]),
		.ready(s_ready[S_ROM]), .rdata(s_rdata[S_ROM]),
		.f_req(dc_freq), .f_addr(dc_faddr), .f_valid(f_valid[0]), .f_data(f_data));

	mc_rom_arb #(.N(3)) rom_arb (
		.clk(clk), .rst_n(rst_n),
		.req({apu_freq, ic_freq, dc_freq}), .addr({apu_faddr, ic_faddr, dc_faddr}), .valid(f_valid), .data(f_data),
		.m_req(rom_req), .m_addr(rom_addr), .m_ack(rom_ack), .m_valid(rom_valid), .m_data(rom_data));

	// WRAM: port A data bus, port B instruction fetch
	mc_ram32_dp #(.AW(15)) wram (.clk(clk),
		.a_en(s_rd[S_WRAM] || s_wr[S_WRAM]), .a_we(s_wr[S_WRAM] ? s_be : 4'd0),
		.a_addr(s_wr[S_WRAM] ? s_waddr[16:2] : s_raddr[16:2]), .a_wdata(s_wdata), .a_rdata(s_rdata[S_WRAM]),
		.b_en(i_aph && i_is_wram), .b_addr(i_haddr[16:2]), .b_rdata(i_wram_q));
	assign s_ready[S_WRAM] = 1'b1;
	assign s_wrok[S_WRAM] = 1'b1;

	mc_ram32_tdp #(.AW(13)) nvram (.clk(clk),
		.a_en(s_rd[S_NVRAM] || s_wr[S_NVRAM]), .a_we(s_wr[S_NVRAM] ? s_be : 4'd0),
		.a_addr(s_wr[S_NVRAM] ? s_waddr[14:2] : s_raddr[14:2]), .a_wdata(s_wdata), .a_rdata(s_rdata[S_NVRAM]),
		.b_en(nv_b_en), .b_we(nv_b_we), .b_addr(nv_b_addr), .b_wdata(nv_b_wdata), .b_rdata(nv_b_rdata));
	assign nv_written = s_wr[S_NVRAM];
	assign s_ready[S_NVRAM] = 1'b1;
	assign s_wrok[S_NVRAM] = 1'b1;

	// video
	wire [31:0] screen_w, screen_h, line_cmp;
	wire [9:0]  new_line, new_height;
	wire [8:0]  new_width;
	wire [31:0] frame;
	wire        in_vblank, line_pulse;

	mc_gpu gpu (
		.clk(clk), .rst_n(rst_n),
		.rd(s_rd[S_GPU]), .raddr(s_raddr[19:0]), .wr(s_wr[S_GPU]), .waddr(s_waddr[19:0]),
		.wdata(s_wdata), .be(s_be), .rdata(s_rdata[S_GPU]), .ready(s_ready[S_GPU]), .wr_ok(s_wrok[S_GPU]),
		.vcount(vcount), .frame(frame), .in_vblank(in_vblank),
		.line_start(line_start), .new_line(new_line), .new_height(new_height), .new_width(new_width),
		.vblank_pulse(vblank_pulse), .rom_size(rom_size),
		.screen_w(screen_w), .screen_h(screen_h), .line_cmp(line_cmp),
		.g_req(gfx_req), .g_addr(gfx_addr), .g_rdy(gfx_rdy), .g_valid(gfx_valid), .g_data(gfx_data),
		.px_valid(px_valid), .px_x(px_x), .px_y(px_y), .px_rgb(px_rgb), .window_cycles(window_cycles),
		.rotation(rotation));

	mc_vtiming vtiming (
		.clk(clk), .rst_n(rst_n), .screen_h_reg(screen_h), .screen_w_reg(screen_w), .line_cmp(line_cmp),
		.vcount(vcount), .height(height), .width(width), .new_line(new_line), .new_height(new_height), .new_width(new_width),
		.frame(frame), .in_vblank(in_vblank),
		.line_start(line_start), .vblank_pulse(vblank_pulse), .line_pulse(line_pulse), .lcount(lcount));

	// sound
	mc_apu apu (
		.clk(clk), .rst_n(rst_n),
		.rd(s_rd[S_APU]), .raddr(s_raddr[12:0]), .wr(s_wr[S_APU]), .waddr(s_waddr[12:0]),
		.wdata(s_wdata), .be(s_be), .rdata(s_rdata[S_APU]), .ready(s_ready[S_APU]), .wr_ok(s_wrok[S_APU]), .rom_size(rom_size),
		.f_req(apu_freq), .f_addr(apu_faddr), .f_valid(f_valid[2]), .f_data(f_data),
		.smp_valid(audio_valid), .smp_l(audio_l), .smp_r(audio_r));

	// I/O
	mc_io io (
		.clk(clk), .rst_n(rst_n),
		.rd(s_rd[S_IO]), .raddr(s_raddr[11:0]), .wr(s_wr[S_IO]), .waddr(s_waddr[11:0]),
		.wdata(s_wdata), .be(s_be), .rdata(s_rdata[S_IO]), .ready(s_ready[S_IO]),
		.in_p1(in_p1), .in_p2(in_p2), .in_p3(in_p3), .in_p4(in_p4), .in_sys(in_sys), .dip(dip),
		.in_analog(in_analog), .in_paddle(in_paddle),
		.kb_stb(kb_stb), .kb_down(kb_down), .kb_ext(kb_ext), .kb_code(kb_code),
		.ms_stb(ms_stb), .ms_dx(ms_dx), .ms_dy(ms_dy), .ms_btn(ms_btn), .ms_wheel(ms_wheel),
		.midi_stb(midi_stb), .midi_byte(midi_byte),
		.vblank_pulse(vblank_pulse), .line_pulse(line_pulse), .dma_done(dma_done), .irq(cpu_irq),
		.dma_start(dma_start), .dma_fill(dma_fill), .dma_src(dma_src), .dma_dst(dma_dst), .dma_len(dma_len),
		.dma_busy(dma_busy), .dbg_valid(dbg_valid), .dbg_char(dbg_char));

	assign dbg_apu_wr = s_wr[S_APU];
	assign dbg_apu_addr = s_waddr[12:0];
	assign dbg_apu_data = s_wdata;
	assign dbg_apu_be = s_be;

	// unmapped addresses read as zero
	assign s_rdata[S_NONE] = 32'd0;
	assign s_ready[S_NONE] = 1'b1;
	assign s_wrok[S_NONE] = 1'b1;
	assign s_wrok[S_ROM] = 1'b1;
	assign s_wrok[S_IO] = 1'b1;
endmodule
