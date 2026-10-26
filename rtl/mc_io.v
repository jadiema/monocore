// MONOCORE-1 I/O block (0x4000_0000): inputs (4 pads, analog sticks, paddles and spinners, keyboard, mouse),
// interrupt controller, timers, cycle counter, DMA registers and the debug output. Slave protocol: see mc_rom_cache.v.
module mc_io (
	input  wire        clk,
	input  wire        rst_n,
	// bus
	input  wire        rd,
	input  wire [11:0] raddr,
	input  wire        wr,
	input  wire [11:0] waddr,
	input  wire [31:0] wdata,
	input  wire [3:0]  be,
	output reg  [31:0] rdata,
	output wire        ready,
	// inputs
	input  wire [11:0] in_p1,
	input  wire [11:0] in_p2,
	input  wire [11:0] in_p3,
	input  wire [11:0] in_p4,
	input  wire [2:0]  in_sys,
	input  wire [31:0] dip,
	input  wire [127:0] in_analog,     // pads 1-4 (32 bits each): left X, left Y, right X, right Y
	input  wire [127:0] in_paddle,     // pads 1-4: paddle (7:0), spinner position (31:16)
	// keyboard: one pulse per key event, PS/2 set 2 code
	input  wire        kb_stb,
	input  wire        kb_down,
	input  wire        kb_ext,
	input  wire [7:0]  kb_code,
	// mouse: one pulse per PS/2 packet; 9-bit signed moves (Y positive upwards, as PS/2 sends it)
	input  wire        ms_stb,
	input  wire [8:0]  ms_dx,
	input  wire [8:0]  ms_dy,
	input  wire [2:0]  ms_btn,          // left, right, middle
	input  wire [7:0]  ms_wheel,        // signed wheel move of the packet
	// interrupt sources and CPU interrupt line
	input  wire        vblank_pulse,
	input  wire        line_pulse,
	input  wire        dma_done,
	output wire        irq,
	// DMA
	output wire        dma_start,
	output wire        dma_fill,
	output reg  [31:0] dma_src,
	output reg  [31:0] dma_dst,
	output reg  [31:0] dma_len,
	input  wire        dma_busy,
	// debug output (simulation only; ignored on the FPGA)
	output reg         dbg_valid,
	output reg  [7:0]  dbg_char
);
	reg [4:0]  irq_status, irq_enable;
	reg [31:0] t0_reload, t1_reload, t0_cnt, t1_cnt;
	reg        t0_on, t1_on;
	reg [63:0] cycles;

	// keyboard: state of the 256 keys and a queue of 16 events
	reg [255:0] key_state;
	reg [9:0]   kq [0:15];             // {down, ext, code}
	reg [3:0]   kq_rd, kq_wr;
	reg [4:0]   kq_n;
	wire [7:0]  key_index = kb_code == 8'h83 ? 8'h02 : {kb_ext, kb_code[6:0]};
	wire        wr_keyev = wr && waddr[11:2] == 10'h034;   // 0xD0: removes the oldest event
	wire        kq_push = kb_stb && kq_n != 5'd16;
	wire        kq_pop  = wr_keyev && kq_n != 5'd0;
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			key_state <= 256'd0; kq_rd <= 4'd0; kq_wr <= 4'd0; kq_n <= 5'd0;
		end else begin
			if (kb_stb) key_state[key_index] <= kb_down;
			if (kq_push) begin kq[kq_wr] <= {kb_down, kb_ext, kb_code}; kq_wr <= kq_wr + 4'd1; end
			if (kq_pop) kq_rd <= kq_rd + 4'd1;
			kq_n <= kq_n + {4'd0, kq_push} - {4'd0, kq_pop};
		end
	end
	wire [9:0] kq_head = kq[kq_rd];

	// mouse: position counters (16 bits, Y positive downwards), wheel counter, buttons
	reg [15:0] ms_x, ms_y;
	reg [7:0]  ms_wh;
	reg [2:0]  ms_b;
	reg        ms_seen;
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			ms_x <= 16'd0; ms_y <= 16'd0; ms_wh <= 8'd0; ms_b <= 3'd0; ms_seen <= 1'b0;
		end else if (ms_stb) begin
			ms_x <= ms_x + {{7{ms_dx[8]}}, ms_dx};
			ms_y <= ms_y - {{7{ms_dy[8]}}, ms_dy};
			ms_wh <= ms_wh + ms_wheel;
			ms_b <= ms_btn;
			ms_seen <= 1'b1;
		end
	end

	wire [31:0] bm = {{8{be[3]}}, {8{be[2]}}, {8{be[1]}}, {8{be[0]}}};
	function [31:0] merge(input [31:0] old);
		merge = (old & ~bm) | (wdata & bm);
	endfunction

	wire wr_irqs   = wr && waddr[11:2] == 10'h004;   // 0x10
	wire wr_irqen  = wr && waddr[11:2] == 10'h005;   // 0x14
	wire wr_t0rel  = wr && waddr[11:2] == 10'h008;   // 0x20
	wire wr_t0ctl  = wr && waddr[11:2] == 10'h009;   // 0x24
	wire wr_t1rel  = wr && waddr[11:2] == 10'h00A;   // 0x28
	wire wr_t1ctl  = wr && waddr[11:2] == 10'h00B;   // 0x2C
	wire wr_dmactl = wr && waddr[11:2] == 10'h013;   // 0x4C
	wire [31:0] t0_ctl_new = merge({31'd0, t0_on});
	wire [31:0] t1_ctl_new = merge({31'd0, t1_on});
	wire [31:0] irqen_new  = merge({27'd0, irq_enable});

	wire t0_fire = t0_on && t0_reload != 0 && t0_cnt <= 32'd1;
	wire t1_fire = t1_on && t1_reload != 0 && t1_cnt <= 32'd1;

	assign dma_start = wr_dmactl && be[0] && wdata[0];
	assign dma_fill  = wdata[1];
	assign ready = !(dma_start || dma_busy);
	assign irq = |(irq_status & irq_enable);

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			irq_status <= 5'd0; irq_enable <= 5'd0;
			t0_reload <= 32'd0; t1_reload <= 32'd0; t0_cnt <= 32'd0; t1_cnt <= 32'd0;
			t0_on <= 1'b0; t1_on <= 1'b0;
			cycles <= 64'd0;
			dma_src <= 32'd0; dma_dst <= 32'd0; dma_len <= 32'd0;
			dbg_valid <= 1'b0; dbg_char <= 8'd0;
		end else begin
			cycles <= cycles + 64'd1;
			dbg_valid <= 1'b0;

			// timers: fire every RELOAD cycles while enabled
			if (wr_t0rel) begin t0_reload <= merge(t0_reload); t0_cnt <= merge(t0_reload); end
			else if (wr_t0ctl && t0_ctl_new[0] && !t0_on) t0_cnt <= t0_reload;
			else if (t0_fire) t0_cnt <= t0_reload;
			else if (t0_on) t0_cnt <= t0_cnt - 32'd1;
			if (wr_t0ctl) t0_on <= t0_ctl_new[0];
			if (wr_t1rel) begin t1_reload <= merge(t1_reload); t1_cnt <= merge(t1_reload); end
			else if (wr_t1ctl && t1_ctl_new[0] && !t1_on) t1_cnt <= t1_reload;
			else if (t1_fire) t1_cnt <= t1_reload;
			else if (t1_on) t1_cnt <= t1_cnt - 32'd1;
			if (wr_t1ctl) t1_on <= t1_ctl_new[0];

			// interrupt status: sources set, writing 1 clears
			irq_status <= (irq_status & ~(wr_irqs ? wdata[4:0] & {5{be[0]}} : 5'd0))
			            | {dma_done, t1_fire, t0_fire, line_pulse, vblank_pulse};
			if (wr_irqen) irq_enable <= irqen_new[4:0];

			if (wr && waddr[11:2] == 10'h010) dma_src <= merge(dma_src);
			if (wr && waddr[11:2] == 10'h011) dma_dst <= merge(dma_dst);
			if (wr && waddr[11:2] == 10'h012) dma_len <= merge(dma_len);
			if (wr && waddr[11:2] == 10'h018 && be[0]) begin dbg_valid <= 1'b1; dbg_char <= wdata[7:0]; end
		end
	end

	always @(posedge clk) if (rd) begin
		case (raddr[11:2])
		10'h000: rdata <= {20'd0, in_p1};
		10'h001: rdata <= {20'd0, in_p2};
		10'h020: rdata <= {20'd0, in_p3};                  // 0x80
		10'h021: rdata <= {20'd0, in_p4};                  // 0x84
		10'h024: rdata <= in_analog[31:0];                 // 0x90
		10'h025: rdata <= in_analog[63:32];
		10'h026: rdata <= in_analog[95:64];
		10'h027: rdata <= in_analog[127:96];
		10'h028: rdata <= in_paddle[31:0];                 // 0xA0
		10'h029: rdata <= in_paddle[63:32];
		10'h02A: rdata <= in_paddle[95:64];
		10'h02B: rdata <= in_paddle[127:96];
		10'h02C: rdata <= key_state[31:0];                 // 0xB0
		10'h02D: rdata <= key_state[63:32];
		10'h02E: rdata <= key_state[95:64];
		10'h02F: rdata <= key_state[127:96];
		10'h030: rdata <= key_state[159:128];
		10'h031: rdata <= key_state[191:160];
		10'h032: rdata <= key_state[223:192];
		10'h033: rdata <= key_state[255:224];
		10'h034: rdata <= {kq_n != 5'd0, 21'd0, kq_head};  // 0xD0
		10'h035: rdata <= {ms_y, ms_x};                    // 0xD4
		10'h036: rdata <= {ms_seen, 15'd0, ms_wh, 5'd0, ms_b};   // 0xD8
		10'h002: rdata <= {29'd0, in_sys};
		10'h003: rdata <= dip;
		10'h004: rdata <= {27'd0, irq_status};
		10'h005: rdata <= {27'd0, irq_enable};
		10'h008: rdata <= t0_reload;
		10'h009: rdata <= {31'd0, t0_on};
		10'h00A: rdata <= t1_reload;
		10'h00B: rdata <= {31'd0, t1_on};
		10'h00C: rdata <= cycles[31:0];
		10'h00D: rdata <= cycles[63:32];
		10'h010: rdata <= dma_src;
		10'h011: rdata <= dma_dst;
		10'h012: rdata <= dma_len;
		10'h01C: rdata <= 32'h00000003;   // 0x70: hardware version 3 (2: pads 3-4, analog, keyboard; 3: mouse)
		default: rdata <= 32'd0;
		endcase
	end
endmodule
