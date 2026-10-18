// MONOCORE-1 small building blocks: FIFO and sequential divider.

// Synchronous FIFO, register based (small depths), show-ahead: dout is the head when !empty.
module mc_fifo #(
	parameter W = 32,
	parameter LOGD = 3           // depth = 2^LOGD
) (
	input  wire         clk,
	input  wire         rst_n,
	input  wire         flush,
	input  wire         push,
	input  wire [W-1:0] din,
	input  wire         pop,
	output wire [W-1:0] dout,
	output wire         empty,
	output wire         full,
	output reg  [LOGD:0] count
);
	(* ramstyle = "logic" *) reg [W-1:0] mem [0:(1 << LOGD) - 1];   // tiny: registers, asynchronous read
	reg [LOGD-1:0] rp, wp;
	assign dout  = mem[rp];
	assign empty = count == 0;
	assign full  = count == (1 << LOGD);
	wire do_push = push && !full;
	wire do_pop  = pop && !empty;
	always @(posedge clk) if (do_push) mem[wp] <= din;
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin rp <= 0; wp <= 0; count <= 0; end
		else if (flush) begin rp <= 0; wp <= 0; count <= 0; end
		else begin
			if (do_push) wp <= wp + 1'b1;
			if (do_pop) rp <= rp + 1'b1;
			count <= count + (do_push ? 1'b1 : 1'b0) - (do_pop ? 1'b1 : 1'b0);
		end
	end
endmodule

// Restoring divider, one quotient bit per cycle: q = n / d, r = n % d.
// The caller guarantees that the quotient fits in QW bits (n < d << QW).
module mc_div #(
	parameter NW = 24,
	parameter DW = 16,
	parameter QW = 9
) (
	input  wire          clk,
	input  wire          rst_n,
	input  wire          start,
	input  wire [NW-1:0] n,
	input  wire [DW-1:0] d,
	output reg  [QW-1:0] q,
	output wire [DW-1:0] r,
	output wire          busy
);
	reg [DW:0]   rem;
	reg [QW-1:0] nlow;            // dividend bits still to bring down
	reg [4:0]    left;
	reg [DW-1:0] dd;
	assign busy = left != 0;
	assign r = rem[DW-1:0];
	wire [DW+1:0] trial = {rem, nlow[QW-1]};
	wire          ge = trial >= {2'b00, dd};
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin rem <= 0; nlow <= 0; left <= 0; q <= 0; dd <= 0; end
		else if (start) begin
			rem  <= {1'b0, n[NW-1:QW]};     // < d by the caller's guarantee
			nlow <= n[QW-1:0];
			dd   <= d;
			q    <= 0;
			left <= QW[4:0];
		end else if (left != 0) begin
			rem  <= ge ? trial[DW:0] - {1'b0, dd} : trial[DW:0];
			nlow <= {nlow[QW-2:0], 1'b0};
			q    <= {q[QW-2:0], ge};
			left <= left - 5'd1;
		end
	end
endmodule
