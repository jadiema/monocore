// MONOCORE-1 I/O block (0x4000_0000): inputs, interrupt controller, timers, cycle counter,
// DMA registers and the debug output. Slave protocol: see mc_rom_cache.v.
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
	input  wire [2:0]  in_sys,
	input  wire [31:0] dip,
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
		10'h01C: rdata <= 32'h00000001;   // 0x70: hardware version 0.1
		default: rdata <= 32'd0;
		endcase
	end
endmodule
