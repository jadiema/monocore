// MONOCORE-1 scan-out: turns the rendered lines into a video signal for the MiSTer framework.
//
// The renderer computes line y at the start of line period y and hands its pixels over (px_valid,
// px_x, px_y, px_rgb) within that period; the scan-out shows it during line period y + 1, from one of
// two line buffers (written: the parity of the line period, shown: the other). A pixel lasts 4 cycles
// (12.5 MHz at 50 MHz).
//
// Interlaced mode: line period v renders picture line 2v + field, so the buffer is chosen by px_y[1];
// f1 tells the framework's scaler (sys/ascal.vhd) where the field goes. At the start of a field the scaler
// uses the VGA_F1 value it latched on the last active line of the previous field, and puts the new field on
// the odd output lines when that value is 0. As VGA_F1 alternates, this means VGA_F1 = 1 during the field
// that goes on the odd lines: f1 = field (seen on the board: the opposite polarity swaps the line pairs).
//
// Line layout, in pixels from the start of the line period: horizontal sync 0-55, picture from
// H_START to H_START + width, blanking for the rest (the period is 667 to 964 pixels depending on the
// height). A width above 384 (lines of 708 pixels or more) uses a shorter sync (0-31) and starts the
// picture at 56, so that 640 pixels fit. Frame layout, in line periods: picture on lines 1 to height (showing lines 0 to height-1),
// vertical sync on 3 lines in the blanking.
module mc_scanout (
	input  wire        clk,
	input  wire        rst_n,
	input  wire [15:0] lcount,
	input  wire [9:0]  vcount,
	input  wire [9:0]  height,
	input  wire [9:0]  width,
	input  wire        interlace,
	input  wire        field,
	input  wire        px_valid,
	input  wire [9:0]  px_x,
	input  wire [9:0]  px_y,
	input  wire [14:0] px_rgb,
	output reg         ce_pix,
	output reg  [7:0]  r,
	output reg  [7:0]  g,
	output reg  [7:0]  b,
	output reg         hs,
	output reg         vs,
	output reg         hblank,
	output reg         vblank,
	output reg         f1
);
	wire        wide = width > 10'd384;
	wire [13:0] H_SYNC  = wide ? 14'd32 : 14'd56;
	wire [13:0] H_START = wide ? 14'd56 : 14'd112;

	(* ramstyle = "M10K, no_rw_check" *) reg [14:0] lbuf [0:2047];
	wire        wslot = interlace ? px_y[1] : px_y[0];   // parity of the line period that renders it
	always @(posedge clk) if (px_valid) lbuf[{wslot, px_x}] <= px_rgb;

	wire [13:0] hpos = lcount[15:2];
	wire        pix_edge = lcount[1:0] == 2'd3;          // last cycle of a pixel
	wire [9:0]  dline = vcount - 10'd1;                  // line on show
	wire        vis_line = vcount != 10'd0 && vcount <= height;
	wire [13:0] hrel = hpos - H_START;
	wire        vis_px = hpos >= H_START && hrel < {4'd0, width};

	// one cycle before the pixel edge: read the buffer
	reg [14:0] q;
	always @(posedge clk) if (lcount[1:0] == 2'd2) q <= lbuf[{dline[0], hrel[9:0]}];

	always @(posedge clk or negedge rst_n) begin
		if (!rst_n) begin
			ce_pix <= 0; r <= 0; g <= 0; b <= 0; hs <= 0; vs <= 0; hblank <= 1; vblank <= 1; f1 <= 0;
		end else begin
			ce_pix <= pix_edge;
			if (pix_edge) begin
				hblank <= !vis_px;
				vblank <= !vis_line;
				hs <= hpos < H_SYNC;                            // syncs active high, like the template's
				vs <= vcount >= height + 10'd4 && vcount < height + 10'd7;
				f1 <= interlace && field;                      // 1 on the field of the odd picture lines
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
