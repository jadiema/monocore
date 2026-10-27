// MONOCORE-1 scan-out: turns the rendered lines into a video signal for the MiSTer framework.
//
// The renderer computes line y at the start of line period y and hands its pixels over (px_valid,
// px_x, px_y, px_rgb) within that period; the scan-out shows it during line period y + 1, from one of
// two line buffers (written: y & 1, shown: the other). A pixel lasts 4 cycles (12.5 MHz at 50 MHz).
//
// Line layout, in pixels from the start of the line period: horizontal sync 0-55, picture from
// H_START to H_START + width, blanking for the rest (the period is 667 to 964 pixels depending on the
// height). Frame layout, in line periods: picture on lines 1 to height (showing lines 0 to height-1),
// vertical sync on 3 lines in the blanking.
module mc_scanout (
	input  wire        clk,
	input  wire        rst_n,
	input  wire [15:0] lcount,
	input  wire [9:0]  vcount,
	input  wire [9:0]  height,
	input  wire [8:0]  width,
	input  wire        px_valid,
	input  wire [8:0]  px_x,
	input  wire [9:0]  px_y,
	input  wire [14:0] px_rgb,
	output reg         ce_pix,
	output reg  [7:0]  r,
	output reg  [7:0]  g,
	output reg  [7:0]  b,
	output reg         hs,
	output reg         vs,
	output reg         hblank,
	output reg         vblank
);
	localparam H_SYNC = 56, H_START = 112;

	(* ramstyle = "M10K, no_rw_check" *) reg [14:0] lbuf [0:1023];
	always @(posedge clk) if (px_valid) lbuf[{px_y[0], px_x}] <= px_rgb;

	wire [13:0] hpos = lcount[15:2];
	wire        pix_edge = lcount[1:0] == 2'd3;          // last cycle of a pixel
	wire [9:0]  dline = vcount - 10'd1;                  // line on show
	wire        vis_line = vcount != 10'd0 && vcount <= height;
	wire [13:0] hrel = hpos - H_START;
	wire        vis_px = hpos >= H_START && hrel < {5'd0, width};

	// one cycle before the pixel edge: read the buffer
	reg [14:0] q;
	always @(posedge clk) if (lcount[1:0] == 2'd2) q <= lbuf[{dline[0], hrel[8:0]}];

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			ce_pix <= 0; r <= 0; g <= 0; b <= 0; hs <= 0; vs <= 0; hblank <= 1; vblank <= 1;
		end else begin
			ce_pix <= pix_edge;
			if (pix_edge) begin
				hblank <= !vis_px;
				vblank <= !vis_line;
				hs <= hpos < H_SYNC;                            // syncs active high, like the template's
				vs <= vcount >= height + 10'd4 && vcount < height + 10'd7;
				if (vis_px && vis_line) begin
					r <= {q[14:10], q[14:12]};
					g <= {q[9:5], q[9:7]};
					b <= {q[4:0], q[4:2]};
				end else begin
					r <= 0; g <= 0; b <= 0;
				end
			end
		end
	end
endmodule
