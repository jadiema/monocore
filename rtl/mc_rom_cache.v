// MONOCORE-1 read-only cache in front of the ROM (SDRAM).
// Direct mapped, 16-byte lines filled by a 4-beat burst from the ROM arbiter.
//
// Slave protocol (shared by every MONOCORE-1 bus slave):
//   rd=1 for one cycle with raddr starts a read; the data is returned in a later cycle by
//   ready=1 with rdata (at the earliest the next cycle, on a hit). ready=1 when idle.
//   A new rd may be issued in the same cycle that ready returns the previous read.
module mc_rom_cache #(
	parameter IDXW = 9,           // log2(number of lines): 9 -> 512 lines = 8 KB
	parameter AW   = 26           // ROM byte address width (64 MB)
) (
	input  wire          clk,
	input  wire          rst_n,
	input  wire          rd,
	input  wire [AW-1:0] raddr,
	output wire          ready,
	output wire [31:0]   rdata,
	// line fill
	output wire          f_req,
	output wire [AW-1:0] f_addr,
	input  wire          f_valid,   // one 32-bit beat; 4 beats per fill
	input  wire [31:0]   f_data
);
	localparam TW = AW - 4 - IDXW;
	localparam LINES = 1 << IDXW;

	localparam S_IDLE = 2'd0, S_CHECK = 2'd1, S_FILL = 2'd2, S_DONE = 2'd3;
	reg [1:0] state;
	reg [AW-1:0] paddr;           // address of the read in progress
	reg [1:0] beat;
	reg [31:0] captured;
	reg [LINES-1:0] valid;

	wire [IDXW-1:0] p_idx = paddr[4 + IDXW - 1:4];
	wire [TW-1:0]   p_tag = paddr[AW - 1:4 + IDXW];

	// tag store
	(* ramstyle = "M10K, no_rw_check" *) reg [TW-1:0] tagmem [0:LINES-1];
	reg [TW-1:0] tag_q;
	reg          valid_q;
	wire tag_we = state == S_FILL && f_valid && beat == 2'd3;
	always @(posedge clk) begin
		if (tag_we) tagmem[p_idx] <= p_tag;
		if (rd) tag_q <= tagmem[raddr[4 + IDXW - 1:4]];
	end
	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			valid <= {LINES{1'b0}};
			valid_q <= 1'b0;
		end else begin
			if (tag_we) valid[p_idx] <= 1'b1;
			if (rd) valid_q <= valid[raddr[4 + IDXW - 1:4]];
		end
	end

	// data store
	wire        fill_we = state == S_FILL && f_valid;
	wire [31:0] d_q;
	mc_ram32 #(.AW(IDXW + 2)) data_ram (
		.clk(clk), .en(rd | fill_we), .we({4{fill_we}}),
		.addr(fill_we ? {p_idx, beat} : raddr[IDXW + 3:2]),
		.wdata(f_data), .rdata(d_q)
	);

	wire hit = valid_q && tag_q == p_tag;

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			state <= S_IDLE;
			paddr <= {AW{1'b0}};
			beat <= 2'd0;
			captured <= 32'd0;
		end else begin
			case (state)
			S_IDLE, S_DONE: if (rd) begin state <= S_CHECK; paddr <= raddr; end
			                else state <= S_IDLE;
			S_CHECK: if (!hit) begin state <= S_FILL; beat <= 2'd0; end
			         else if (rd) paddr <= raddr;
			         else state <= S_IDLE;
			S_FILL: if (f_valid) begin
				if (beat == paddr[3:2]) captured <= f_data;
				beat <= beat + 2'd1;
				if (beat == 2'd3) state <= S_DONE;
			end
			endcase
		end
	end

	assign ready = state == S_IDLE || state == S_DONE || (state == S_CHECK && hit);
	assign rdata = state == S_DONE ? captured : d_q;
	assign f_req = state == S_FILL;
	assign f_addr = {paddr[AW - 1:4], 4'h0};
endmodule
