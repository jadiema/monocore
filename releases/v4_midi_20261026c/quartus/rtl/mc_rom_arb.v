// MONOCORE-1 ROM arbiter: shares the ROM/SDRAM read port between line-fill requesters.
// Fixed priority, requester 0 highest. One burst of 4 x 32-bit beats at a time.
//
// Memory side protocol (implemented by the SDRAM controller, or by the simulation model):
//   m_req is held with m_addr (16-byte aligned) until m_ack; the controller then returns
//   4 beats with m_valid/m_data, in order, possibly spaced.
module mc_rom_arb #(
	parameter N  = 2,
	parameter AW = 26
) (
	input  wire            clk,
	input  wire            rst_n,
	input  wire [N-1:0]    req,
	input  wire [N*AW-1:0] addr,
	output wire [N-1:0]    valid,
	output wire [31:0]     data,
	output wire            m_req,
	output wire [AW-1:0]   m_addr,
	input  wire            m_ack,
	input  wire            m_valid,
	input  wire [31:0]     m_data
);
	reg busy, acked;
	reg [$clog2(N > 1 ? N : 2)-1:0] owner;
	reg [1:0] beats;

	// lowest-index requester wins
	reg [$clog2(N > 1 ? N : 2)-1:0] pick;
	integer i;
	always @(*) begin
		pick = 0;
		for (i = N - 1; i >= 0; i = i - 1) if (req[i]) pick = i[$clog2(N > 1 ? N : 2)-1:0];
	end

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			busy <= 1'b0; acked <= 1'b0; owner <= 0; beats <= 2'd0;
		end else if (!busy) begin
			if (|req) begin busy <= 1'b1; acked <= 1'b0; owner <= pick; beats <= 2'd0; end
		end else begin
			if (m_ack) acked <= 1'b1;
			if (m_valid) begin
				beats <= beats + 2'd1;
				if (beats == 2'd3) busy <= 1'b0;
			end
		end
	end

	assign m_req  = busy && !acked;
	assign m_addr = addr[owner * AW +: AW];
	assign data   = m_data;
	genvar g;
	generate for (g = 0; g < N; g = g + 1) begin: v
		assign valid[g] = busy && m_valid && owner == g;
	end endgenerate
endmodule
