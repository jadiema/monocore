// MONOCORE-1 block RAMs.
// Byte enables are built from four 8-bit lanes so that Quartus infers M10K blocks from plain
// Verilog-2005 (no SystemVerilog packed arrays). Reads are registered (one cycle latency).
// "no_rw_check": the design never reads an address in the cycle it is written through the other
// port (video and sound writes wait while the engines read), so Quartus may map each memory to a
// single true dual-port M10K instead of one copy per read port.

// single port: one read or write per cycle
module mc_ram32 #(
	parameter AW = 10            // word address width
) (
	input  wire          clk,
	input  wire          en,
	input  wire [3:0]    we,     // byte write enables (0 = read)
	input  wire [AW-1:0] addr,
	input  wire [31:0]   wdata,
	output wire [31:0]   rdata
);
	genvar i;
	generate for (i = 0; i < 4; i = i + 1) begin: lane
		mc_ram8 #(.AW(AW)) u (
			.clk(clk), .en(en), .we(we[i]), .addr(addr),
			.wdata(wdata[8 * i +: 8]), .rdata(rdata[8 * i +: 8])
		);
	end endgenerate
endmodule

module mc_ram8 #(
	parameter AW = 10
) (
	input  wire          clk,
	input  wire          en,
	input  wire          we,
	input  wire [AW-1:0] addr,
	input  wire [7:0]    wdata,
	output reg  [7:0]    rdata
);
	(* ramstyle = "M10K, no_rw_check" *) reg [7:0] mem [0:(1 << AW) - 1] /* verilator public */;
	always @(posedge clk) if (en) begin
		if (we) mem[addr] <= wdata;
		rdata <= mem[addr];
	end
endmodule

// true dual port: port A read/write, port B read only (same clock)
module mc_ram32_dp #(
	parameter AW = 10
) (
	input  wire          clk,
	input  wire          a_en,
	input  wire [3:0]    a_we,
	input  wire [AW-1:0] a_addr,
	input  wire [31:0]   a_wdata,
	output wire [31:0]   a_rdata,
	input  wire          b_en,
	input  wire [AW-1:0] b_addr,
	output wire [31:0]   b_rdata
);
	genvar i;
	generate for (i = 0; i < 4; i = i + 1) begin: lane
		mc_ram8_dp #(.AW(AW)) u (
			.clk(clk),
			.a_en(a_en), .a_we(a_we[i]), .a_addr(a_addr), .a_wdata(a_wdata[8 * i +: 8]), .a_rdata(a_rdata[8 * i +: 8]),
			.b_en(b_en), .b_addr(b_addr), .b_rdata(b_rdata[8 * i +: 8])
		);
	end endgenerate
endmodule

module mc_ram8_dp #(
	parameter AW = 10
) (
	input  wire          clk,
	input  wire          a_en,
	input  wire          a_we,
	input  wire [AW-1:0] a_addr,
	input  wire [7:0]    a_wdata,
	output reg  [7:0]    a_rdata,
	input  wire          b_en,
	input  wire [AW-1:0] b_addr,
	output reg  [7:0]    b_rdata
);
	(* ramstyle = "M10K, no_rw_check" *) reg [7:0] mem [0:(1 << AW) - 1] /* verilator public */;
	always @(posedge clk) if (a_en) begin
		if (a_we) mem[a_addr] <= a_wdata;
		a_rdata <= mem[a_addr];
	end
	always @(posedge clk) if (b_en) b_rdata <= mem[b_addr];
endmodule
